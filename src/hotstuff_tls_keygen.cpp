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
#include "salticidae/crypto.h"
#include "hotstuff/type.h"

using salticidae::Config;
using hotstuff::tls_pkey_bt;
using hotstuff::tls_x509_bt;

int main(int argc, char **argv) {
    Config config("hotstuff.conf");
    tls_pkey_bt priv_key;
    tls_x509_bt pub_key;
    auto opt_n = Config::OptValInt::create(1);
    config.add_opt("num", opt_n, Config::SET_VAL);
    config.parse(argc, argv);
    int n = opt_n->get();
    if (n < 1)
        error(1, 0, "n must be >0");
    while (n--)
    {
        priv_key = new salticidae::PKey(salticidae::PKey::create_privkey_rsa());
        pub_key = new salticidae::X509(salticidae::X509::create_self_signed_from_pubkey(*priv_key));
        printf("crt:%s sec:%s cid:%s\n",
                salticidae::get_hex(pub_key->get_der()).c_str(),
                salticidae::get_hex(priv_key->get_privkey_der()).c_str(),
                salticidae::get_hex(salticidae::get_hash(pub_key->get_der())).c_str());
    }
    return 0;
}
