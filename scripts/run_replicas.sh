#!/bin/bash
rep=({0..3})
if [[ $# -gt 0 ]]; then
    rep=($@)
fi
for i in "${rep[@]}"; do
    echo "starting replica $i"
    #valgrind --leak-check=full ./hotstuff-app --conf hotstuff-sec${i}.conf > log${i} 2>&1 &
    #gdb -ex r -ex bt -ex q --args ./hotstuff-app --conf hotstuff-sec${i}.conf > log${i} 2>&1 &
    ./hotstuff-app --conf hotstuff-sec${i}.conf > log${i} 2>&1 &
done
wait
