#include "hotstuff/crypto.h"

using namespace hotstuff;

int main() {
    PrivKeySecp256k1 p;
    p.from_hex("4aede145d13021fb43c938bced67511a7740c05786d3e0b94ffbdaa7f15afc57");
    pubkey_bt pub = p.get_pubkey();
    printf("%s\n", get_hex(*pub).c_str());
    DataStream s;
    s << *pub;
    PubKeySecp256k1 pub2;
    s >> pub2;
    printf("%s\n", get_hex(pub2).c_str());
    SigSecp256k1 sig;
    sig.sign(bytearray_t(32), p);
    printf("%s\n", get_hex(sig).c_str());
    s << sig;
    SigSecp256k1 sig2(secp256k1_default_verify_ctx);
    s >> sig2;
    bytearray_t msg = bytearray_t(32);
    msg[0] = 1;
    printf("%d %d\n", sig2.verify(bytearray_t(32), pub2),
                    sig2.verify(msg, pub2));
    p.from_rand();
    printf("%s\n", get_hex(p).c_str());
    sig.sign(msg, p);
    printf("%s\n", get_hex(sig).c_str());
    s << sig;
    s >> sig2;
    printf("%d %d\n", sig2.verify(msg, PubKeySecp256k1(p)),
                    sig2.verify(msg, pub2));
}
