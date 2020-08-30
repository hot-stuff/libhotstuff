import argparse

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Generate configuration files for a run.')
    parser.add_argument('--replicas', type=str, default='replicas.txt',
                        help='text file with all replicas IPs (each row is "<external_ip> <inter_replica_net_ip>", repeat a line if you would like to share the same machine)')
    parser.add_argument('--clients', type=str, default='clients.txt',
                        help='text file with all clients IPs (each row is "<external_ip>")')
    parser.add_argument('--prefix', type=str, default='hotstuff.gen')
    args = parser.parse_args()

    print("[nodes_setup]")
    host_idx_count = {}
    replicas = []
    clients = []
    with open(args.replicas, "r") as rfile:
        for line in rfile:
            (pub_ip, priv_ip) = line.split()
            replicas.append((pub_ip.strip(), priv_ip.strip()))
    with open(args.clients, "r") as cfile:
        for line in cfile:
            clients.append(line.strip())
    machines = sorted(set([pub_ip for (pub_ip, _) in replicas] + clients))
    print("\n".join(machines))
    print("\n[nodes]")
    for (i, (pub_ip, priv_ip)) in enumerate(replicas):
        host_idx = host_idx_count.setdefault(pub_ip, 0)
        host_idx_count[pub_ip] += 1
        print("replica{} ansible_host={} host_idx={} extra_conf={}-sec{}.conf".format(
                i, pub_ip, host_idx, args.prefix, i))

    print("\n[clients]")
    for (i, ip) in enumerate(clients):
        host_idx = host_idx_count.setdefault(pub_ip, 0)
        host_idx_count[pub_ip] += 1
        print("client{} ansible_host={} host_idx={} cid={}".format(i, ip, host_idx, i))
