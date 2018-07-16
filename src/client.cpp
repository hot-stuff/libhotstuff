#include "client.h"

namespace hotstuff {

uint64_t CommandDummy::cnt = 0;

void MsgClient::gen_reqcmd(const Command &cmd) {
    DataStream s;
    set_opcode(REQ_CMD);
    s << cmd;
    set_payload(std::move(s));
}

void MsgClient::parse_reqcmd(CommandDummy &cmd) const {
    DataStream s(get_payload());
    s >> cmd;
}

void MsgClient::gen_respcmd(const uint256_t &cmd_hash, const Finality &fin) {
    DataStream s;
    set_opcode(RESP_CMD);
    s << cmd_hash << fin;
    set_payload(std::move(s));
}

void MsgClient::parse_respcmd(uint256_t &cmd_hash, Finality &fin) const {
    DataStream s(get_payload());
    s >> cmd_hash >> fin;
}

void MsgClient::gen_chkcmd(const uint256_t &cmd_hash) {
    DataStream s;
    set_opcode(CHK_CMD);
    s << cmd_hash;
    set_payload(std::move(s));
}

void MsgClient::parse_chkcmd(uint256_t &cmd_hash) const {
    DataStream s(get_payload());
    s >> cmd_hash;
}

}
