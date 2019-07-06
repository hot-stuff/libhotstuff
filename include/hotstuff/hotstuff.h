/**
 * Copyright 2018 VMware
 * Copyright 2018 Ted Yin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _HOTSTUFF_CORE_H
#define _HOTSTUFF_CORE_H

#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "salticidae/util.h"
#include "salticidae/network.h"
#include "salticidae/msg.h"
#include "hotstuff/util.h"
#include "hotstuff/consensus.h"

namespace hotstuff {

using salticidae::PeerNetwork;
using salticidae::ElapsedTime;
using salticidae::_1;
using salticidae::_2;

const double ent_waiting_timeout = 10;
const double double_inf = 1e10;

/** Network message format for HotStuff. */
struct MsgPropose {
    static const opcode_t opcode = 0x0;
    DataStream serialized;
    Proposal proposal;
    MsgPropose(const Proposal &);
    /** Only move the data to serialized, do not parse immediately. */
    MsgPropose(DataStream &&s): serialized(std::move(s)) {}
    /** Parse the serialized data to blks now, with `hsc->storage`. */
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgVote {
    static const opcode_t opcode = 0x1;
    DataStream serialized;
    Vote vote;
    MsgVote(const Vote &);
    MsgVote(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgReqBlock {
    static const opcode_t opcode = 0x2;
    DataStream serialized;
    std::vector<uint256_t> blk_hashes;
    MsgReqBlock() = default;
    MsgReqBlock(const std::vector<uint256_t> &blk_hashes);
    MsgReqBlock(DataStream &&s);
};


struct MsgRespBlock {
    static const opcode_t opcode = 0x3;
    DataStream serialized;
    std::vector<block_t> blks;
    MsgRespBlock(const std::vector<block_t> &blks);
    MsgRespBlock(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

using promise::promise_t;

class HotStuffBase;
using pacemaker_bt = BoxObj<class PaceMaker>;

template<EntityType ent_type>
class FetchContext: public promise_t {
    TimerEvent timeout;
    HotStuffBase *hs;
    MsgReqBlock fetch_msg;
    const uint256_t ent_hash;
    std::unordered_set<NetAddr> replica_ids;
    inline void timeout_cb(TimerEvent &);
    public:
    FetchContext(const FetchContext &) = delete;
    FetchContext &operator=(const FetchContext &) = delete;
    FetchContext(FetchContext &&other);

    FetchContext(const uint256_t &ent_hash, HotStuffBase *hs);
    ~FetchContext() {}

    inline void send(const NetAddr &replica_id);
    inline void reset_timeout();
    inline void add_replica(const NetAddr &replica_id, bool fetch_now = true);
};

class BlockDeliveryContext: public promise_t {
    public:
    ElapsedTime elapsed;
    BlockDeliveryContext &operator=(const BlockDeliveryContext &) = delete;
    BlockDeliveryContext(const BlockDeliveryContext &other):
        promise_t(static_cast<const promise_t &>(other)),
        elapsed(other.elapsed) {}
    BlockDeliveryContext(BlockDeliveryContext &&other):
        promise_t(static_cast<const promise_t &>(other)),
        elapsed(std::move(other.elapsed)) {}
    template<typename Func>
    BlockDeliveryContext(Func callback): promise_t(callback) {
        elapsed.start();
    }
};


/** HotStuff protocol (with network implementation). */
class HotStuffBase: public HotStuffCore {
    using BlockFetchContext = FetchContext<ENT_TYPE_BLK>;
    using CmdFetchContext = FetchContext<ENT_TYPE_CMD>;

    friend BlockFetchContext;
    friend CmdFetchContext;

    public:
    using Net = PeerNetwork<opcode_t>;
    using commit_cb_t = std::function<void(const Finality &)>;

    protected:
    /** the binding address in replica network */
    NetAddr listen_addr;
    /** the block size */
    size_t blk_size;
    /** libevent handle */
    EventContext ec;
    salticidae::ThreadCall tcall;
    VeriPool vpool;
    std::vector<NetAddr> peers;

    private:
    /** whether libevent handle is owned by itself */
    bool ec_loop;
    /** network stack */
    Net pn;
    std::unordered_set<uint256_t> valid_tls_certs;
#ifdef HOTSTUFF_BLK_PROFILE
    BlockProfiler blk_profiler;
#endif
    pacemaker_bt pmaker;
    /* queues for async tasks */
    std::unordered_map<const uint256_t, BlockFetchContext> blk_fetch_waiting;
    std::unordered_map<const uint256_t, BlockDeliveryContext> blk_delivery_waiting;
    std::unordered_map<const uint256_t, commit_cb_t> decision_waiting;
    using cmd_queue_t = salticidae::MPSCQueueEventDriven<std::pair<uint256_t, commit_cb_t>>;
    cmd_queue_t cmd_pending;
    std::queue<uint256_t> cmd_pending_buffer;

    /* statistics */
    uint64_t fetched;
    uint64_t delivered;
    mutable uint64_t nsent;
    mutable uint64_t nrecv;

    mutable uint32_t part_parent_size;
    mutable uint32_t part_fetched;
    mutable uint32_t part_delivered;
    mutable uint32_t part_decided;
    mutable uint32_t part_gened;
    mutable double part_delivery_time;
    mutable double part_delivery_time_min;
    mutable double part_delivery_time_max;
    mutable std::unordered_map<const NetAddr, uint32_t> part_fetched_replica;

    void on_fetch_cmd(const command_t &cmd);
    void on_fetch_blk(const block_t &blk);
    void on_deliver_blk(const block_t &blk);

    /** deliver consensus message: <propose> */
    inline void propose_handler(MsgPropose &&, const Net::conn_t &);
    /** deliver consensus message: <vote> */
    inline void vote_handler(MsgVote &&, const Net::conn_t &);
    /** fetches full block data */
    inline void req_blk_handler(MsgReqBlock &&, const Net::conn_t &);
    /** receives a block */
    inline void resp_blk_handler(MsgRespBlock &&, const Net::conn_t &);

    inline bool conn_handler(const salticidae::ConnPool::conn_t &, bool);

    void do_broadcast_proposal(const Proposal &) override;
    void do_vote(ReplicaID, const Vote &) override;
    void do_decide(Finality &&) override;
    void do_consensus(const block_t &blk) override;

    protected:

    /** Called to replicate the execution of a command, the application should
     * implement this to make transition for the application state. */
    virtual void state_machine_execute(const Finality &) = 0;

    public:
    HotStuffBase(uint32_t blk_size,
            ReplicaID rid,
            privkey_bt &&priv_key,
            NetAddr listen_addr,
            pacemaker_bt pmaker,
            EventContext ec,
            size_t nworker,
            const Net::Config &netconfig);

    ~HotStuffBase();

    /* the API for HotStuffBase */

    /* Submit the command to be decided. */
    void exec_command(uint256_t cmd_hash, commit_cb_t callback);
    void start(std::vector<std::tuple<NetAddr, pubkey_bt, uint256_t>> &&replicas,
                bool ec_loop = false);

    size_t size() const { return peers.size(); }
    const auto &get_decision_waiting() const { return decision_waiting; }
    ThreadCall &get_tcall() { return tcall; }
    PaceMaker *get_pace_maker() { return pmaker.get(); }
    void print_stat() const;
    virtual void do_elected() {}
//#ifdef HOTSTUFF_AUTOCLI
//    virtual void do_demand_commands(size_t) {}
//#endif

    /* Helper functions */
    /** Returns a promise resolved (with command_t cmd) when Command is fetched. */
    promise_t async_fetch_cmd(const uint256_t &cmd_hash, const NetAddr *replica_id, bool fetch_now = true);
    /** Returns a promise resolved (with block_t blk) when Block is fetched. */
    promise_t async_fetch_blk(const uint256_t &blk_hash, const NetAddr *replica_id, bool fetch_now = true);
    /** Returns a promise resolved (with block_t blk) when Block is delivered (i.e. prefix is fetched). */
    promise_t async_deliver_blk(const uint256_t &blk_hash,  const NetAddr &replica_id);
};

/** HotStuff protocol (templated by cryptographic implementation). */
template<typename PrivKeyType = PrivKeyDummy,
        typename PubKeyType = PubKeyDummy,
        typename PartCertType = PartCertDummy,
        typename QuorumCertType = QuorumCertDummy>
class HotStuff: public HotStuffBase {
    using HotStuffBase::HotStuffBase;
    protected:

    part_cert_bt create_part_cert(const PrivKey &priv_key, const uint256_t &blk_hash) override {
        HOTSTUFF_LOG_DEBUG("create part cert with priv=%s, blk_hash=%s",
                            get_hex10(priv_key).c_str(), get_hex10(blk_hash).c_str());
        return new PartCertType(
                    static_cast<const PrivKeyType &>(priv_key),
                    blk_hash);
    }

    part_cert_bt parse_part_cert(DataStream &s) override {
        PartCert *pc = new PartCertType();
        s >> *pc;
        return pc;
    }

    quorum_cert_bt create_quorum_cert(const uint256_t &blk_hash) override {
        return new QuorumCertType(get_config(), blk_hash);
    }

    quorum_cert_bt parse_quorum_cert(DataStream &s) override {
        QuorumCert *qc = new QuorumCertType();
        s >> *qc;
        return qc;
    }

    public:
    HotStuff(uint32_t blk_size,
            ReplicaID rid,
            const bytearray_t &raw_privkey,
            NetAddr listen_addr,
            pacemaker_bt pmaker,
            EventContext ec = EventContext(),
            size_t nworker = 4,
            const Net::Config &netconfig = Net::Config()):
        HotStuffBase(blk_size,
                    rid,
                    new PrivKeyType(raw_privkey),
                    listen_addr,
                    std::move(pmaker),
                    ec,
                    nworker,
                    netconfig) {}

    void start(const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> &replicas, bool ec_loop = false) {
        std::vector<std::tuple<NetAddr, pubkey_bt, uint256_t>> reps;
        for (auto &r: replicas)
            reps.push_back(
                std::make_tuple(
                    std::get<0>(r),
                    new PubKeyType(std::get<1>(r)),
                    uint256_t(std::get<2>(r))
                ));
        HotStuffBase::start(std::move(reps), ec_loop);
    }
};

using HotStuffNoSig = HotStuff<>;
using HotStuffSecp256k1 = HotStuff<PrivKeySecp256k1, PubKeySecp256k1,
                                    PartCertSecp256k1, QuorumCertSecp256k1>;

template<EntityType ent_type>
FetchContext<ent_type>::FetchContext(FetchContext && other):
        promise_t(static_cast<const promise_t &>(other)),
        hs(other.hs),
        fetch_msg(std::move(other.fetch_msg)),
        ent_hash(other.ent_hash),
        replica_ids(std::move(other.replica_ids)) {
    other.timeout.del();
    timeout = TimerEvent(hs->ec,
            std::bind(&FetchContext::timeout_cb, this, _1));
    reset_timeout();
}

template<>
inline void FetchContext<ENT_TYPE_CMD>::timeout_cb(TimerEvent &) {
    HOTSTUFF_LOG_WARN("cmd fetching %.10s timeout", get_hex(ent_hash).c_str());
    for (const auto &replica_id: replica_ids)
        send(replica_id);
    reset_timeout();
}

template<>
inline void FetchContext<ENT_TYPE_BLK>::timeout_cb(TimerEvent &) {
    HOTSTUFF_LOG_WARN("block fetching %.10s timeout", get_hex(ent_hash).c_str());
    for (const auto &replica_id: replica_ids)
        send(replica_id);
    reset_timeout();
}

template<EntityType ent_type>
FetchContext<ent_type>::FetchContext(
                                const uint256_t &ent_hash, HotStuffBase *hs):
            promise_t([](promise_t){}),
            hs(hs), ent_hash(ent_hash) {
    fetch_msg = std::vector<uint256_t>{ent_hash};

    timeout = TimerEvent(hs->ec,
            std::bind(&FetchContext::timeout_cb, this, _1));
    reset_timeout();
}

template<EntityType ent_type>
void FetchContext<ent_type>::send(const NetAddr &replica_id) {
    hs->part_fetched_replica[replica_id]++;
    hs->pn.send_msg(fetch_msg, replica_id);
}

template<EntityType ent_type>
void FetchContext<ent_type>::reset_timeout() {
    timeout.add(salticidae::gen_rand_timeout(ent_waiting_timeout));
}

template<EntityType ent_type>
void FetchContext<ent_type>::add_replica(const NetAddr &replica_id, bool fetch_now) {
    if (replica_ids.empty() && fetch_now)
        send(replica_id);
    replica_ids.insert(replica_id);
}

}

#endif
