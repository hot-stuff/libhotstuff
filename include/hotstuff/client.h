#ifndef _HOTSTUFF_CLIENT_H
#define _HOTSTUFF_CLIENT_H

#include "salticidae/msg.h"
#include "hotstuff/type.h"
#include "hotstuff/entity.h"
#include "hotstuff/consensus.h"

namespace hotstuff {

enum {
    REQ_CMD = 0x4,
    RESP_CMD = 0x5,
    CHK_CMD = 0x6
};

struct MsgClient: public salticidae::MsgBase<> {
    using MsgBase::MsgBase;
    void gen_reqcmd(const Command &cmd);
    void parse_reqcmd(command_t &cmd, HotStuffCore *hsc) const;

    void gen_respcmd(const Finality &fin);
    void parse_respcmd(Finality &fin) const;

    void gen_chkcmd(const uint256_t &cmd_hash);
    void parse_chkcmd(uint256_t &cmd_hash) const;
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
