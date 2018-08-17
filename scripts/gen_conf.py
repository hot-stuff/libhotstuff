import os, re
import subprocess
import itertools
import argparse

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Generate configuration file for a batch of replicas')
    parser.add_argument('--prefix', type=str, default='hotstuff.gen')
    parser.add_argument('--iplist', type=str, default=None)
    parser.add_argument('--iter', type=int, default=10)
    parser.add_argument('--pport', type=int, default=10000)
    parser.add_argument('--cport', type=int, default=20000)
    parser.add_argument('--keygen', type=str, default='./hotstuff-keygen')
    args = parser.parse_args()


    if args.iplist is None:
        ips = ['127.0.0.1']
    else:
        ips = [l.strip() for l in open(args.iplist, 'r').readlines()]
    prefix = args.prefix
    iter = args.iter
    base_pport = args.pport
    base_cport = args.cport
    keygen_bin= args.keygen

    main_conf = open("{}.conf".format(prefix), 'w')
    replicas = ["{}:{};{}".format(ip, base_pport + i, base_cport + i)
                for ip in ips
                for i in range(iter)]
    p = subprocess.Popen([keygen_bin, '--num', str(len(replicas))],
                        stdout=subprocess.PIPE, stderr=open(os.devnull, 'w'))
    keys = [[t[4:] for t in l.decode('ascii').split()] for l in p.stdout]
    for r in zip(replicas, keys, itertools.count(0)):
        main_conf.write("replica = {}, {}\n".format(r[0], r[1][0]))
        r_conf = open("{}-sec{}.conf".format(prefix, r[2]), 'w')
        r_conf.write("privkey = {}\n".format(r[1][1]))
        r_conf.write("idx = {}\n".format(r[2]))
