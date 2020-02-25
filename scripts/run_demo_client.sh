#!/bin/bash
# Try to run run_demo.sh first and then this script, then use Ctrl-C to
# terminate the proposing replica (e.g. replica 0). Leader rotation will be
# scheduled. Try to kill and run this script again, new commands should still
# get through (be replicated) once the new leader becomes stable.

./examples/hotstuff-client --idx 0 --iter -1 --max-async 4
