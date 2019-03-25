/**
 * Copyright 2018 VMware
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hotstuff/entity.h"
#include "hotstuff/crypto.h"

namespace hotstuff {

secp256k1_context_t secp256k1_default_sign_ctx = new Secp256k1Context(true);
secp256k1_context_t secp256k1_default_verify_ctx = new Secp256k1Context(false);

QuorumCertSecp256k1::QuorumCertSecp256k1(
        const ReplicaConfig &config, const uint256_t &obj_hash):
            QuorumCert(), obj_hash(obj_hash), rids(config.nreplicas) {
    rids.clear();
}
   
bool QuorumCertSecp256k1::verify(const ReplicaConfig &config) {
    if (sigs.size() < config.nmajority) return false;
    for (size_t i = 0; i < rids.size(); i++)
        if (rids.get(i))
        {
            HOTSTUFF_LOG_DEBUG("checking cert(%d), obj_hash=%s",
                                i, get_hex10(obj_hash).c_str());
            if (!sigs[i].verify(obj_hash,
                            static_cast<const PubKeySecp256k1 &>(config.get_pubkey(i)),
                            secp256k1_default_verify_ctx))
            return false;
        }
    return true;
}

promise_t QuorumCertSecp256k1::verify(const ReplicaConfig &config, VeriPool &vpool) {
    if (sigs.size() < config.nmajority)
        return promise_t([](promise_t &pm) { pm.resolve(false); });
    std::vector<promise_t> vpm;
    for (size_t i = 0; i < rids.size(); i++)
        if (rids.get(i))
        {
            HOTSTUFF_LOG_DEBUG("checking cert(%d), obj_hash=%s",
                                i, get_hex10(obj_hash).c_str());
            vpm.push_back(vpool.verify(new Secp256k1VeriTask(obj_hash,
                            static_cast<const PubKeySecp256k1 &>(config.get_pubkey(i)),
                            sigs[i])));
        }
    return promise::all(vpm).then([](const promise::values_t &values) {
        for (const auto &v: values)
            if (!promise::any_cast<bool>(v)) return false;
        return true;
    });
}

}
