#!/usr/bin/env python3
"""Card 132 step 2e: pair the host's effect spawns with the client's.

Reads the "fx audit:" lines UBWCOOP_C132_FX=1 writes on both sides:
  card132: <side> t=<ms> fx audit: <what> <asset> at (x, y, z) owner <name> (<class>) frames <chain>
A host spawn pairs with a client spawn of the same asset within --dist uu and --ms of it (both processes run on one
machine, so GetTickCount64 is one clock). A spawn at (0, 0, 0) (a cue, no location) pairs on asset and time alone.

Prints one row per asset: host count, client count, paired count, and the host spawns no client spawn matched (gaps),
with their call chain, so each gap names the host code that made it. Exit status 1 when a gap is found.

The "extra" column counts client spawns no host spawn matched: a client effect the host did not make (a double, or a
client-only effect). --window N prints each asset's host and client counts per N-second window of the host's clock.

Usage: fx_pair.py logs/hbNN-host.log logs/hbNN-client.log [--dist 300] [--ms 1000] [--ignore REGEX] [--window 30]

Card 136 (--c136): pair by event id instead. The host's "card136: host fx <id> t=<ms> <point> <asset> ..." lines and the
client's "card136: client fx <id> <point> <asset> [owner <class>] <result>" lines (both UBWCOOP_C136_LOG=1). Per asset and point: host
events, client rows, client rows whose result starts "played", the other results, and host ids no client row has
(filtered for that client, local only, first person not sent, or lost). Exit status 1 when a client row has no host id.
"""

import argparse
import re
import sys
from collections import defaultdict

LINE = re.compile(
    r"card132: (host|client)\S* t=(\d+) fx audit: (.+?) (\S+) at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\) "
    r"owner (.*?) frames (.*)$")


def read(path, side):
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for text in f:
            m = LINE.search(text)
            if m is None or m.group(1) != side:
                continue
            out.append({
                "t": int(m.group(2)), "what": m.group(3), "asset": m.group(4),
                "at": (float(m.group(5)), float(m.group(6)), float(m.group(7))),
                "owner": m.group(8), "frames": m.group(9).strip(),
            })
    return out


def near(a, b, dist):
    if a == (0.0, 0.0, 0.0) or b == (0.0, 0.0, 0.0):
        return True
    return sum((x - y) ** 2 for x, y in zip(a, b)) <= dist * dist


def pair(host, client, dist, ms):
    by_asset = defaultdict(list)
    for i, c in enumerate(client):
        by_asset[c["asset"]].append(i)
    used = set()
    rows = defaultdict(lambda: {"host": 0, "client": 0, "paired": 0, "gaps": []})
    for c in client:
        rows[c["asset"]]["client"] += 1
    for h in host:
        row = rows[h["asset"]]
        row["host"] += 1
        best = None
        for i in by_asset.get(h["asset"], ()):
            c = client[i]
            dt = abs(c["t"] - h["t"])
            if i in used or dt > ms or not near(h["at"], c["at"], dist):
                continue
            if best is None or dt < abs(client[best]["t"] - h["t"]):
                best = i
        if best is None:
            row["gaps"].append(h)
        else:
            used.add(best)
            row["paired"] += 1
    return rows


def print_windows(host, client, width_ms):
    """Per asset, "host/client" counts in each window from the first spawn on either side."""
    start = min([e["t"] for e in host + client] or [0])
    counts = defaultdict(lambda: defaultdict(lambda: [0, 0]))
    last = 0
    for side, events in ((0, host), (1, client)):
        for e in events:
            w = (e["t"] - start) // width_ms
            counts[e["asset"]][w][side] += 1
            last = max(last, w)
    print(f"per-window counts host/client, {width_ms // 1000} s windows from t={start}")
    for asset in sorted(counts):
        cells = " ".join(f"{counts[asset][w][0]}/{counts[asset][w][1]}" for w in range(last + 1))
        print(f"  {asset:<56} {cells}")


C136_HOST = re.compile(r"card136: host fx (\d+) t=(\d+) (\S+) (\S+) at ")
C136_CLIENT = re.compile(r"card136: client fx (\d+) (\S+) (\S+) (?:owner (\S+) )?(.*)$")


def c136_pair(host_log, client_log, ignore):
    host = {}
    with open(host_log, encoding="utf-8", errors="replace") as f:
        for text in f:
            m = C136_HOST.search(text)
            if m and not (ignore and ignore.search(m.group(4))):
                host[int(m.group(1))] = (m.group(3), m.group(4))
    rows = defaultdict(lambda: {"host": 0, "client": 0, "played": 0, "other": defaultdict(int), "unseen": []})
    for i, (point, asset) in host.items():
        rows[(point, asset)]["host"] += 1
    seen = set()
    orphans = 0
    with open(client_log, encoding="utf-8", errors="replace") as f:
        for text in f:
            m = C136_CLIENT.search(text)
            if m is None or (ignore and ignore.search(m.group(3))):
                continue
            i = int(m.group(1))
            key = host.get(i, (m.group(2), m.group(3)))
            orphans += i not in host
            seen.add(i)
            r = rows[key]
            r["client"] += 1
            result = m.group(5).strip()
            if result.startswith("played"):
                r["played"] += 1
            if result != "played":
                r["other"][result] += 1
    for i, key in host.items():
        if i not in seen:
            rows[key]["unseen"].append(i)
    print(f"{'point':<26} {'asset':<56} {'host':>5} {'client':>6} {'played':>6} {'unseen':>6}  other results")
    for key in sorted(rows, key=lambda k: (-rows[k]["host"], k)):
        r = rows[key]
        other = ", ".join(f"{n} {t}" for t, n in sorted(r["other"].items()))
        print(f"{key[0]:<26} {key[1]:<56} {r['host']:>5} {r['client']:>6} {r['played']:>6} {len(r['unseen']):>6}  "
              f"{other}")
    print(f"host events {len(host)}, client rows {sum(r['client'] for r in rows.values())}, "
          f"client rows with no host id {orphans}")
    return 1 if orphans else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host_log")
    ap.add_argument("client_log")
    ap.add_argument("--dist", type=float, default=300.0)
    ap.add_argument("--ms", type=int, default=1000)
    ap.add_argument("--window", type=int, default=0, help="seconds per window for the per-asset count table (0: none)")
    ap.add_argument("--ignore", default=None, help="regex of assets to leave out (local-only UI sounds and the like)")
    ap.add_argument("--c136", action="store_true", help="pair card 136 host and client fx lines by event id")
    args = ap.parse_args()
    ignore = re.compile(args.ignore) if args.ignore else None
    if args.c136:
        return c136_pair(args.host_log, args.client_log, ignore)
    host = [e for e in read(args.host_log, "host") if not (ignore and ignore.search(e["asset"]))]
    client = [e for e in read(args.client_log, "client") if not (ignore and ignore.search(e["asset"]))]
    rows = pair(host, client, args.dist, args.ms)
    gaps = 0
    print(f"{'asset':<60} {'host':>5} {'client':>6} {'paired':>6} {'gaps':>5} {'extra':>5}")
    for asset in sorted(rows, key=lambda a: (-len(rows[a]["gaps"]), a)):
        r = rows[asset]
        gaps += len(r["gaps"])
        mark = "GAP " if r["gaps"] else "    "
        print(f"{mark}{asset:<56} {r['host']:>5} {r['client']:>6} {r['paired']:>6} {len(r['gaps']):>5} "
              f"{r['client'] - r['paired']:>5}")
    for asset in sorted(rows):
        for h in rows[asset]["gaps"][:3]:
            x, y, z = h["at"]
            print(f"  gap: host t={h['t']} {h['what']} {asset} at ({x:.0f}, {y:.0f}, {z:.0f}) owner {h['owner']} "
                  f"frames {h['frames']}")
    if args.window > 0:
        print_windows(host, client, args.window * 1000)
    print(f"host spawns {len(host)}, client spawns {len(client)}, unpaired host spawns {gaps}")
    return 1 if gaps else 0


if __name__ == "__main__":
    sys.exit(main())
