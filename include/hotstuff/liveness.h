#ifndef _HOTSTUFF_LIVENESS_H
#define _HOTSTUFF_LIVENESS_H

#include "hotstuff/consensus.h"

namespace hotstuff {

/** Abstraction for liveness gadget (oracle). */
class PaceMaker {
    protected:
    HotStuffCore *hsc;
    public:
    virtual ~PaceMaker() = default;
    /** Initialize the PaceMaker. A derived class should also call the
     * default implementation to set `hsc`. */
    virtual void init(HotStuffCore *_hsc) { hsc = _hsc; }
    /** Get a promise resolved when the pace maker thinks it is a *good* time
     * to issue new commands. When promise is resolved, the replica should
     * propose the command. */
    virtual promise_t beat() = 0;
    /** Get the current proposer. */
    virtual ReplicaID get_proposer() = 0;
    /** Select the parent blocks for a new block.
     * @return Parent blocks. The block at index 0 is the direct parent, while
     * the others are uncles/aunts. The returned vector should be non-empty. */
    virtual std::vector<block_t> get_parents() = 0;
    /** Get a promise resolved when the pace maker thinks it is a *good* time
     * to vote for a block. The promise is resolved with the next proposer's ID
     * */
    virtual promise_t next_proposer(ReplicaID last_proposer) = 0;
};

using pacemaker_bt = BoxObj<PaceMaker>;

/** Parent selection implementation for PaceMaker: select all parents.
 * PaceMakers derived from this class will select the highest block as the
 * direct parent, while including other tail blocks (up to parent_limit) as
 * uncles/aunts. */
class PMAllParents: public virtual PaceMaker {
    const int32_t parent_limit;         /**< maximum number of parents */
    public:
    PMAllParents(int32_t parent_limit): parent_limit(parent_limit) {}
    std::vector<block_t> get_parents() override {
        auto tails = hsc->get_tails();
        size_t nparents = tails.size();
        if (parent_limit > 0)
            nparents = std::min(nparents, (size_t)parent_limit);
        assert(nparents > 0);
        block_t p = *tails.rbegin();
        std::vector<block_t> parents{p};
        nparents--;
        /* add the rest of tails as "uncles/aunts" */
        while (nparents--)
        {
            auto it = tails.begin();
            parents.push_back(*it);
            tails.erase(it);
        }
        return std::move(parents);
    }
};

/** Beat implementation for PaceMaker: simply wait for the QC of last proposed
 * block.  PaceMakers derived from this class will beat only when the last
 * block proposed by itself gets its QC. */
class PMWaitQC: public virtual PaceMaker {
    std::queue<promise_t> pending_beats;
    block_t last_proposed;
    bool locked;

    protected:
    void schedule_next() {
        if (!pending_beats.empty() && !locked)
        {
            auto pm = pending_beats.front();
            pending_beats.pop();
            hsc->async_qc_finish(last_proposed).then([this, pm]() {
                pm.resolve(get_proposer());
            });
            locked = true;
        }
    }

    void update_last_proposed() {
        hsc->async_wait_propose().then([this](block_t blk) {
            update_last_proposed();
            last_proposed = blk;
            locked = false;
            schedule_next();
        });
    }

    public:
    void init(HotStuffCore *hsc) override {
        PaceMaker::init(hsc);
        last_proposed = hsc->get_genesis();
        locked = false;
        update_last_proposed();
    }

    ReplicaID get_proposer() override {
        return hsc->get_id();
    }

    promise_t beat() override {
        promise_t pm;
        pending_beats.push(pm);
        schedule_next();
        return pm;
    }

    promise_t next_proposer(ReplicaID last_proposer) override {
        return promise_t([last_proposer](promise_t &pm) {
            pm.resolve(last_proposer);
        });
    }
};

/** Naive PaceMaker where everyone can be a proposer at any moment. */
struct PaceMakerDummy: public PMAllParents, public PMWaitQC {
    PaceMakerDummy(int32_t parent_limit):
        PMAllParents(parent_limit), PMWaitQC() {}
};

/** PaceMakerDummy with a fixed proposer. */
class PaceMakerDummyFixed: public PaceMakerDummy {
    ReplicaID proposer;

    public:
    PaceMakerDummyFixed(ReplicaID proposer,
                        int32_t parent_limit):
        PaceMakerDummy(parent_limit),
        proposer(proposer) {}

    ReplicaID get_proposer() override {
        return proposer;
    }

    promise_t next_proposer(ReplicaID) override {
        return promise_t([this](promise_t &pm) {
            pm.resolve(proposer);
        });
    }
};

/**
 * Simple long-standing proposer liveness gadget.
 * There are three roles for each replica: proposer, candidate and follower.
 *
 * For a proposer, it proposes a new block and refrains itself from proposing
 * the next block unless it receives the QC for the previous block. It will
 * give up the leadership and turn into a candidate when it hasn't seen such QC
 * for a while.
 *
 * For a follower, it never proposes any block, but keeps a timer for the QC
 * for the block last proposed by the proposer (the proposer it believes to
 * be). When it times out without seeing such QC, the follower turns into a
 * candidate.
 *
 * For a candidate, it periodically proposes empty blocks to synchronize the
 * preferred branch, with randomized timeout, and check for any new QC. Once it
 * sees such new QC, if the QC is given by itself, it becomes the proposer,
 * otherwise yields to the creator of the QC as a follower.
 */
class PMStickyProposer: public PMWaitQC {
    enum {
        PROPOSER,
        FOLLOWER,
        CANDIDATE
    } role;
    double qc_timeout;
    double candidate_timeout;
    EventContext eb;
    /** QC timer or randomized timeout */
    Event timer;
    block_t last_proposed;
    /** the proposer it believes when it is a follower */
    ReplicaID proposer;

    /* extra state needed for a proposer */
    std::queue<promise_t> pending_beats;
    bool locked;

    /* extra state needed for a candidate */
    std::unordered_map<ReplicaID,
        std::pair<block_t, promise_t>> last_proposed_by;

    promise_t pm_wait_receive_proposal;
    promise_t pm_wait_propose;
    promise_t pm_qc_finish;

    void clear_promises() {
        pm_wait_receive_proposal.reject();
        pm_wait_propose.reject();
        pm_qc_finish.reject();
        for (auto &p: last_proposed_by)
            p.second.reject();
        last_proposed_by.clear();
    }

    void reg_follower_receive_proposal() {
        pm_wait_receive_proposal =
            hsc->async_wait_receive_proposal().then(
                std::bind(&PMStickyProposer::follower_receive_proposal, this, _1, _2));
    }

    void follower_receive_proposal(const Proposal &prop) {
        if (prop.proposer == proposer)
        {
            auto qc_ref = prop.blk->qc_ref;
            if (last_proposed)
            {
                if (qc_ref != last_proposed)
                    to_candidate(); /* proposer misbehave */
            }
            last_proposed = prop.blk;
            /* reset QC timer */
            timer.del();
            timer.add_with_timeout(qc_timeout);
        }
        reg_follower_receive_proposal();
    }

    void proposer_schedule_next() {
        if (!pending_beats.empty() && !locked)
        {
            auto pm = pending_beats.front();
            pending_beats.pop();
            pm_qc_finish =
                hsc->async_qc_finish(last_proposed).then([this, pm]() {
                    pm.resolve(proposer);
                });
            locked = true;
        }
    }

    void reg_proposer_propose() {
        pm_wait_propose = hsc->async_wait_propose().then(
            std::bind(&PMStickyProposer::proposer_propose, this, _1, _2));
    }

    void proposer_propose(const block_t &blk) {
        last_proposed = blk;
        locked = false;
        proposer_schedule_next();
        reg_proposer_propose();
        timer.del();
        timer.add_with_timeout(qc_timeout);
    }

    void candidate_qc_timeout() {
        pm_qc_finish.reject();
        hsc->async_wait_propose().then([this](const block_t &blk) {
            pm_qc_finish = hsc->async_qc_finish(blk).then([this]() {
                /* managed to collect a QC */
                to_proposer();
            });
        });
        on_propose(std::vector<comman_t>{}, get_parents());
        timer.del();
        timer.add_with_timeout(gen_rand_timeout(candidate_timeout));
    }

    void reg_candidate_receive_proposal() {
        pm_wait_receive_proposal =
            hsc->async_wait_receive_proposal().then(
                std::bind(&PMStickyProposer::candidate_receive_proposal, this, _1, _2));
    }

    void candidate_receive_proposal(const Proposal &prop) {
        auto &p = last_proposed_by[prop.proposer];
        p.second.reject();
        p = std::make_pair(prop.blk, hsc->async_qc_finish(prop.blk).then([this]() {
            to_follower(prop.proposer);
        }));
    }

    void to_follower(ReplicaID new_proposer) {
        clear_promises();
        role = FOLLOWER;
        proposer = new_proposer;
        last_proposed = nullptr;
        timer = Event(eb, -1, 0, [this](int, short) {
            /* unable to get a QC in time */
            to_candidate();
        });
        reg_follower_receive_proposal();
        /* redirect all pending cmds to the new proposer */
        for (auto &pm: pending_beats)
            pm.resolve(proposer);
        pending_beats.clear();
    }

    void to_proposer() {
        clear_promises();
        role = PROPOSER;
        proposer = hsc->get_id();
        last_proposed = hsc->get_genesis();
        timer = Event(eb, -1, 0, [this](int, short) {
            /* proposer unable to get a QC in time */
            to_candidate();
        });
        /* prepare the variables for the role of a proposer */
        locked = false;
        reg_proposer_propose();
    }

    void to_candidate() {
        clear_promises();
        role = CANDIDATE;
        proposer = hsc->get_id();
        timer = Event(eb, -1, 0, [this](int, short) {
            candidate_qc_timeout();
        });
        candidate_timeout = qc_timeout;
        candidate_qc_timeout();
    }

    public:
    void init(HotStuffCore *hsc) override {
        PMWaitQC::init(hsc);
        role = CANDIDATE;
        proposer = 0;
    }

    ReplicaID get_proposer() override {
        return proposer;
    }

    promise_t beat() override {
        if (proposer == hsc->get_id())
        {
            promise_t pm;
            pending_beats.push(pm);
            proposer_schedule_next();
            return std::move(pm);
        }
        else
            return promise_t([](promise_t &pm) {
                pm.resolve(proposer);
            });
    }

    promise_t next_proposer(ReplicaID last_proposer) override {
        return promise_t([last_proposer](promise_t &pm) {
            pm.resolve(proposer);
        });
    }
};

}

#endif
