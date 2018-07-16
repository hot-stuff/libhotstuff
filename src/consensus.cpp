#include <cassert>
#include <stack>

#include "hotstuff/util.h"
#include "hotstuff/consensus.h"

#define LOG_INFO HOTSTUFF_LOG_INFO
#define LOG_DEBUG HOTSTUFF_LOG_DEBUG
#define LOG_WARN HOTSTUFF_LOG_WARN

namespace hotstuff {

/* The core logic of HotStuff, is farily simple :) */
/*** begin HotStuff protocol logic ***/
HotStuffCore::HotStuffCore(ReplicaID id,
                            privkey_bt &&priv_key,
                            int32_t parent_limit):
        b0(new Block(true, 1)),
        bqc(b0),
        bexec(b0),
        vheight(0),
        priv_key(std::move(priv_key)),
        tails{bqc},
        id(id),
        parent_limit(parent_limit),
        storage(new EntityStorage()) {
    storage->add_blk(b0);
    b0->qc_ref = b0;
}

void HotStuffCore::sanity_check_delivered(const block_t &blk) {
    if (!blk->delivered)
        throw std::runtime_error("block not delivered");
}

block_t HotStuffCore::sanity_check_delivered(const uint256_t &blk_hash) {
    block_t blk = storage->find_blk(blk_hash);
    if (blk == nullptr || !blk->delivered)
        throw std::runtime_error("block not delivered");
    return std::move(blk);
}

bool HotStuffCore::on_deliver_blk(const block_t &blk) {
    if (blk->delivered)
    {
        LOG_WARN("attempt to deliver a block twice");
        return false;
    }
    blk->parents.clear();
    for (const auto &hash: blk->parent_hashes)
    {
        block_t p = sanity_check_delivered(hash);
        blk->parents.push_back(p);
    }
    blk->height = blk->parents[0]->height + 1;
    for (const auto &cmd: blk->cmds)
        cmd->container = blk;

    if (blk->qc)
    {
        block_t _blk = storage->find_blk(blk->qc->get_blk_hash());
        if (_blk == nullptr)
            throw std::runtime_error("block referred by qc not fetched");
        blk->qc_ref = std::move(_blk);
    } // otherwise blk->qc_ref remains null

    for (auto pblk: blk->parents) tails.erase(pblk);
    tails.insert(blk);

    blk->delivered = true;
    LOG_DEBUG("delivered %.10s", get_hex(blk->get_hash()).c_str());
    return true;
}

void HotStuffCore::check_commit(const block_t &_blk) {
    const block_t &blk = _blk->qc_ref;
    if (blk->qc_ref == nullptr) return;
    if (blk->decision) return;
    block_t p = blk->parents[0];
    if (p == blk->qc_ref)
    { /* commit */
        std::vector<block_t> commit_queue;
        block_t b;
        for (b = p; b->height > bexec->height; b = b->parents[0])
        { /* todo: also commit the uncles/aunts */
            commit_queue.push_back(b);
        }
        if (b != bexec)
            throw std::runtime_error("safety breached :(");
        for (auto it = commit_queue.rbegin(); it != commit_queue.rend(); it++)
        {
            const block_t &blk = *it;
            blk->decision = 1;
#ifdef HOTSTUFF_ENABLE_LOG_PROTO
            LOG_INFO("commit blk %.10s", get_hex10(blk->get_hash()).c_str());
#endif
            for (auto cmd: blk->cmds)
                do_decide(cmd);
        }
        bexec = p;
    }
}

bool HotStuffCore::update(const uint256_t &bqc_hash) {
    block_t _bqc = sanity_check_delivered(bqc_hash);
    if (_bqc->qc_ref == nullptr) return false;
    check_commit(_bqc);
    if (_bqc->qc_ref->height > bqc->qc_ref->height)
        bqc = _bqc;
    return true;
}

void HotStuffCore::on_propose(const std::vector<command_t> &cmds) {
    size_t nparents = parent_limit < 1 ? tails.size() : parent_limit;
    assert(tails.size() > 0);
    block_t p = *tails.rbegin();
    std::vector<block_t> parents{p};
    tails.erase(p);
    nparents--;
    /* add the rest of tails as "uncles/aunts" */
    while (nparents--)
    {
        auto it = tails.begin();
        parents.push_back(*it);
        tails.erase(it);
    }
    quorum_cert_bt qc = nullptr;
    block_t qc_ref = nullptr;
    if (p != b0 && p->voted.size() >= config.nmajority)
    {
        qc = p->self_qc->clone();
        qc->compute();
        qc_ref = p;
    }
    /* create a new block */
    block_t bnew = storage->add_blk(
        Block(
            parents,
            cmds,
            p->height + 1,
            std::move(qc), qc_ref,
            nullptr
        ));
    const uint256_t bnew_hash = bnew->get_hash();
    bnew->self_qc = create_quorum_cert(bnew_hash);
    on_deliver_blk(bnew);
    update(bnew_hash);
    Proposal prop(id, bqc->get_hash(), bnew, nullptr);
#ifdef HOTSTUFF_ENABLE_LOG_PROTO
    LOG_INFO("propose %s", std::string(*bnew).c_str());
#endif
    /* self-vote */
    on_receive_vote(
        Vote(id, bqc->get_hash(), bnew_hash,
            create_part_cert(*priv_key, bnew_hash), this));
    on_propose_(bnew);
    /* boradcast to other replicas */
    do_broadcast_proposal(prop);
}

void HotStuffCore::on_receive_proposal(const Proposal &prop) {
    if (!update(prop.bqc_hash)) return;
#ifdef HOTSTUFF_ENABLE_LOG_PROTO
    LOG_INFO("got %s", std::string(prop).c_str());
#endif
    block_t bnew = prop.blk;
    sanity_check_delivered(bnew);
    bool opinion = false;
    if (bnew->height > vheight)
    {
        block_t pref = bqc->qc_ref;
        block_t b;
        for (b = bnew;
            b->height > pref->height;
            b = b->parents[0]);
        opinion = b == pref;
        vheight = bnew->height;
    }
#ifdef HOTSTUFF_ENABLE_LOG_PROTO
    LOG_INFO("now state: %s", std::string(*this).c_str());
#endif
    do_vote(prop.proposer,
        Vote(id,
            bqc->get_hash(),
            bnew->get_hash(),
            (opinion ?
                create_part_cert(*priv_key, bnew->get_hash()) :
                nullptr),
            nullptr));
}

void HotStuffCore::on_receive_vote(const Vote &vote) {
    if (!update(vote.bqc_hash)) return;
#ifdef HOTSTUFF_ENABLE_LOG_PROTO
    LOG_INFO("got %s", std::string(vote).c_str());
    LOG_INFO("now state: %s", std::string(*this).c_str());
#endif

    block_t blk = sanity_check_delivered(vote.blk_hash);
    if (vote.cert == nullptr) return;
    if (!vote.verify())
    {
        LOG_WARN("invalid vote");
        return;
    }
    if (!blk->voted.insert(vote.voter).second)
    {
        LOG_WARN("duplicate votes");
        return;
    }
    size_t qsize = blk->voted.size();
    if (qsize <= config.nmajority)
    {
        blk->self_qc->add_part(vote.voter, *vote.cert);
        if (qsize == config.nmajority)
            on_qc_finish(blk);
    }
}
/*** end HotStuff protocol logic ***/

void HotStuffCore::prune(uint32_t staleness) {
    block_t start;
    /* skip the blocks */
    for (start = bexec; staleness; staleness--, start = start->parents[0])
        if (!start->parents.size()) return;
    std::stack<block_t> s;
    start->qc_ref = nullptr;
    s.push(start);
    while (!s.empty())
    {
        auto &blk = s.top();
        if (blk->parents.empty())
        {
            storage->try_release_blk(blk);
            s.pop();
            continue;
        }
        blk->qc_ref = nullptr;
        s.push(blk->parents.back());
        blk->parents.pop_back();
    }
}

int8_t HotStuffCore::get_cmd_decision(const uint256_t &cmd_hash) {
    auto cmd = storage->find_cmd(cmd_hash);
    return cmd != nullptr ? cmd->get_decision() : 0;
}

void HotStuffCore::add_replica(ReplicaID rid, const NetAddr &addr,
                                pubkey_bt &&pub_key) {
    config.add_replica(rid, 
            ReplicaInfo(rid, addr, std::move(pub_key)));
    b0->voted.insert(rid);
}

promise_t HotStuffCore::async_qc_finish(const block_t &blk) {
    if (blk->voted.size() >= config.nmajority)
        return promise_t([](promise_t &pm) {
            pm.resolve();
        });
    auto it = qc_waiting.find(blk);
    if (it == qc_waiting.end())
        it = qc_waiting.insert(std::make_pair(blk, promise_t())).first;
    return it->second;
}

void HotStuffCore::on_qc_finish(const block_t &blk) {
    auto it = qc_waiting.find(blk);
    if (it != qc_waiting.end())
    {
        it->second.resolve();
        qc_waiting.erase(it);
    }
}

promise_t HotStuffCore::async_wait_propose() {
    return propose_waiting;
}

void HotStuffCore::on_propose_(const block_t &blk) {
    auto t = std::move(propose_waiting);
    propose_waiting = promise_t();
    t.resolve(blk);
}

HotStuffCore::operator std::string () const {
    DataStream s;
    s << "<hotstuff "
      << "bqc=" << get_hex10(bqc->get_hash()) << " "
      << "bexec=" << get_hex10(bqc->get_hash()) << " "
      << "vheight=" << std::to_string(vheight) << " "
      << "tails=" << std::to_string(tails.size()) << ">";
    return std::string(std::move(s));
}

}
