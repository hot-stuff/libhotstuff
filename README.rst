libhotstuff
-----------

libhotstuff is a general-purpose BFT state machine replication library with
modularity and simplicity, suitable for building hybrid consensus
cryptocurrencies.

Features
========

- Simplicity. The protocol core logic implementation is as simple as the one
  specified in our paper. See ``consensus.h`` and ``consensus.cpp``.

- Modular design. You can use abstraction from the lowest level (the core
  protocol logic) to the highest level (the state machine replication service
  with network implementation) in your application, or override/redefine the
  detailed behavior to customize your own consensus.

- Liveness decoupled from safety. The liveness logic is entirely decoupled from
  safety. By defining your own liveness gadget ("PaceMaker"), you can implement
  your own liveness heuristics/algorithm.  The actual performance varies
  depending on your liveness implementation, but the safety is always intact.

- Friendly to blockchain systems. A PaceMaker could potentially be PoW-based and
  it makes it easier to build a hybrid consensus system, or use it at the core of
  some cryptocurrencies.

- Minimal. The project strives to keep code base small and implement just the
  basic functionality of state machine replication: to deliver a consistent
  command sequence to the library user. Application-specific parts are not
  included, but demonstrated in the demo program.

Try the Current Version
=======================

NOTICE: the project is still in-progress. Try at your own risk, and this
section may be incomplete and subject to changes.

::

    # install from the repo
    git clone https://github.com/hot-stuff/libhotstuff.git
    cd hot-stuff/
    git submodule update --init --recursive

    # ensure openssl and libevent are installed on your machine
    cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=ON -DHOTSTUFF_PROTO_LOG=ON
    make

    # start 4 demo replicas with scripts/run_demo.sh
    # start the demo client with scripts/run_demo_client.sh
