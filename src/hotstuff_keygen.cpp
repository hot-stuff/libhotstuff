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

#include <error.h>
#include "salticidae/util.h"
#include "hotstuff/crypto.h"

using salticidae::Config;
using hotstuff::privkey_bt;
using hotstuff::pubkey_bt;

int main(int argc, char **argv) {
    Config config("hotstuff.conf");
    privkey_bt priv_key;
    auto opt_n = Config::OptValInt::create(1);
    auto opt_algo = Config::OptValStr::create("secp256k1");
    config.add_opt("num", opt_n, Config::SET_VAL);
    config.add_opt("algo", opt_algo, Config::SET_VAL);
    config.parse(argc, argv);
    auto &algo = opt_algo->get();
    if (algo == "secp256k1")
        priv_key = new hotstuff::PrivKeySecp256k1();
    else
        error(1, 0, "algo not supported");
    int n = opt_n->get();
    if (n < 1)
        error(1, 0, "n must be >0");
    while (n--)
    {
        priv_key->from_rand();
        pubkey_bt pub_key = priv_key->get_pubkey();
        printf("pub:%s sec:%s\n", get_hex(*pub_key).c_str(),
                            get_hex(*priv_key).c_str());
    }
    return 0;
}
