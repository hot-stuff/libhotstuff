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

}

#endif
