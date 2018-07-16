#!/bin/bash
for i in {0..3}; do
    #valgrind ./hotstuff-app --conf hotstuff-sec${i}.conf > log${i} 2>&1 &
    ./hotstuff-app --conf hotstuff-sec${i}.conf > log${i} 2>&1 &
done
wait
