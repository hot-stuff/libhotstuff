#!/bin/bash
# NOTE: make sure hotstuff-keygen and hotstuff-tls-keygen exist by building the main repo
# NOTE: make sure you have replicas.txt and clients.txt written properly:
# 1) in replicas.txt, each row is "<external_ip_for_ssh> <inter_replica_net_ip>" (without quotes)
# 2) in clients.txt each row is an IP adress for the machine
# One IP can appear one or more times in the text files. If the same IP appears
# several times, it runs multiple replica/client processes at the same time.

python3 ./gen_inventory.py --prefix 'hotstuff.gen' > nodes.ini
awk '{print $2}' replicas.txt > replicas_inter.txt
python3 ../gen_conf.py --ips replicas_inter.txt --iter 1 --prefix 'hotstuff.gen' --keygen ../../hotstuff-keygen --tls-keygen ../../hotstuff-tls-keygen --block-size 400
