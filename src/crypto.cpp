#include "hotstuff/entity.h"
#include "hotstuff/crypto.h"

namespace hotstuff {

secp256k1_context_t secp256k1_default_sign_ctx = new Secp256k1Context(true);
secp256k1_context_t secp256k1_default_verify_ctx = new Secp256k1Context(false);

QuorumCertSecp256k1::QuorumCertSecp256k1(
        const ReplicaConfig &config, const uint256_t &blk_hash):
            QuorumCert(), blk_hash(blk_hash), rids(config.nmajority) {
    rids.clear();
}
   
bool QuorumCertSecp256k1::verify(const ReplicaConfig &config) {
    bytearray_t _blk_hash(blk_hash);
    if (rids.size() < config.nmajority) return false;
    for (size_t i = 0; i < rids.size(); i++)
        if (!sigs[i].verify(_blk_hash,
                            static_cast<const PubKeySecp256k1 &>(config.get_pubkey(rids.get(i)))))
            return false;
    return true;
}

}
