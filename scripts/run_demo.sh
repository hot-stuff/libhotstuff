#!/bin/bash
i=${IMAGE_IDX:=1}
# avoid stack overflow as in our simple demo the bootstrapping replica will
# potentially have a long chain of promise resolution
ulimit -s unlimited

echo "starting replica $i"
#valgrind --leak-check=full ./examples/hotstuff-app --conf hotstuff-sec${i}.conf > log${i} 2>&1 &
#gdb -ex r -ex bt -ex q --args ./examples/hotstuff-app --conf hotstuff-sec${i}.conf > log${i} 2>&1 &
cd libhotstuff
./examples/hotstuff-app --conf "./hotstuff-sec${i}.conf" > "logs" 2>&1 
