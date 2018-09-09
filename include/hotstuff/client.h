#ifndef _HOTSTUFF_CLIENT_H
#define _HOTSTUFF_CLIENT_H

#include "salticidae/msg.h"
#include "hotstuff/type.h"
#include "hotstuff/entity.h"
#include "hotstuff/consensus.h"

namespace hotstuff {

struct MsgReqCmd {
    static const opcode_t opcode = 0x4;
    DataStream serialized;
    command_t cmd;
    MsgReqCmd(const Command &cmd);
    MsgReqCmd(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgRespCmd {
    static const opcode_t opcode = 0x5;
    DataStream serialized;
    Finality fin;
    MsgRespCmd(const Finality &fin);
    MsgRespCmd(DataStream &&s);
};

class CommandDummy: public Command {
    uint32_t cid;
    uint32_t n;
    uint256_t hash;
#if HOTSTUFF_CMD_DMSIZE > 0
    uint8_t payload[HOTSTUFF_CMD_DMSIZE];
#endif
    uint256_t compute_hash() {
        DataStream s;
        s << cid << n;
        return s.get_hash();
    }

    public:
    CommandDummy() {}
    ~CommandDummy() override {}

    CommandDummy(uint32_t cid, uint32_t n):
        cid(cid), n(n), hash(compute_hash()) {}

    void serialize(DataStream &s) const override {
        s << cid << n;
#if HOTSTUFF_CMD_DMSIZE > 0
        s.put_data(payload, payload + HOTSTUFF_CMD_DMSIZE);
#endif
    }

    void unserialize(DataStream &s) override {
        s >> cid >> n;
#if HOTSTUFF_CMD_DMSIZE > 0
        auto base = s.get_data_inplace(HOTSTUFF_CMD_DMSIZE);
        memmove(payload, base, HOTSTUFF_CMD_DMSIZE);
#endif
        hash = compute_hash();
    }

    const uint256_t &get_hash() const override {
        return hash;
    }

    bool verify() const override {
        return true;
    }
};

}

#endif
