#!/usr/bin/env python3
"""Where a run's time went (card 513). Usage: tools/idle_report.py <tag>

Reads logs/<tag>-host.log and logs/<tag>.drive. Prints:
- the host command span, the time inside commands, and the gaps between them by the kind of the command before;
- the `objective go` settle total (each go holds its pawns for the settle before its rows);
- the drive's own dead-time and VOID summary lines.
"""
import collections
import re
import statistics
import sys

CMD_RX = re.compile(r"\[\s*(\d+) ms\].*?cmd: (\d+) (.*)")


def host_cmds(path):
    first, last, kind = {}, {}, {}
    with open(path, errors="ignore") as f:
        for line in f:
            m = CMD_RX.match(line)
            if not m:
                continue
            t, n = int(m.group(1)), int(m.group(2))
            first.setdefault(n, t)
            last[n] = t
            kind.setdefault(n, re.sub(r"\d+", "#", m.group(3))[:60])
    return first, last, kind


def main(tag):
    first, last, kind = host_cmds(f"logs/{tag}-host.log")
    order = sorted(first, key=first.get)
    if len(order) < 2:
        sys.exit(f"{tag}: fewer than 2 host commands")
    gaps, count, busy = collections.Counter(), collections.Counter(), 0.0
    for a, b in zip(order, order[1:]):
        busy += (last[a] - first[a]) / 1000
        gaps[kind[a]] += max(0, first[b] - last[a]) / 1000
        count[kind[a]] += 1
    span = (first[order[-1]] - first[order[0]]) / 1000
    print(f"{tag}: host command span {span:.0f} s, inside commands {busy:.0f} s, between {sum(gaps.values()):.0f} s")
    print("between commands, by the command before:")
    for k, v in gaps.most_common(12):
        print(f"  {v:7.0f} s {count[k]:4d}x avg {v / count[k]:5.1f}  {k}")
    goes = [(last[n] - first[n]) / 1000 for n in order if kind[n].startswith("objective go")]
    if goes:
        print(f"objective go: {len(goes)} goes, {sum(goes):.0f} s, median {statistics.median(goes):.2f} s")
    with open(f"logs/{tag}.drive", errors="ignore") as f:
        for line in f:
            if re.search(r"dead time:|dead-wait cap:|scenario finished", line):
                print("drive:", line.rstrip()[:240])


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else sys.exit(__doc__))
