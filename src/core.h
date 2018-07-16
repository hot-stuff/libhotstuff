#ifndef _HOTSTUFF_CORE_H
#define _HOTSTUFF_CORE_H

#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "salticidae/stream.h"
#include "salticidae/util.h"
#include "salticidae/network.h"
#include "salticidae/msg.h"

#include "promise.hpp"
#include "util.h"
#include "entity.h"
#include "crypto.h"

using salticidae::EventContext;
using salticidae::Event;
using salticidae::NetAddr;
using salticidae::MsgNetwork;
using salticidae::PeerNetwork;
using salticidae::ElapsedTime;
using salticidae::_1;
using salticidae::_2;

namespace hotstuff {

const double ent_waiting_timeout = 10;
const double double_inf = 1e10;

enum {
    PROPOSE = 0x0,
    VOTE = 0x1,
    QUERY_FETCH_BLK = 0x2,
    RESP_FETCH_BLK = 0x3,
};

using promise::promise_t;

struct Proposal;
struct Vote;

/** Abstraction for HotStuff protocol state machine (without network implementation). */
class HotStuffCore {
    block_t b0;                                  /** the genesis block */
    /* === state variables === */
    /** block containing the QC for the highest block having one */
    block_t bqc;
    block_t bexec;                            /**< last executed block */
    uint32_t vheight;          /**< height of the block last voted for */
    /* === auxilliary variables === */
    privkey_bt priv_key;            /**< private key for signing votes */
    std::set<block_t, BlockHeightCmp> tails;   /**< set of tail blocks */
    ReplicaConfig config;                   /**< replica configuration */
    /* === async event queues === */
    std::unordered_map<block_t, promise_t> qc_waiting;
    promise_t propose_waiting;

    block_t sanity_check_delivered(const uint256_t &blk_hash);
    void sanity_check_delivered(const block_t &blk);
    void check_commit(const block_t &_bqc);
    bool update(const uint256_t &bqc_hash);
    void on_qc_finish(const block_t &blk);
    void on_propose_(const block_t &blk);

    protected:
    ReplicaID id;                  /**< identity of the replica itself */
    const int32_t parent_limit;         /**< maximum number of parents */

    public:
    BoxObj<EntityStorage> storage;

    HotStuffCore(ReplicaID id,
                privkey_bt &&priv_key,
                int32_t parent_limit);
    virtual ~HotStuffCore() = default;

    /* Inputs of the state machine triggered by external events, should called
     * by the class user, with proper invariants. */

    /** Call to initialize the protocol, should be called once before all other
     * functions. */
    void on_init(uint32_t nfaulty) { config.nmajority = 2 * nfaulty + 1; }

    /** Call to deliver a block.
     * A block is only delivered if itself is fetched, the block for the
     * contained qc is fetched and all parents are delivered. The user should
     * always ensure this invariant. The invalid blocks will be dropped by this
     * function.
     * @return true if valid */
    bool on_deliver_blk(const block_t &blk);

    /** Call upon the delivery of a proposal message.
     * The block mentioned in the message should be already delivered. */
    void on_receive_proposal(const Proposal &prop);

    /** Call upon the delivery of a vote message.
     * The block mentioned in the message should be already delivered. */
    void on_receive_vote(const Vote &vote);

    /** Call to submit new commands to be decided (executed). */
    void on_propose(const std::vector<command_t> &cmds);

    /* Functions required to construct concrete instances for abstract classes.
     * */

    /* Outputs of the state machine triggering external events.  The virtual
     * functions should be implemented by the user to specify the behavior upon
     * the events. */
    protected:
    /** Called by HotStuffCore upon the decision being made for cmd. */
    virtual void do_decide(const command_t &cmd) = 0;
    /** Called by HotStuffCore upon broadcasting a new proposal.
     * The user should send the proposal message to all replicas except for
     * itself. */
    virtual void do_broadcast_proposal(const Proposal &prop) = 0;
    /** Called upon sending out a new vote to the next proposer.  The user
     * should send the vote message to a *good* proposer to have good liveness,
     * while safety is always guaranteed by HotStuffCore. */
    virtual void do_vote(ReplicaID last_proposer, const Vote &vote) = 0;

    /* The user plugs in the detailed instances for those
     * polymorphic data types. */
    public:
    /** Create a partial certificate that proves the vote for a block. */
    virtual part_cert_bt create_part_cert(const PrivKey &priv_key, const uint256_t &blk_hash) = 0;
    /** Create a partial certificate from its seralized form. */
    virtual part_cert_bt parse_part_cert(DataStream &s) = 0;
    /** Create a quorum certificate that proves 2f+1 votes for a block. */
    virtual quorum_cert_bt create_quorum_cert(const uint256_t &blk_hash) = 0;
    /** Create a quorum certificate from its serialized form. */
    virtual quorum_cert_bt parse_quorum_cert(DataStream &s) = 0;
    /** Create a command object from its serialized form. */
    virtual command_t parse_cmd(DataStream &s) = 0;

    public:
    /** Add a replica to the current configuration. This should only be called
     * before running HotStuffCore protocol. */
    void add_replica(ReplicaID rid, const NetAddr &addr, pubkey_bt &&pub_key);
    /** Try to prune blocks lower than last committed height - staleness. */
    void prune(uint32_t staleness);

    /* PaceMaker can use these functions to monitor the core protocol state
     * transition */
    /** Get a promise resolved when the block gets a QC. */
    promise_t async_qc_finish(const block_t &blk);
    /** Get a promise resolved when a new block is proposed. */
    promise_t async_wait_propose();

    /* Other useful functions */
    block_t get_genesis() { return b0; }
    const ReplicaConfig &get_config() { return config; }
    int8_t get_cmd_decision(const uint256_t &cmd_hash);
    ReplicaID get_id() { return id; }
    operator std::string () const;
};

/** Abstraction for proposal messages. */
struct Proposal: public Serializable {
    ReplicaID proposer;
    /** hash for the block containing the highest QC */
    uint256_t bqc_hash;
    /** block being proposed */
    block_t blk;

    /** handle of the core object to allow polymorphism. The user should use
     * a pointer to the object of the class derived from HotStuffCore */
    HotStuffCore *hsc;

    Proposal(HotStuffCore *hsc): blk(nullptr), hsc(hsc) {}
    Proposal(ReplicaID proposer,
            const uint256_t &bqc_hash,
            block_t &blk,
            HotStuffCore *hsc):
        proposer(proposer),
        bqc_hash(bqc_hash),
        blk(blk), hsc(hsc) {}

    void serialize(DataStream &s) const override {
        s << proposer
          << bqc_hash
          << *blk;
    }

    void unserialize(DataStream &s) override {
        assert(hsc != nullptr);
        s >> proposer
          >> bqc_hash;
        Block _blk;
        _blk.unserialize(s, hsc);
        blk = hsc->storage->add_blk(std::move(_blk));
    }

    operator std::string () const {
        DataStream s;
        s << "<proposal "
          << "rid=" << std::to_string(proposer) << " "
          << "bqc=" << get_hex10(bqc_hash) << " "
          << "blk=" << get_hex10(blk->get_hash()) << ">";
        return std::string(std::move(s));
    }
};

/** Abstraction for vote messages. */
struct Vote: public Serializable {
    ReplicaID voter;
    /** hash for the block containing the highest QC */
    uint256_t bqc_hash;
    /** block being voted */
    uint256_t blk_hash;
    /** proof of validity for the vote (nullptr for a negative vote) */
    part_cert_bt cert;
    
    /** handle of the core object to allow polymorphism */
    HotStuffCore *hsc;

    Vote(HotStuffCore *hsc): cert(nullptr), hsc(hsc) {}
    Vote(ReplicaID voter,
        const uint256_t &bqc_hash,
        const uint256_t &blk_hash,
        part_cert_bt &&cert,
        HotStuffCore *hsc):
        voter(voter),
        bqc_hash(bqc_hash),
        blk_hash(blk_hash),
        cert(std::move(cert)), hsc(hsc) {}

    Vote(const Vote &other):
        voter(other.voter),
        bqc_hash(other.bqc_hash),
        blk_hash(other.blk_hash),
        cert(other.cert->clone()),
        hsc(other.hsc) {}

    Vote(Vote &&other) = default;
    
    void serialize(DataStream &s) const override {
        s << voter
          << bqc_hash
          << blk_hash;
        if (cert == nullptr)
            s << (uint8_t)0;
        else
            s << (uint8_t)1 << *cert;
    }

    void unserialize(DataStream &s) override {
        assert(hsc != nullptr);
        uint8_t has_cert;
        s >> voter
          >> bqc_hash
          >> blk_hash
          >> has_cert;
        cert = has_cert ? hsc->parse_part_cert(s) : nullptr;
    }

    bool verify() const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(voter)) &&
                cert->get_blk_hash() == blk_hash;
    }

    operator std::string () const {
        DataStream s;
        s << "<vote "
          << "rid=" << std::to_string(voter) << " "
          << "bqc=" << get_hex10(bqc_hash) << " "
          << "blk=" << get_hex10(blk_hash) << " "
          << "cert=" << (cert ? "yes" : "no") << ">";
        return std::string(std::move(s));
    }
};

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

/** Network message format for HotStuff. */
struct MsgHotStuff: public salticidae::MsgBase<> {
    using MsgBase::MsgBase;
    void gen_propose(const Proposal &);
    void parse_propose(Proposal &) const;

    void gen_vote(const Vote &);
    void parse_vote(Vote &) const;

    void gen_qfetchblk(const std::vector<uint256_t> &blk_hashes);
    void parse_qfetchblk(std::vector<uint256_t> &blk_hashes) const;

    void gen_rfetchblk(const std::vector<block_t> &blks);
    void parse_rfetchblk(std::vector<block_t> &blks, HotStuffCore *hsc) const;
};

using promise::promise_t;

class HotStuffBase;

template<EntityType ent_type>
class FetchContext: public promise_t {
    Event timeout;
    HotStuffBase *hs;
    MsgHotStuff fetch_msg;
    const uint256_t ent_hash;
    std::unordered_set<NetAddr> replica_ids;
    inline void timeout_cb(evutil_socket_t, short);
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
    using conn_t = MsgNetwork<MsgHotStuff>::conn_t;

    friend BlockFetchContext;
    friend CmdFetchContext;

    protected:
    /** the binding address in replica network */
    NetAddr listen_addr;
    /** the block size */
    size_t blk_size;
    /** libevent handle */
    EventContext eb;
    pacemaker_bt pmaker;

    private:
    /** whether libevent handle is owned by itself */
    bool eb_loop;
    /** network stack */
    PeerNetwork<MsgHotStuff> pn;
#ifdef HOTSTUFF_ENABLE_BLK_PROFILE
    BlockProfiler blk_profiler;
#endif
    /* queues for async tasks */
    std::unordered_map<const uint256_t, BlockFetchContext> blk_fetch_waiting;
    std::unordered_map<const uint256_t, BlockDeliveryContext> blk_delivery_waiting;
    std::unordered_map<const uint256_t, CmdFetchContext> cmd_fetch_waiting;
    std::unordered_map<const uint256_t, promise_t> decision_waiting;
    std::queue<command_t> cmd_pending;

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
    inline void propose_handler(const MsgHotStuff &, conn_t);
    /** deliver consensus message: <vote> */
    inline void vote_handler(const MsgHotStuff &, conn_t);
    /** fetches full block data */
    inline void query_fetch_blk_handler(const MsgHotStuff &, conn_t);
    /** receives a block */
    inline void resp_fetch_blk_handler(const MsgHotStuff &, conn_t);

    void do_broadcast_proposal(const Proposal &) override;
    void do_vote(ReplicaID, const Vote &) override;
    void do_decide(const command_t &) override;

    public:
    HotStuffBase(uint32_t blk_size,
            int32_t parent_limit,
            ReplicaID rid,
            privkey_bt &&priv_key,
            NetAddr listen_addr,
            EventContext eb = EventContext(),
            pacemaker_bt pmaker = nullptr);

    ~HotStuffBase();

    /* the API for HotStuffBase */

    /* Submit the command to be decided. */
    void add_command(command_t cmd) {
        cmd_pending.push(storage->add_cmd(cmd));
        if (cmd_pending.size() >= blk_size)
        {
            std::vector<command_t> cmds;
            for (uint32_t i = 0; i < blk_size; i++)
            {
                cmds.push_back(cmd_pending.front());
                cmd_pending.pop();
            }
            pmaker->beat().then([this, cmds = std::move(cmds)]() {
                on_propose(cmds);
            });
        }
    }

    void add_replica(ReplicaID idx, const NetAddr &addr, pubkey_bt &&pub_key);
    void start(bool eb_loop = false);

    size_t size() const { return pn.all_peers().size(); }
    void print_stat() const;

    /* Helper functions */
    /** Returns a promise resolved (with command_t cmd) when Command is fetched. */
    promise_t async_fetch_cmd(const uint256_t &cmd_hash, const NetAddr *replica_id, bool fetch_now = true);
    /** Returns a promise resolved (with block_t blk) when Block is fetched. */
    promise_t async_fetch_blk(const uint256_t &blk_hash, const NetAddr *replica_id, bool fetch_now = true);
    /** Returns a promise resolved (with block_t blk) when Block is delivered (i.e. prefix is fetched). */
    promise_t async_deliver_blk(const uint256_t &blk_hash,  const NetAddr &replica_id);
    /** Returns a promise resolved (with command_t cmd) when Command is decided. */
    promise_t async_decide(const uint256_t &cmd_hash);
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
            int32_t parent_limit,
            ReplicaID rid,
            const bytearray_t &raw_privkey,
            NetAddr listen_addr,
            EventContext eb = nullptr):
        HotStuffBase(blk_size,
                    parent_limit,
                    rid,
                    new PrivKeyType(raw_privkey),
                    listen_addr,
                    eb) {}

    void add_replica(ReplicaID idx, const NetAddr &addr, const bytearray_t &pubkey_raw) {
        DataStream s(pubkey_raw);
        HotStuffBase::add_replica(idx, addr, new PubKeyType(pubkey_raw));
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
    timeout = Event(hs->eb, -1, 0,
            std::bind(&FetchContext::timeout_cb, this, _1, _2));
    reset_timeout();
}

template<>
inline void FetchContext<ENT_TYPE_CMD>::timeout_cb(evutil_socket_t, short) {
    HOTSTUFF_LOG_WARN("cmd fetching %.10s timeout", get_hex(ent_hash).c_str());
    for (const auto &replica_id: replica_ids)
        send(replica_id);
    reset_timeout();
}

template<>
inline void FetchContext<ENT_TYPE_BLK>::timeout_cb(evutil_socket_t, short) {
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
    fetch_msg.gen_qfetchblk(std::vector<uint256_t>{ent_hash});

    timeout = Event(hs->eb, -1, 0,
            std::bind(&FetchContext::timeout_cb, this, _1, _2));
    reset_timeout();
}

template<EntityType ent_type>
void FetchContext<ent_type>::send(const NetAddr &replica_id) {
    hs->part_fetched_replica[replica_id]++;
    hs->pn.send_msg(fetch_msg, replica_id);
}

template<EntityType ent_type>
void FetchContext<ent_type>::reset_timeout() {
    timeout.add_with_timeout(salticidae::gen_rand_timeout(ent_waiting_timeout));
}

template<EntityType ent_type>
void FetchContext<ent_type>::add_replica(const NetAddr &replica_id, bool fetch_now) {
    if (replica_ids.empty() && fetch_now)
        send(replica_id);
    replica_ids.insert(replica_id);
}

}

#endif
