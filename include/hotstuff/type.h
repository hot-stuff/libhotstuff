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

#ifndef _HOTSTUFF_TYPE_H
#define _HOTSTUFF_TYPE_H

#include "promise.hpp"
#include "salticidae/event.h"
#include "salticidae/ref.h"
#include "salticidae/netaddr.h"
#include "salticidae/stream.h"
#include "salticidae/type.h"
#include "salticidae/util.h"

namespace hotstuff {

using salticidae::RcObj;
using salticidae::ArcObj;
using salticidae::BoxObj;

using salticidae::uint256_t;
using salticidae::DataStream;
using salticidae::htole;
using salticidae::letoh;
using salticidae::get_hex;
using salticidae::from_hex;
using salticidae::bytearray_t;
using salticidae::get_hash;

using salticidae::NetAddr;
using salticidae::TimerEvent;
using salticidae::FdEvent;
using salticidae::EventContext;
using promise::promise_t;

template<typename SerialType>
inline std::string get_hex10(const SerialType &x) {
    return get_hex(x).substr(0, 10);
}

class HotStuffError: public salticidae::SalticidaeError {
    public:
    template<typename... Args>
    HotStuffError(Args... args): salticidae::SalticidaeError(args...) {}
};

class HotStuffInvalidEntity: public HotStuffError {
    public:
    template<typename... Args>
    HotStuffInvalidEntity(Args... args): HotStuffError(args...) {}
};

using salticidae::Serializable;

class Cloneable {
    public:
    virtual ~Cloneable() = default;
    virtual Cloneable *clone() = 0;
};

using ReplicaID = uint16_t;
using opcode_t = uint8_t;
using tls_pkey_bt = BoxObj<salticidae::PKey>;
using tls_x509_bt = BoxObj<salticidae::X509>;

}

#endif
