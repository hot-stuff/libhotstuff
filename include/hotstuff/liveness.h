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
    virtual void init(HotStuffCore *_hsc) { hsc = _hsc; }
    /** Get a promise resolved when the pace maker thinks it is a *good* time
     * to issue new commands. When promise is resolved, the replica should
     * propose the command. */
    virtual promise_t beat() = 0;
    virtual ReplicaID get_proposer() = 0;
    /** Get a promise resolved when the pace maker thinks it is a *good* time
     * to vote for a block. The promise is resolved with the next proposer's ID
     * */
    virtual promise_t next_proposer(ReplicaID last_proposer) = 0;
};

using pacemaker_bt = BoxObj<PaceMaker>;

/** A pace maker that waits for the qc of the last proposed block. */
class PaceMakerDummy: public PaceMaker {
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
    PaceMakerDummy() = default;

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

class PaceMakerDummyFixed: public PaceMakerDummy {
    ReplicaID proposer;

    public:
    ReplicaID get_proposer() override {
        return proposer;
    }

    PaceMakerDummyFixed(ReplicaID proposer): proposer(proposer) {}

    promise_t next_proposer(ReplicaID) override {
        return promise_t([this](promise_t &pm) {
            pm.resolve(proposer);
        });
    }
};

}

#endif
