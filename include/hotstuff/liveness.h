#ifndef _HOTSTUFF_LIVENESS_H
#define _HOTSTUFF_LIVENESS_H

#include "hotstuff/consensus.h"

namespace hotstuff {

/** Abstraction for liveness gadget (oracle). */
class PaceMaker {
    public:
    virtual ~PaceMaker() = default;
    /** Get a promise resolved when the pace maker thinks it is a *good* time
     * to issue new commands. When promise is resolved with the ID of itself,
     * the replica should propose the command, otherwise it will forward the
     * command to the proposer indicated by the ID. */
    virtual promise_t beat() = 0;
    /** Get a promise resolved when the pace maker thinks it is a *good* time
     * to vote for a block. The promise is resolved with the next proposer's ID
     * */
    virtual promise_t next_proposer(ReplicaID last_proposer) = 0;
};

using pacemaker_bt = BoxObj<PaceMaker>;

/** A pace maker that waits for the qc of the last proposed block. */
class PaceMakerDummy: public PaceMaker {
    HotStuffCore *hsc;
    std::queue<promise_t> pending_beats;
    block_t last_proposed;
    bool locked;

    void schedule_next() {
        if (!pending_beats.empty() && !locked)
        {
            auto pm = pending_beats.front();
            pending_beats.pop();
            hsc->async_qc_finish(last_proposed).then(
                    [id = hsc->get_id(), pm]() {
                pm.resolve(id);
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
    PaceMakerDummy(HotStuffCore *hsc):
            hsc(hsc),
            last_proposed(hsc->get_genesis()),
            locked(false) {
        update_last_proposed();
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

}

#endif
