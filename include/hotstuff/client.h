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
    static uint64_t cnt;
    uint64_t n;
    uint256_t hash;

    public:

    CommandDummy() {}

    ~CommandDummy() override {}

    CommandDummy(uint64_t n):
        n(n), hash(salticidae::get_hash(*this)) {}

    static command_t make_cmd() {
        return new CommandDummy(cnt++);
    }

    void serialize(DataStream &s) const override {
        s << n;
    }

    void unserialize(DataStream &s) override {
        s >> n;
        hash = salticidae::get_hash(*this);
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
