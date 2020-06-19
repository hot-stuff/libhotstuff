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

#ifndef _HOTSTUFF_CRYPTO_H
#define _HOTSTUFF_CRYPTO_H

#include <openssl/rand.h>

#include "secp256k1.h"
#include "salticidae/crypto.h"
#include "hotstuff/type.h"
#include "hotstuff/task.h"
#include <bls.hpp>

namespace hotstuff {

using salticidae::SHA256;

class PubKey: public Serializable, Cloneable {
    public:
    virtual ~PubKey() = default;
    virtual PubKey *clone() override = 0;
};

using pubkey_bt = BoxObj<PubKey>;

class PrivKey: public Serializable {
    public:
    virtual ~PrivKey() = default;
    virtual pubkey_bt get_pubkey() const = 0;
    virtual void from_rand() = 0;
};

using privkey_bt = BoxObj<PrivKey>;

class PartCert: public Serializable, public Cloneable {
    public:
    virtual ~PartCert() = default;
    virtual promise_t verify(const PubKey &pubkey, VeriPool &vpool) = 0;
    virtual bool verify(const PubKey &pubkey) = 0;
    virtual const uint256_t &get_obj_hash() const = 0;
    virtual PartCert *clone() override = 0;
};

class ReplicaConfig;

class QuorumCert: public Serializable, public Cloneable {
    public:
    virtual ~QuorumCert() = default;
    virtual void add_part(ReplicaID replica, const PartCert &pc) = 0;
    virtual void compute() = 0;
    virtual promise_t verify(const ReplicaConfig &config, VeriPool &vpool) = 0;
    virtual bool verify(const ReplicaConfig &config) = 0;
    virtual const uint256_t &get_obj_hash() const = 0;
    virtual QuorumCert *clone() override = 0;
};

using part_cert_bt = BoxObj<PartCert>;
using quorum_cert_bt = BoxObj<QuorumCert>;

class PubKeyDummy: public PubKey {
    PubKeyDummy *clone() override { return new PubKeyDummy(*this); }
    void serialize(DataStream &) const override {}
    void unserialize(DataStream &) override {}
};

class PrivKeyDummy: public PrivKey {
    pubkey_bt get_pubkey() const override { return new PubKeyDummy(); }
    void serialize(DataStream &) const override {}
    void unserialize(DataStream &) override {}
    void from_rand() override {}
};

class PartCertDummy: public PartCert {
    uint256_t obj_hash;
    public:
    PartCertDummy() {}
    PartCertDummy(const uint256_t &obj_hash):
        obj_hash(obj_hash) {}

    void serialize(DataStream &s) const override {
        s << (uint32_t)0 << obj_hash;
    }

    void unserialize(DataStream &s) override {
        uint32_t tmp;
        s >> tmp >> obj_hash;
    }

    PartCert *clone() override {
        return new PartCertDummy(obj_hash);
    }

    bool verify(const PubKey &) override { return true; }
    promise_t verify(const PubKey &, VeriPool &) override {
        return promise_t([](promise_t &pm){ pm.resolve(true); });
    }

    const uint256_t &get_obj_hash() const override { return obj_hash; }
};

class QuorumCertDummy: public QuorumCert {
    uint256_t obj_hash;
    public:
    QuorumCertDummy() {}
    QuorumCertDummy(const ReplicaConfig &, const uint256_t &obj_hash):
        obj_hash(obj_hash) {}

    void serialize(DataStream &s) const override {
        s << (uint32_t)1 << obj_hash;
    }

    void unserialize(DataStream &s) override {
        uint32_t tmp;
        s >> tmp >> obj_hash;
    }

    QuorumCert *clone() override {
        return new QuorumCertDummy(*this);
    }

    void add_part(ReplicaID, const PartCert &) override {}
    void compute() override {}
    bool verify(const ReplicaConfig &) override { return true; }
    promise_t verify(const ReplicaConfig &, VeriPool &) override {
        return promise_t([](promise_t &pm) { pm.resolve(true); });
    }

    const uint256_t &get_obj_hash() const override { return obj_hash; }
};

class Secp256k1Context {
    secp256k1_context *ctx;
    friend class PubKeySecp256k1;
    friend class SigSecp256k1;
    public:
    Secp256k1Context(bool sign = false):
        ctx(secp256k1_context_create(
            sign ? SECP256K1_CONTEXT_SIGN :
                    SECP256K1_CONTEXT_VERIFY)) {}

    Secp256k1Context(const Secp256k1Context &) = delete;

    Secp256k1Context(Secp256k1Context &&other): ctx(other.ctx) {
        other.ctx = nullptr;
    }

    ~Secp256k1Context() {
        if (ctx) secp256k1_context_destroy(ctx);
    }
};

using secp256k1_context_t = ArcObj<Secp256k1Context>;

extern secp256k1_context_t secp256k1_default_sign_ctx;
extern secp256k1_context_t secp256k1_default_verify_ctx;

class PrivKeySecp256k1;

class PubKeySecp256k1: public PubKey {
    static const auto _olen = 33;
    friend class SigSecp256k1;
    secp256k1_pubkey data;
    secp256k1_context_t ctx;

    public:
    PubKeySecp256k1(const secp256k1_context_t &ctx =
                            secp256k1_default_sign_ctx):
        PubKey(), ctx(ctx) {}

    PubKeySecp256k1(const bytearray_t &raw_bytes,
                    const secp256k1_context_t &ctx =
                            secp256k1_default_sign_ctx):
        PubKeySecp256k1(ctx) { from_bytes(raw_bytes); }

    inline PubKeySecp256k1(const PrivKeySecp256k1 &priv_key,
                            const secp256k1_context_t &ctx =
                                    secp256k1_default_sign_ctx);

    void serialize(DataStream &s) const override {
        static uint8_t output[_olen];
        size_t olen = _olen;
        (void)secp256k1_ec_pubkey_serialize(
                ctx->ctx, (unsigned char *)output,
                &olen, &data, SECP256K1_EC_COMPRESSED);
        s.put_data(output, output + _olen);
    }

    void unserialize(DataStream &s) override {
        static const auto _exc = std::invalid_argument("ill-formed public key");
        try {
            if (!secp256k1_ec_pubkey_parse(
                    ctx->ctx, &data, s.get_data_inplace(_olen), _olen))
                throw _exc;
        } catch (std::ios_base::failure &) {
            throw _exc;
        }
    }

    PubKeySecp256k1 *clone() override {
        return new PubKeySecp256k1(*this);
    }
};

class PrivKeySecp256k1: public PrivKey {
    static const auto nbytes = 32;
    friend class PubKeySecp256k1;
    friend class SigSecp256k1;
    uint8_t data[nbytes];
    secp256k1_context_t ctx;

    public:
    PrivKeySecp256k1(const secp256k1_context_t &ctx =
                            secp256k1_default_sign_ctx):
        PrivKey(), ctx(ctx) {}

    PrivKeySecp256k1(const bytearray_t &raw_bytes,
                     const secp256k1_context_t &ctx =
                            secp256k1_default_sign_ctx):
        PrivKeySecp256k1(ctx) { from_bytes(raw_bytes); }

    void serialize(DataStream &s) const override {
        s.put_data(data, data + nbytes);
    }

    void unserialize(DataStream &s) override {
        static const auto _exc = std::invalid_argument("ill-formed private key");
        try {
            memmove(data, s.get_data_inplace(nbytes), nbytes);
        } catch (std::ios_base::failure &) {
            throw _exc;
        }
    }

    void from_rand() override {
        if (!RAND_bytes(data, nbytes))
            throw std::runtime_error("cannot get rand bytes from openssl");
    }

    inline pubkey_bt get_pubkey() const override;
};

pubkey_bt PrivKeySecp256k1::get_pubkey() const {
    return new PubKeySecp256k1(*this, ctx);
}

PubKeySecp256k1::PubKeySecp256k1(
        const PrivKeySecp256k1 &priv_key,
        const secp256k1_context_t &ctx): PubKey(), ctx(ctx) {
    if (!secp256k1_ec_pubkey_create(ctx->ctx, &data, priv_key.data))
        throw std::invalid_argument("invalid secp256k1 private key");
}

class SigSecp256k1: public Serializable {
    secp256k1_ecdsa_signature data;
    secp256k1_context_t ctx;

    static void check_msg_length(const bytearray_t &msg) {
        if (msg.size() != 32)
            throw std::invalid_argument("the message should be 32-bytes");
    }

    public:
    SigSecp256k1(const secp256k1_context_t &ctx =
                        secp256k1_default_sign_ctx):
        Serializable(), ctx(ctx) {}
    SigSecp256k1(const uint256_t &digest,
                const PrivKeySecp256k1 &priv_key,
                secp256k1_context_t &ctx =
                        secp256k1_default_sign_ctx):
        Serializable(), ctx(ctx) {
        sign(digest, priv_key);
    }

    void serialize(DataStream &s) const override {
        static uint8_t output[64];
        (void)secp256k1_ecdsa_signature_serialize_compact(
            ctx->ctx, (unsigned char *)output,
            &data);
        s.put_data(output, output + 64);
    }

    void unserialize(DataStream &s) override {
        static const auto _exc = std::invalid_argument("ill-formed signature");
        try {
            if (!secp256k1_ecdsa_signature_parse_compact(
                    ctx->ctx, &data, s.get_data_inplace(64)))
                throw _exc;
        } catch (std::ios_base::failure &) {
            throw _exc;
        }
    }

    void sign(const bytearray_t &msg, const PrivKeySecp256k1 &priv_key) {
        check_msg_length(msg);
        if (!secp256k1_ecdsa_sign(
                ctx->ctx, &data,
                (unsigned char *)&*msg.begin(),
                (unsigned char *)priv_key.data,
                NULL, // default nonce function
                NULL))
            throw std::invalid_argument("failed to create secp256k1 signature");
    }

    bool verify(const bytearray_t &msg, const PubKeySecp256k1 &pub_key,
                const secp256k1_context_t &_ctx) const {
        check_msg_length(msg);
        std::cout << "blah1" << std::endl;
        std::cout << (unsigned char *)&*msg.begin() << std::endl;
        return secp256k1_ecdsa_verify(
                _ctx->ctx, &data,
                (unsigned char *)&*msg.begin(),
                &pub_key.data) == 1;
    }

    bool verify(const bytearray_t &msg, const PubKeySecp256k1 &pub_key) {
        return verify(msg, pub_key, ctx);
    }
};

class Secp256k1VeriTask: public VeriTask {
    uint256_t msg;
    PubKeySecp256k1 pubkey;
    SigSecp256k1 sig;
    public:
    Secp256k1VeriTask(const uint256_t &msg,
                        const PubKeySecp256k1 &pubkey,
                        const SigSecp256k1 &sig):
        msg(msg), pubkey(pubkey), sig(sig) {}
    virtual ~Secp256k1VeriTask() = default;

    bool verify() override {
        return sig.verify(msg, pubkey, secp256k1_default_verify_ctx);
    }
};

class PartCertSecp256k1: public SigSecp256k1, public PartCert {
    uint256_t obj_hash;

    public:
    PartCertSecp256k1() = default;
    PartCertSecp256k1(const PrivKeySecp256k1 &priv_key, const uint256_t &obj_hash):
        SigSecp256k1(obj_hash, priv_key),
        PartCert(),
        obj_hash(obj_hash) {}

    bool verify(const PubKey &pub_key) override {
        return SigSecp256k1::verify(obj_hash,
                                    static_cast<const PubKeySecp256k1 &>(pub_key),
                                    secp256k1_default_verify_ctx);
    }

    promise_t verify(const PubKey &pub_key, VeriPool &vpool) override {
        return vpool.verify(new Secp256k1VeriTask(obj_hash,
                static_cast<const PubKeySecp256k1 &>(pub_key),
                static_cast<const SigSecp256k1 &>(*this)));
    }

    const uint256_t &get_obj_hash() const override { return obj_hash; }

    PartCertSecp256k1 *clone() override {
        return new PartCertSecp256k1(*this);
    }

    void serialize(DataStream &s) const override {
        s << obj_hash;
        this->SigSecp256k1::serialize(s);
    }

    void unserialize(DataStream &s) override {
        s >> obj_hash;
        this->SigSecp256k1::unserialize(s);
    }
};

class QuorumCertSecp256k1: public QuorumCert {
    uint256_t obj_hash;
    salticidae::Bits rids;
    std::unordered_map<ReplicaID, SigSecp256k1> sigs;

    public:
    QuorumCertSecp256k1() = default;
    QuorumCertSecp256k1(const ReplicaConfig &config, const uint256_t &obj_hash);

    void add_part(ReplicaID rid, const PartCert &pc) override {
        if (pc.get_obj_hash() != obj_hash)
            throw std::invalid_argument("PartCert does match the block hash");
        sigs.insert(std::make_pair(
            rid, static_cast<const PartCertSecp256k1 &>(pc)));
        rids.set(rid);
    }

    void compute() override {}

    bool verify(const ReplicaConfig &config) override;
    promise_t verify(const ReplicaConfig &config, VeriPool &vpool) override;

    const uint256_t &get_obj_hash() const override { return obj_hash; }

    QuorumCertSecp256k1 *clone() override {
        return new QuorumCertSecp256k1(*this);
    }

    void serialize(DataStream &s) const override {
        s << obj_hash << rids;
        for (size_t i = 0; i < rids.size(); i++)
            if (rids.get(i)) s << sigs.at(i);
    }

    void unserialize(DataStream &s) override {
        s >> obj_hash >> rids;
        for (size_t i = 0; i < rids.size(); i++)
            if (rids.get(i)) s >> sigs[i];
    }
};

    class PrivKeyBLS;
    class PubKeyBLS: public PubKey {
        static const auto _olen = bls::PublicKey::PUBLIC_KEY_SIZE;
        friend class SigSecBLS;

        bls::PublicKey* data = nullptr;

    public:
        PubKeyBLS() :
                PubKey() {}

        PubKeyBLS(const bytearray_t &raw_bytes) :
                PubKeyBLS() {
            data = new bls::PublicKey(bls::PublicKey::FromBytes(&raw_bytes[0]));
        }

        PubKeyBLS(const PubKeyBLS &obj) {
            data = new bls::PublicKey(*(obj.data));
        }

        ~PubKeyBLS() override {
            delete data;
            data = nullptr;
        }

        inline PubKeyBLS(const PrivKeyBLS &priv_key);

        void serialize(DataStream &s) const override {
            static uint8_t output[_olen];
            data->Serialize(output);
            s.put_data(output, output + _olen);
        }

        void unserialize(DataStream &s) override {
            static const auto _exc = std::invalid_argument("ill-formed public key");

            try {
                data = new bls::PublicKey(bls::PublicKey::FromBytes(s.get_data_inplace(_olen)));
            } catch (std::ios_base::failure &) {
                throw _exc;
            }
        }

        PubKeyBLS *clone() override {
            return new PubKeyBLS(*this);
        }
    };

    class PrivKeyBLS: public PrivKey {
        static const auto nbytes = bls::PrivateKey::PRIVATE_KEY_SIZE;;
        friend class SigSecBLS;

    public:
        bls::PrivateKey* data = nullptr;

        PrivKeyBLS():
                PrivKey() {}

        PrivKeyBLS(const bytearray_t &raw_bytes):
                PrivKeyBLS()
                {
                    static const auto _exc = std::invalid_argument("ill-formed public key");
                    try {
                        data = new bls::PrivateKey(bls::PrivateKey::FromBytes(&raw_bytes[0]));
                    } catch (std::ios_base::failure &) {
                        throw _exc;
                    }
                }

        ~PrivKeyBLS()
        {
            delete data;
            data = nullptr;
        }

        void serialize(DataStream &s) const override {
            static uint8_t output[nbytes];
            data->Serialize(output);
            s.put_data(output, output + nbytes);
        }

        void unserialize(DataStream &s) override {
            static const auto _exc = std::invalid_argument("ill-formed public key");
            try {
                const uint8_t* dat = s.get_data_inplace(bls::PrivateKey::PRIVATE_KEY_SIZE);
                data = new bls::PrivateKey(bls::PrivateKey(bls::PrivateKey::FromBytes(dat)));
            } catch (std::ios_base::failure &) {
                throw _exc;
            }
        }

        void from_rand() override {
            bn_t b;
            bn_new(b);
            data = new bls::PrivateKey(bls::PrivateKey::FromBN(b));
        }

        inline pubkey_bt get_pubkey() const override;
    };

    pubkey_bt PrivKeyBLS::get_pubkey() const {
        return new PubKeyBLS(*this);
    }

    PubKeyBLS::PubKeyBLS(const PrivKeyBLS &priv_key): PubKey() {
        data = new bls::PublicKey(priv_key.data->GetPublicKey());
    }

    class SigSecBLS: public Serializable {

        static void check_msg_length(const bytearray_t &msg) {
            if (msg.size() != 32)
                throw std::invalid_argument("the message should be 32-bytes");
        }

    public:
        bls::InsecureSignature* data = nullptr;

        SigSecBLS ():
                Serializable(){}
        SigSecBLS(const uint256_t &digest,
                  const PrivKeyBLS &priv_key):
                Serializable() {
            sign(digest, priv_key);
        }

        SigSecBLS (const SigSecBLS &obj)
        {
            data = new bls::InsecureSignature(*(obj.data));
        }

        SigSecBLS (bls::InsecureSignature sig):
                Serializable()
                {
                    data = new bls::InsecureSignature(sig);
                }

        ~SigSecBLS() override
        {
            delete data;
            data = nullptr;
        }

        void serialize(DataStream &s) const override {
            static uint8_t output[bls::InsecureSignature::SIGNATURE_SIZE];

            int i = 0;
            for (auto in : data->Serialize())
            {
                output[i++] = in;
            }
            s.put_data(output, output + bls::InsecureSignature::SIGNATURE_SIZE);
        }

        void unserialize(DataStream &s) override {
            static const auto _exc = std::invalid_argument("ill-formed signature");
            try {
                data = new bls::InsecureSignature(bls::InsecureSignature::FromBytes(s.get_data_inplace(bls::InsecureSignature::SIGNATURE_SIZE)));
            } catch (std::ios_base::failure &) {
                throw _exc;
            }
        }

        void sign(const bytearray_t &msg, const PrivKeyBLS &priv_key) {
            check_msg_length(msg);
            std::cout << "sign: " << msg.data() << std::endl;
            data = new bls::InsecureSignature(priv_key.data->SignInsecure(msg.data(), sizeof(&msg)));
        }

        bool verify(const bytearray_t &msg, const PubKeyBLS &pub_key) const {
            check_msg_length(msg);
            std::cout << "blah1" << std::endl;
            std::cout << (unsigned char *)&*msg.begin() << std::endl;
            bool res1 = data->Verify({(unsigned char *)&*msg.begin()}, {*(pub_key.data)});
            std::cout << "blah2 " << res1 << std::endl;
            return res1;
        }
    };

    class SigVeriTaskBLS: public VeriTask {
        uint256_t msg;
        PubKeyBLS pubkey;
        SigSecBLS sig;
    public:
        SigVeriTaskBLS(const uint256_t &msg,
                          const PubKeyBLS &pubkey,
                          const SigSecBLS &sig):
                msg(msg), pubkey(pubkey), sig(sig) {}
        virtual ~SigVeriTaskBLS() = default;

        bool verify() override {
            return sig.verify(msg, pubkey);
        }
    };

    class PartCertBLS: public SigSecBLS, public PartCert {
        uint256_t obj_hash;

    public:
        PartCertBLS() = default;
        PartCertBLS(const PrivKeyBLS &priv_key, const uint256_t &obj_hash):
                SigSecBLS(obj_hash, priv_key),
                PartCert(),
                obj_hash(obj_hash) {}

        PartCertBLS(const PartCertBLS &obj)  : SigSecBLS(obj) { }

        bool verify(const PubKey &pub_key) override {
            return SigSecBLS::verify(obj_hash,
                                     static_cast<const PubKeyBLS &>(pub_key));
        }

        promise_t verify(const PubKey &pub_key, VeriPool &vpool) override {
            return vpool.verify(new SigVeriTaskBLS(obj_hash,
                                                      static_cast<const PubKeyBLS &>(pub_key),
                                                      static_cast<const SigSecBLS &>(*this)));
        }

        const uint256_t &get_obj_hash() const override { return obj_hash; }

        PartCertBLS *clone() override {
            return new PartCertBLS(*this);
        }

        void serialize(DataStream &s) const override {
            s << obj_hash;
            this->SigSecBLS::serialize(s);
        }

        void unserialize(DataStream &s) override {
            s >> obj_hash;
            this->SigSecBLS::unserialize(s);
        }
    };

    class QuorumCertBLS: public QuorumCert {
        uint256_t obj_hash;
        salticidae::Bits rids;
        std::unordered_map<ReplicaID, SigSecBLS> signatures;
        SigSecBLS* theSig = nullptr;

    public:
        QuorumCertBLS() = default;
        QuorumCertBLS(const ReplicaConfig &config, const uint256_t &obj_hash);

        ~QuorumCertBLS()
        {
            delete theSig;
            theSig = nullptr;
        }

        void add_part(ReplicaID rid, const PartCert &pc) override {
            if (pc.get_obj_hash() != obj_hash)
                throw std::invalid_argument("PartCert does match the block hash");
            signatures.insert(std::make_pair(
                    rid, static_cast<const PartCertBLS &>(pc)));
            rids.set(rid);
        }

        void compute() override
        {
            std::vector<bls::InsecureSignature> sigShareOut;
            std::vector<size_t> players;

            for(auto elem : signatures) {
                players.push_back(elem.first);
                sigShareOut.push_back(*elem.second.data);
            }
            std::cout << "Try combine?" << std::endl;
            std::cout << players.size() << std::endl;
            std::cout << sigShareOut.size() << std::endl;
            uint8_t* d = obj_hash.to_bytes().data();

            theSig = new SigSecBLS(bls::Threshold::AggregateUnitSigs(sigShareOut, d, sizeof(d), &players[0], 10));
            std::cout << "those5?" << std::endl;
        }

        bool verify(const ReplicaConfig &config) override;
        promise_t verify(const ReplicaConfig &config, VeriPool &vpool) override;

        const uint256_t &get_obj_hash() const override { return obj_hash; }

        QuorumCertBLS *clone() override {
            return new QuorumCertBLS(*this);
        }

        void serialize(DataStream &s) const override {
            bool combined = (theSig != nullptr);
            s << obj_hash << rids << combined;
            if (combined) {
                s << *theSig;
            }
            else {
                for (size_t i = 0; i < rids.size(); i++)
                    if (rids.get(i)) s << signatures.at(i);
            }
        }

        void unserialize(DataStream &s) override {
            bool combined;
            s >> obj_hash >> rids >> combined;
            if (combined) {
                s >> *theSig;
            }
            else {
                for (size_t i = 0; i < rids.size(); i++)
                    if (rids.get(i)) s >> signatures[i];
            }
        }
    };

}

#endif
