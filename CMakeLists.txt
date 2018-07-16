cmake_minimum_required(VERSION 3.9)
project(hotstuff)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/salticidae/cmake/Modules/")

add_subdirectory(salticidae)
include_directories(salticidae/include)

find_package(Libevent REQUIRED)
find_package(OpenSSL REQUIRED)

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

include_directories(include)
add_library(hotstuff
    src/entity.cpp
    src/core.cpp
    src/client.cpp
    src/crypto.cpp
    src/util.cpp)
target_link_libraries(hotstuff salticidae secp256k1 crypto)

add_subdirectory(test)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DHOTSTUFF_DEBUG_LOG)
    add_definitions(-DSALTICIDAE_DEBUG_LOG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-DHOTSTUFF_NORMAL_LOG)
    add_definitions(-DSALTICIDAE_NORMAL_LOG)
endif()

# add executables

add_executable(hotstuff-app
    src/hotstuff.cpp)
target_link_libraries(hotstuff-app hotstuff)

add_executable(hotstuff-client
    src/hotstuff_client.cpp)
target_link_libraries(hotstuff-client hotstuff)

add_executable(hotstuff-keygen
    src/hotstuff_keygen.cpp)
target_link_libraries(hotstuff-keygen hotstuff)

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
#message(${CMAKE_CXX_FLAGS_RELEASE})
#remove_cxx_flag("-O3")
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
#message(${CMAKE_CXX_FLAGS_RELEASE})
