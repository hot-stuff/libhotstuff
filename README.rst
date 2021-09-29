libhotstuff
-----------

libhotstuff is a general-purpose BFT state machine replication library with
modularity and simplicity, suitable for building hybrid consensus
cryptocurrencies.

Paper
=====

This repository includes the prototype implementation evaluated in our
*HotStuff: BFT Consensus in the Lens of Blockchain* paper. The consensus
protocol is also used by Facebook in Libra_ (now rebranded to "Diem") project.

Feel free to contact us if you'd like to reproduce the results in the paper, or
tweak the code for your own project/product.

- Full paper: https://arxiv.org/abs/1803.05069
- PODC 2019 paper: https://dl.acm.org/citation.cfm?id=3331591

Heads Up
========

Although we don't know why, many developers seem to be stuck with those
intermediate algorithms in the paper (e.g. Basic HotStuff) that are only for
theory/research purpose. As already noted in the paper, we recommend the last
algorithm ("Event-Driven HotStuff") as it is simple, elegant (there is no
"view" at all in the core protocol and only one generic type of QCs) and very
close to an actual implementation. It is also the original algorithm we came up
with, before adding in Basic/Chained HotStuff that may help some researchers
understand its theoretical relation to PBFT, etc.

TL;DR: read the spec (and the proof) of the algorithm in "Implementation"
section, if you're only curious about using HotStuff in your system/work.

.. _Libra: https://github.com/libra

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
    cd libhotstuff/
    git submodule update --init --recursive

    # ensure openssl and libevent are installed on your machine, more
    # specifically, you need:
    #
    # CMake >= 3.9 (cmake)
    # C++14 (g++)
    # libuv >= 1.10.0 (libuv1-dev)
    # openssl >= 1.1.0 (libssl-dev)
    #
    # on Ubuntu: sudo apt-get install libssl-dev libuv1-dev cmake make

    cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=ON -DHOTSTUFF_PROTO_LOG=ON
    make

    # start 4 demo replicas with scripts/run_demo.sh
    # then, start the demo client with scripts/run_demo_client.sh


    # Fault tolerance:
    # Try to run the replicas as in run_demo.sh first and then run_demo_client.sh.
    # Use Ctrl-C to terminate the proposing replica (e.g. replica 0). Leader
    # rotation will be scheduled. Try to kill and run run_demo_client.sh again, new
    # commands should still get through (be replicated) once the new leader becomes
    # stable. Or try the following script:
    # scripts/faulty_leader_demo.sh

Try to Reproduce Our Basic Results
==================================

See here_.

TODO (When I get some free time...)
===================================

- Rewrite this minimal code base in Rust: this time, with all the experience,
  without C++ template kung-fu, I plan to have a ready-to-use, blackbox-like
  libhotstuff implementation as a full library with better encapsulation and
  interface. The new goal would be *any* engineer without knowledge of BFT
  should be able to use it for his/her own application, without changing the
  library code.  Ping me if you like this re-writing idea or you'd like to
  be part of it.

- Limit the async event callback depth (otherwise in the demo a fresh replica
  could overflow its callback stack when trying to catch up)
- Add a PoW-based Pacemaker example
- Branch pruning & swapping (the current implementation stores the entire chain in memory)
- Persistent protocol state (recovery?)

.. _here: https://github.com/hot-stuff/libhotstuff/tree/master/scripts/deploy
