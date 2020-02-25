#!/bin/bash
killall hotstuff-app
./examples/hotstuff-app --conf ./hotstuff-sec0.conf > log0 2>&1 &
leader_pid="$!"
rep=({1..3})
if [[ $# -gt 0 ]]; then
    rep=($@)
fi
for i in "${rep[@]}"; do
    echo "starting replica $i"
    #valgrind --leak-check=full ./examples/hotstuff-app --conf hotstuff-sec${i}.conf > log${i} 2>&1 &
    #gdb -ex r -ex bt -ex q --args ./examples/hotstuff-app --conf hotstuff-sec${i}.conf > log${i} 2>&1 &
    ./examples/hotstuff-app --conf ./hotstuff-sec${i}.conf > log${i} 2>&1 &
done
echo "All replicas started. Let's issue some commands to be replicated (in 5 sec)..."
sleep 5
echo "Start issuing commands and the leader will be killed in 5 seconds"
./examples/hotstuff-client --idx 0 --iter -1 --max-async 4 &
cli_pid=$!
sleep 5
kill "$leader_pid"
echo "Leader is dead. Let's try to restart our clients (because the simple clients don't timeout/retry some lost requests)."
kill "$cli_pid"
./examples/hotstuff-client --idx 0 --iter -1 --max-async 4
