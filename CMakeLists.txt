# Copyright 2018 VMware
# Copyright 2018 Ted Yin
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.9)
project(hotstuff)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/salticidae/cmake/Modules/")

add_subdirectory(salticidae)
include_directories(salticidae/include)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

include(ExternalProject)
include_directories(secp256k1/include)
ExternalProject_Add(libsecp256k1
    SOURCE_DIR secp256k1
    CONFIGURE_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/secp256k1/autogen.sh
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/secp256k1/configure --disable-shared --with-pic --with-bignum=no --enable-module-recovery
    BUILD_COMMAND make
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 1)

add_library(secp256k1 STATIC IMPORTED)
set_target_properties(
    secp256k1
    PROPERTIES IMPORTED_LOCATION
    ${CMAKE_CURRENT_SOURCE_DIR}/secp256k1/.libs/libsecp256k1.a)
add_dependencies(secp256k1 libsecp256k1)

# add libraries

include_directories(./)
include_directories(include)
add_library(hotstuff
    OBJECT
    src/util.cpp
    src/client.cpp
    src/crypto.cpp
    src/entity.cpp
    src/consensus.cpp
    src/hotstuff.cpp
    )

option(BUILD_SHARED "build shared library." OFF)
if(BUILD_SHARED)
    set_property(TARGET hotstuff PROPERTY POSITION_INDEPENDENT_CODE 1)
    add_library(hotstuff_shared SHARED $<TARGET_OBJECTS:hotstuff>)
    set_target_properties(hotstuff_shared PROPERTIES OUTPUT_NAME "hotstuff")
    target_link_libraries(hotstuff_shared salticidae_static secp256k1 crypto ${CMAKE_THREAD_LIBS_INIT})
endif()
add_library(hotstuff_static STATIC $<TARGET_OBJECTS:hotstuff>)
set_target_properties(hotstuff_static PROPERTIES OUTPUT_NAME "hotstuff")
target_link_libraries(hotstuff_static salticidae_static secp256k1 crypto ${CMAKE_THREAD_LIBS_INIT})

add_subdirectory(test)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release")
endif()

option(HOTSTUFF_DEBUG_LOG "enable debug log" OFF)
option(HOTSTUFF_NORMAL_LOG "enable normal log" ON)
option(HOTSTUFF_PROTO_LOG "enable protocol log" OFF)
option(HOTSTUFF_MSG_STAT "eanble message statistics" ON)
option(HOTSTUFF_BLK_PROFILE "enable block profiling" OFF)
option(HOTSTUFF_TWO_STEP "use two-step HotStuff (instead of three-step HS)" OFF)
option(BUILD_EXAMPLES "build examples" ON)

configure_file(src/config.h.in include/hotstuff/config.h @ONLY)

# build examples
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# build tools
add_executable(hotstuff-keygen
    src/hotstuff_keygen.cpp)
target_link_libraries(hotstuff-keygen hotstuff_static)

add_executable(hotstuff-tls-keygen
    src/hotstuff_tls_keygen.cpp)
target_link_libraries(hotstuff-tls-keygen hotstuff_static)

find_package(Doxygen)
if (DOXYGEN_FOUND)
    add_custom_target(doc
        ${DOXYGEN_EXECUTABLE} doc/doxygen.conf WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif(DOXYGEN_FOUND)

#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -no-pie -pg")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -W -Wall -Wextra -pedantic -Wsuggest-override")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -W -Wall -Wextra -pedantic -Wsuggest-override")

macro(remove_cxx_flag flag)
  string(REPLACE "${flag}" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
endmacro()

remove_cxx_flag("-DNDEBUG")
