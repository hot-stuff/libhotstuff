#include "hotstuff/entity.h"
#include "hotstuff/hotstuff.h"

namespace hotstuff {

void Block::serialize(DataStream &s) const {
    s << htole((uint32_t)parent_hashes.size());
    for (const auto &hash: parent_hashes)
        s << hash;
    s << htole((uint32_t)cmds.size());
    for (auto cmd: cmds)
        s << *cmd;
    if (qc)
        s << (uint8_t)1 << *qc;
    else
        s << (uint8_t)0;
    s << htole((uint32_t)extra.size()) << extra;
}

void Block::unserialize(DataStream &s, HotStuffCore *hsc) {
    uint32_t n;
    uint8_t flag;
    s >> n;
    n = letoh(n);
    parent_hashes.resize(n);
    for (auto &hash: parent_hashes)
        s >> hash;
    s >> n;
    n = letoh(n);
    cmds.resize(n);
    for (auto &cmd: cmds)
        cmd = hsc->parse_cmd(s);
    s >> flag;
    qc = flag ? hsc->parse_quorum_cert(s) : nullptr;
    s >> n;
    n = letoh(n);
    if (n == 0)
        extra.clear();
    else
    {
        auto base = s.get_data_inplace(n);
        extra = bytearray_t(base, base + n);
    }
    this->hash = salticidae::get_hash(*this);
}

}
