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
    if (qc)
        s << (uint8_t)1 << *qc;
    else
        s << (uint8_t)0;
    if (extra)
        s << (uint8_t)1 << *extra;
    else
        s << (uint8_t)0;
}

void Block::unserialize(DataStream &s, HotStuffCore *hsc) {
    uint32_t n;
    uint8_t flag;
    s >> n;
    parent_hashes.resize(n);
    for (auto &hash: parent_hashes)
        s >> hash;
    s >> n;
    cmds.resize(n);
    for (auto &cmd: cmds)
        cmd = hsc->parse_cmd(s);
    s >> flag;
    qc = flag ? hsc->parse_quorum_cert(s) : nullptr;
    this->hash = salticidae::get_hash(*this);
    s >> flag;
    extra = flag ? hsc->parse_extra_block_data(s) : nullptr;
}

}
