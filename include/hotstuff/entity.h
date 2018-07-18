#ifndef _HOTSTUFF_ENT_H
#define _HOTSTUFF_ENT_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstddef>
#include <ios>

#include "salticidae/netaddr.h"
#include "salticidae/ref.h"
#include "hotstuff/type.h"
#include "hotstuff/util.h"
#include "hotstuff/crypto.h"

namespace hotstuff {

enum EntityType {
    ENT_TYPE_CMD = 0x0,
    ENT_TYPE_BLK = 0x1
};

struct ReplicaInfo {
    ReplicaID id;
    salticidae::NetAddr addr;
    pubkey_bt pubkey;

    ReplicaInfo(ReplicaID id,
                const salticidae::NetAddr &addr,
                pubkey_bt &&pubkey):
        id(id), addr(addr), pubkey(std::move(pubkey)) {}

    ReplicaInfo(const ReplicaInfo &other):
        id(other.id), addr(other.addr),
        pubkey(other.pubkey->clone()) {}

    ReplicaInfo(ReplicaInfo &&other):
        id(other.id), addr(other.addr),
        pubkey(std::move(other.pubkey)) {}
};

class ReplicaConfig {
    std::unordered_map<ReplicaID, ReplicaInfo> replica_map;

    public:
    size_t nmajority;

    void add_replica(ReplicaID rid, const ReplicaInfo &info) {
        replica_map.insert(std::make_pair(rid, info));
    }

    const ReplicaInfo &get_info(ReplicaID rid) const {
        auto it = replica_map.find(rid);
        if (it == replica_map.end())
            throw HotStuffError("rid %s not found",
                    get_hex(rid).c_str());
        return it->second;
    }

    const PubKey &get_pubkey(ReplicaID rid) const {
        return *(get_info(rid).pubkey);
    }

    const salticidae::NetAddr &get_addr(ReplicaID rid) const {
        return get_info(rid).addr;
    }
};

class Block;
class HotStuffCore;

using block_t = salticidae::RcObj<Block>;
using block_weak_t = salticidae::WeakObj<Block>;

class Command: public Serializable {
    friend HotStuffCore;
    block_weak_t container;
    public:
    virtual ~Command() = default;
    virtual const uint256_t &get_hash() const = 0;
    virtual bool verify() const = 0;
    inline int8_t get_decision() const;
    block_t get_container() const {
        return container;
    }
};

using command_t = RcObj<Command>;

template<typename Hashable>
inline static std::vector<uint256_t>
get_hashes(const std::vector<Hashable> &plist) {
    std::vector<uint256_t> hashes;
    for (const auto &p: plist)
        hashes.push_back(p->get_hash());
    return std::move(hashes);
}

class Block {
    friend HotStuffCore;
    std::vector<uint256_t> parent_hashes;
    std::vector<command_t> cmds;
    quorum_cert_bt qc;
    uint256_t hash;

    std::vector<block_t> parents;
    block_t qc_ref;
    quorum_cert_bt self_qc;
    uint32_t height;
    bool delivered;
    int8_t decision;

    std::unordered_set<ReplicaID> voted;

    public:
    Block():
        qc(nullptr),
        qc_ref(nullptr),
        self_qc(nullptr), height(0),
        delivered(false), decision(0) {}

    Block(bool delivered, int8_t decision):
        qc(nullptr),
        hash(salticidae::get_hash(*this)),
        qc_ref(nullptr),
        self_qc(nullptr), height(0),
        delivered(delivered), decision(decision) {}

    Block(const std::vector<block_t> &parents,
        const std::vector<command_t> &cmds,
        uint32_t height,
        quorum_cert_bt &&qc,
        const block_t &qc_ref,
        quorum_cert_bt &&self_qc,
        int8_t decision = 0):
            parent_hashes(get_hashes(parents)),
            cmds(cmds),
            qc(std::move(qc)),
            hash(salticidae::get_hash(*this)),
            parents(parents),
            qc_ref(qc_ref),
            self_qc(std::move(self_qc)),
            height(height),
            delivered(0),
            decision(decision) {}

    void serialize(DataStream &s) const;

    void unserialize(DataStream &s, HotStuffCore *hsc);

    const std::vector<command_t> &get_cmds() const {
        return cmds;
    }

    const std::vector<block_t> &get_parents() const {
        return parents;
    }

    const std::vector<uint256_t> &get_parent_hashes() const {
        return parent_hashes;
    }

    const uint256_t &get_hash() const { return hash; }

    bool verify(const ReplicaConfig &config) const {
        if (qc && !qc->verify(config)) return false;
        for (auto cmd: cmds)
            if (!cmd->verify()) return false;
        return true;
    }

    int8_t get_decision() const { return decision; }

    bool is_delivered() const { return delivered; }

    uint32_t get_height() const { return height; }

    const quorum_cert_bt &get_qc() const { return qc; }

    operator std::string () const {
        DataStream s;
        s << "<block "
          << "id="  << get_hex10(hash) << " "
          << "height=" << std::to_string(height) << " "
          << "parent=" << get_hex10(parent_hashes[0]) << ">";
        return std::string(std::move(s));
    }
};

struct BlockHeightCmp {
    bool operator()(const block_t &a, const block_t &b) const {
        return a->get_height() < b->get_height();
    }
};

int8_t Command::get_decision() const {
    block_t cptr = container;
    return cptr ? cptr->get_decision() : 0;
}

class EntityStorage {
    std::unordered_map<const uint256_t, block_t> blk_cache;
    std::unordered_map<const uint256_t, command_t> cmd_cache;
    public:
    bool is_blk_delivered(const uint256_t &blk_hash) {
        auto it = blk_cache.find(blk_hash);
        if (it == blk_cache.end()) return false;
        return it->second->is_delivered();
    }

    bool is_blk_fetched(const uint256_t &blk_hash) {
        return blk_cache.count(blk_hash);
    }

    const block_t &add_blk(Block &&_blk) {
        block_t blk = new Block(std::move(_blk));
        return blk_cache.insert(std::make_pair(blk->get_hash(), blk)).first->second;
    }

    const block_t &add_blk(const block_t &blk) {
        return blk_cache.insert(std::make_pair(blk->get_hash(), blk)).first->second;
    }

    block_t find_blk(const uint256_t &blk_hash) {
        auto it = blk_cache.find(blk_hash);
        return it == blk_cache.end() ? nullptr : it->second;
    }

    bool is_cmd_fetched(const uint256_t &cmd_hash) {
        return cmd_cache.count(cmd_hash);
    }

    const command_t &add_cmd(const command_t &cmd) {
        return cmd_cache.insert(std::make_pair(cmd->get_hash(), cmd)).first->second;
    }

    command_t find_cmd(const uint256_t &cmd_hash) {
        auto it = cmd_cache.find(cmd_hash);
        return it == cmd_cache.end() ? nullptr: it->second;
    }

    size_t get_cmd_cache_size() {
        return cmd_cache.size();
    }
    size_t get_blk_cache_size() {
        return blk_cache.size();
    }

    bool try_release_cmd(const command_t &cmd) {
        if (cmd.get_cnt() == 2) /* only referred by cmd and the storage */
        {
            const auto &cmd_hash = cmd->get_hash();
            cmd_cache.erase(cmd_hash);
            return true;
        }
        return false;
    }

    bool try_release_blk(const block_t &blk) {
        if (blk.get_cnt() == 2) /* only referred by blk and the storage */
        {
            const auto &blk_hash = blk->get_hash();
#ifdef HOTSTUFF_ENABLE_LOG_PROTO
            HOTSTUFF_LOG_INFO("releasing blk %.10s", get_hex(blk_hash).c_str());
#endif
            for (const auto &cmd: blk->get_cmds())
                try_release_cmd(cmd);
            blk_cache.erase(blk_hash);
            return true;
        }
#ifdef HOTSTUFF_ENABLE_LOG_PROTO
        else
            HOTSTUFF_LOG_INFO("cannot release (%lu)", blk.get_cnt());
#endif
        return false;
    }
};

}

#endif
