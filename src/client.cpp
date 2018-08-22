#include "hotstuff/client.h"

namespace hotstuff {

const opcode_t MsgReqCmd::opcode;
MsgReqCmd::MsgReqCmd(const Command &cmd) { serialized << cmd; }
void MsgReqCmd::postponed_parse(HotStuffCore *hsc) {
    cmd = hsc->parse_cmd(serialized);
}

const opcode_t MsgRespCmd::opcode;
MsgRespCmd::MsgRespCmd(const Finality &fin) { serialized << fin; }
MsgRespCmd::MsgRespCmd(DataStream &&s) { s >> fin; }

}
