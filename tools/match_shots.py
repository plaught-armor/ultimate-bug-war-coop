#!/usr/bin/env python3
"""Card 155: pair the client's own shots with the host's server-copy shots.

Usage: tools/match_shots.py HOST_LOG CLIENT_LOG

Reads the playlog clock lines (host and client clocks, offset = client t - host t) and maps each client shot
("playlog: client t=T shot c seq=N ...") to host time with the offset of the nearest clock exchange at or before it
(piecewise; the first exchange for shots before it). Each client shot pairs with the nearest unused host copy shot
("shot s") whose host time is within -400..+1500 ms of the mapped time (the playtest finding 16 window) and whose
origin is within 300 uu. Prints the matched shots, the dropped ones (weapon, ADS, ms since reload) and the pellet
count mismatches (k differs), then a totals line.
"""

import bisect
import math
import re
import sys

WINDOW_LO_MS = -400
WINDOW_HI_MS = 1500
MAX_UU = 300.0

CLOCK_RE = re.compile(r"playlog: host t=\d+ clock (\d+) \([^)]*\): host t (\d+) client t (\d+) offset (-?\d+) ms rtt (\d+) ms")
SHOT_RE = re.compile(
    r"playlog: (host|client) t=(\d+) shot ([chs]) seq=(\d+) w=(\S*) k=(\d+) ads=(-?\d+) rl=(-?\d+) "
    r"at=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+) p=(-?[\d.]+) y=(-?[\d.]+)")


def read_lines(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read().splitlines()


def parse_clock(lines):
    """[(host_t, offset_ms)] sorted by host time, from the host's clock lines."""
    rows = []
    for line in lines:
        m = CLOCK_RE.search(line)
        if m:
            rows.append((int(m.group(2)), int(m.group(4)), int(m.group(5))))
    rows.sort()
    return rows


def parse_shots(lines, side):
    shots = []
    for line in lines:
        m = SHOT_RE.search(line)
        if m and m.group(3) == side:
            shots.append({
                "t": int(m.group(2)), "seq": int(m.group(4)), "w": m.group(5), "k": int(m.group(6)),
                "ads": int(m.group(7)), "rl": int(m.group(8)),
                "at": (float(m.group(9)), float(m.group(10)), float(m.group(11))),
            })
    return shots


def offset_at(clock, client_t):
    """The offset of the last exchange at or before client_t (client clock), else the first."""
    keys = [h + off for h, off, _ in clock]  # each exchange's time on the client clock
    i = bisect.bisect_right(keys, client_t) - 1
    return clock[max(i, 0)][1]


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    host_lines = read_lines(argv[1])
    client_lines = read_lines(argv[2])
    clock = parse_clock(host_lines)
    if not clock:
        print("no host clock lines: cannot map client time", file=sys.stderr)
        return 1
    client = parse_shots(client_lines, "c")
    copies = parse_shots(host_lines, "s")
    copy_t = [s["t"] for s in copies]
    used = [False] * len(copies)
    matched, dropped, pellets = [], [], []
    for c in client:
        th = c["t"] - offset_at(clock, c["t"])
        lo = bisect.bisect_left(copy_t, th + WINDOW_LO_MS)
        hi = bisect.bisect_right(copy_t, th + WINDOW_HI_MS)
        best, best_dt = -1, None
        for i in range(lo, hi):
            if used[i] or math.dist(copies[i]["at"], c["at"]) > MAX_UU:
                continue
            dt = copies[i]["t"] - th
            if best_dt is None or abs(dt) < abs(best_dt):
                best, best_dt = i, dt
        if best < 0:
            dropped.append(c)
            continue
        used[best] = True
        h = copies[best]
        matched.append((c, h, best_dt))
        if h["k"] != c["k"]:
            pellets.append((c, h))
    rtts = [r for _, _, r in clock]
    print(f"clock: {len(clock)} exchanges, offset {clock[0][1]}..{clock[-1][1]} ms, rtt max {max(rtts)} ms")
    print(f"{'seq':>5} {'client t':>12} {'host dt':>8} {'weapon':<40} {'k c/h':>6} {'ads':>4} {'rl':>7}")
    for c, h, dt in matched:
        print(f"{c['seq']:>5} {c['t']:>12} {dt:>+8} {c['w']:<40} {c['k']:>3}/{h['k']:<2} {c['ads']:>4} {c['rl']:>7}")
    print(f"dropped ({len(dropped)}):")
    for c in dropped:
        print(f"  seq {c['seq']} t {c['t']} w {c['w']} k {c['k']} ads {c['ads']} rl {c['rl']} at {c['at']}")
    print(f"pellet mismatches ({len(pellets)}):")
    for c, h in pellets:
        print(f"  seq {c['seq']} w {c['w']}: client k {c['k']}, host k {h['k']} ({h['w']})")
    print(f"totals: client shots {len(client)}, host copy shots {len(copies)}, matched {len(matched)}, "
          f"dropped {len(dropped)}, pellet mismatches {len(pellets)}, host copies unpaired {used.count(False)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
