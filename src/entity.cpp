#include "hotstuff/entity.h"
#include "hotstuff/hotstuff.h"

namespace hotstuff {

void Block::serialize(DataStream &s) const {
    s << (uint32_t)parent_hashes.size();
    for (const auto &hash: parent_hashes)
        s << hash;
    s << (uint32_t)cmds.size();
    for (auto cmd: cmds)
        s << *cmd;
    if (qc == nullptr)
        s << (uint8_t)0;
    else
        s << (uint8_t)1 << *qc;
}

void Block::unserialize(DataStream &s, HotStuffCore *hsc) {
    uint32_t n;
    uint8_t has_qc;
    s >> n;
    parent_hashes.resize(n);
    for (auto &hash: parent_hashes)
        s >> hash;
    s >> n;
    cmds.resize(n);
    for (auto &cmd: cmds)
        cmd = hsc->parse_cmd(s);
    s >> has_qc;
    qc = has_qc ? hsc->parse_quorum_cert(s) : nullptr;
    this->hash = salticidae::get_hash(*this);
}

}
