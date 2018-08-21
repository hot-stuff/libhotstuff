import sys
import re
import argparse
from datetime import datetime, timedelta

def str2datetime(s):
    parts = s.split('.')
    dt = datetime.strptime(parts[0], "%Y-%m-%d %H:%M:%S")
    return dt.replace(microsecond=int(parts[1]))


def plot_thr(fname):
    import matplotlib.pyplot as plt
    x = range(len(values))
    y = values
    plt.xlabel(r"time")
    plt.ylabel(r"tx/sec")
    plt.plot(x, y)
    plt.show()
    plt.savefig(fname)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--interval', type=float, default=1, required=False)
    parser.add_argument('--output', type=str, default="hist.png", required=False)
    args = parser.parse_args()
    commit_pat = re.compile('([^[].*) \[hotstuff info\] ([0-9.]*) [0-9.]*$')
    interval = args.interval
    begin_time = None
    next_begin_time = None
    cnt = 0
    lat = 0
    timestamps = []
    values = []
    for line in sys.stdin:
        m = commit_pat.match(line)
        if m:
            timestamps.append(str2datetime(m.group(1)))
            lat += float(m.group(2))
    timestamps.sort()
    for timestamp in timestamps:
        if begin_time and timestamp < next_begin_time:
            cnt += 1
        else:
            if begin_time:
                values.append(cnt)
            begin_time = timestamp
            next_begin_time = begin_time + timedelta(seconds=interval)
            cnt = 1
    values.append(cnt)
    print(values)
    print("lat = {:.3f}ms".format(lat / len(timestamps) * 1e3))
    plot_thr(args.output)
