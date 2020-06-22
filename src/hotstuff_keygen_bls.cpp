
#include "salticidae/util.h"

#include <iostream>
#include <bls.hpp>
#include <random>
#include <thread>
#include <atomic>
#include <cmath>
#include <fstream>

using namespace bls;
using namespace std;

int main(int argc, char **argv) {

    if (argc != 3) {
        throw std::invalid_argument( "Expecting N, K as parameters" );
    }

    char *endptr;
    int N = atoi(argv[1]);
    int K = atoi(argv[2]);

    std::vector<std::vector<PublicKey>> commits;
    std::vector<std::vector<PrivateKey>> frags;
    for (size_t i = 0; i < N; ++i) {
        commits.emplace_back(std::vector<PublicKey>());
        frags.emplace_back(std::vector<PrivateKey>());
        for (size_t j = 0; j < N; ++j) {
            if (j < K) {
                g1_t g;
                commits[i].emplace_back(PublicKey::FromG1(&g));
            }
            bn_t b;
            bn_new(b);
            frags[i].emplace_back(PrivateKey::FromBN(b));
        }
    }

    frags[0][0].GetPublicKey();

    for (int i = 0; i < N; i++) {
        Threshold::Create(commits[i], frags[i], K, N);
    }

    std::vector<PublicKey> keys;
    keys.reserve(N);

    std::ofstream myfile;

    myfile.open (((string) "keys").append(std::to_string(N)).append(".cfg"));

    for (int i = 0; i < N; i++) {
        keys.push_back(commits[i][0]);
    }
    
    PublicKey masterPubkey = PublicKey::AggregateInsecure(keys);

    uint8_t pkBytes[bls::PublicKey::PUBLIC_KEY_SIZE];
    masterPubkey.Serialize(pkBytes);
    string hexkey = bls::Util::HexStr(pkBytes, bls::PublicKey::PUBLIC_KEY_SIZE);
    myfile << "master: " << hexkey << "\n\n";

    std::vector<std::vector<PrivateKey>> recvdFrags = {{}};
    for (int i = 0; i < N; ++i) {
        recvdFrags.emplace_back(std::vector<PrivateKey>());
        for (int j = 0; j < N; ++j) {
            recvdFrags[i].emplace_back(frags[j][i]);
        }
    }

    vector<PrivateKey> privs;
    privs.reserve(N);

    for (int x = 0; x < N; x++) {
        PrivateKey priv = PrivateKey::AggregateInsecure(recvdFrags[x]);
        privs.push_back(priv);

        uint8_t pkBytes[bls::PrivateKey::PRIVATE_KEY_SIZE];
        priv.Serialize(pkBytes);
        string hexkey = bls::Util::HexStr(pkBytes, bls::PrivateKey::PRIVATE_KEY_SIZE);
        myfile << "priv" << x << ": " << hexkey << "\n";
    }

    for (int x = 0; x < N; x++) {
        uint8_t pkBytes[bls::PublicKey::PUBLIC_KEY_SIZE];
        privs[x].GetPublicKey().Serialize(pkBytes);
        string hexkey = bls::Util::HexStr(pkBytes, bls::PublicKey::PUBLIC_KEY_SIZE);
        myfile << "pub" << x << ": " << hexkey << "\n";
    }

    return 0;
}
