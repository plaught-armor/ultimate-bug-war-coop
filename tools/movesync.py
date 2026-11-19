#!/usr/bin/env python3
"""Join the host and client 'movesync:' probe lines (enemies.cpp move_probe) on NPC guid.

Both games run on one machine, so their GetTickCount64 stamps share a clock. For each guid seen on both sides this
prints the largest host-to-client distance, when the client copy first fell more than FAR uu behind, and how long the
client copy sat still while the host copy moved.

Usage: tools/movesync.py logs/<tag>-host.log logs/<tag>-client.log
       tools/movesync.py logs/<tag>-host.log        (solo run: host corpses only)
"""
import math
import re
import sys

FAR = 500.0      # uu: farther than this is a stale client copy
STILL = 5.0      # uu: a client copy that moved less than this between samples is standing still
MOVING = 100.0   # uu: a host copy that moved more than this between samples is moving
LINE = re.compile(r"movesync: (host|client) t=(\d+) n=\d+:(.*)")


PAWN = 0  # key for the side's own pawn track ("pawn x y z" rows); NPC guid 0 rows are skipped, so it is free


def read(path):
    """guid -> list of (t, (x, y, z), flags, health, drawn location (x, y, z; from build 132 hb48 the sprite renderer when drawn, else the ragdoll body 0, else the mesh component), movement mode, capsule collision, replicated
    location (x, y, z), state bits (field 15: 1 montage root motion on, 2 root-motion montage playing, 4 mesh hidden, 8 animator showing the static mesh, 16 mesh not visible, 32 sprite renderer drawn, 64 drawn location read from the ragdoll body 0; fields 16-18 the sprite renderer location)), in time order; key PAWN -> [(t, (x, y, z))]. Rows without the later fields carry None there."""
    out = {}
    with open(path, errors="replace") as f:
        for line in f:
            m = LINE.search(line)
            if not m:
                continue
            t = int(m.group(2))
            for row in m.group(3).split(";"):
                parts = row.split()
                if len(parts) == 4 and parts[0] == "pawn":
                    out.setdefault(PAWN, []).append((t, tuple(float(v) for v in parts[1:4])))
                    continue
                if len(parts) not in (6, 9, 11, 14, 15, 18):
                    continue
                guid = int(parts[0], 16)
                loc = tuple(float(v) for v in parts[1:4])
                mesh = tuple(float(v) for v in parts[6:9]) if len(parts) >= 9 else loc
                health = float(parts[5])
                mode = int(parts[9]) if len(parts) >= 11 else None
                coll = int(parts[10]) if len(parts) >= 11 else None
                rep = tuple(float(v) for v in parts[11:14]) if len(parts) >= 14 else None
                bits = int(parts[14]) if len(parts) >= 15 else None
                if guid == 0:
                    # A solo host assigns no guids. A corpse's capsule stays put, so dead rows key on it instead.
                    if health > 0:
                        continue
                    guid = ("corpse at", tuple(round(v / 10) * 10 for v in loc))
                out.setdefault(guid, []).append((t, loc, parts[4], health, mesh, mode, coll, rep, bits))
    return out


def nearest(samples, t):
    best = None
    for s in samples:
        if best is None or abs(s[0] - t) < abs(best[0] - t):
            best = s
    return best


def solo(host):
    """Solo host: how far each corpse's mesh (the ragdoll) sits from its capsule, the vanilla baseline."""
    far = 0
    for g in sorted(set(host) - {PAWN}, key=str):
        dead = [h for h in host[g] if h[3] <= 0]
        if not dead:
            continue
        off = max(math.dist(h[4], h[1]) for h in dead)
        far += 1 if off > FAR else 0
        name = f"{g:#x}" if isinstance(g, int) else f"at {g[1]}"
        print(f"corpse {name}: mesh off capsule host {off:.0f}; last host mesh {tuple(round(v) for v in dead[-1][4])}")
    print(f"corpses off capsule (> {FAR:.0f} uu): {far}")


def main():
    if len(sys.argv) == 2:
        solo(read(sys.argv[1]))
        return
    host, client = read(sys.argv[1]), read(sys.argv[2])
    both = sorted(g for g in set(host) & set(client) if isinstance(g, int) and g != PAWN)
    print(f"guids: host {len(host)}, client {len(client)}, both {len(both)}")
    stale = 0
    for g in both:
        worst, first_far, still_ms = 0.0, None, 0
        prev_h = prev_c = None
        for h in host[g]:
            c = nearest(client[g], h[0])
            if c is None or abs(c[0] - h[0]) > 600:
                continue
            d = math.dist(h[1], c[1])
            if h[3] > 0 and d > worst:
                worst = d
            if h[3] > 0 and d > FAR and first_far is None:
                first_far = h[0]
            if prev_h is not None and prev_c is not None and h[3] > 0:
                if math.dist(prev_h[1], h[1]) > MOVING and math.dist(prev_c[1], c[1]) < STILL:
                    still_ms += h[0] - prev_h[0]
            prev_h, prev_c = h, c
        stale += 1 if first_far is not None else 0
        t0 = host[g][0][0]
        print(f"guid {g:#x}: host samples {len(host[g])} client {len(client[g])}, flags host {host[g][0][2]} client "
              f"{client[g][0][2]}, worst {worst:.0f} uu, first far "
              f"{'-' if first_far is None else f'+{first_far - t0} ms'}, client still while host moved {still_ms} ms"
              f"{replicated(host[g], client[g]) if first_far is not None else ''}")
    print(f"stale guids (> {FAR:.0f} uu while alive): {stale} of {len(both)}")
    corpses(host, client, both)
    modes(host, client, both)
    statics(host, client, both)


def replicated(host_samples, client_samples):
    """A stale copy: the client's ReplicatedMovement.Location against the host's location and the client's actor.
    Near the host but far from the actor: updates arrive and are not applied. Near the actor: they stopped arriving."""
    worst_rep_host, worst_rep_actor, changes, prev = 0.0, 0.0, 0, None
    for c in client_samples:
        if c[7] is None or c[3] <= 0:
            continue
        h = nearest(host_samples, c[0])
        if h is not None and abs(h[0] - c[0]) <= 600:
            worst_rep_host = max(worst_rep_host, math.dist(h[1], c[7]))
        worst_rep_actor = max(worst_rep_actor, math.dist(c[1], c[7]))
        changes += 1 if prev is not None and math.dist(prev, c[7]) > STILL else 0
        prev = c[7]
    if prev is None:
        return "; no replicated location in the client rows"
    return (f"; client ReplicatedMovement.Location worst {worst_rep_host:.0f} uu from the host, {worst_rep_actor:.0f} "
            f"from the client actor, changed in {changes} samples")


def modes(host, client, both):
    """Alive NPCs: CharacterMovement mode and capsule collision per side, and how long a client copy read Falling (3)
    while the host's did not."""
    for g in both:
        alive = [h for h in host[g] if h[3] > 0 and h[5] is not None]
        if not alive:
            continue
        falling_ms, first = 0, None
        hm, cm, cc = set(), set(), set()
        for h in alive:
            c = nearest(client[g], h[0])
            if c is None or abs(c[0] - h[0]) > 600 or c[5] is None:
                continue
            hm.add(h[5])
            cm.add(c[5])
            cc.add(c[6])
            if c[5] == 3 and h[5] != 3:
                falling_ms += 1000
                first = h[0] - alive[0][0] if first is None else first
        print(f"modes {g:#x}: host {sorted(hm)} client {sorted(cm)}, client capsule collision {sorted(cc)}, client "
              f"Falling while host not ~{falling_ms} ms (first at +{first if first is not None else '-'} ms)")


def statics(host, client, both):
    """Field 15 bit 3 (animator showing the static mesh) per side while the host's NPC is alive. Each side picks the
    static mesh from its own significance (its own pawn's distance), so a mismatch prints the client pawn distance."""
    for g in both:
        alive = [h for h in host[g] if h[3] > 0 and h[8] is not None]
        if not alive:
            continue
        host_ms = client_ms = only_client_ms = 0
        first, away = None, "-"
        for h in alive:
            c = nearest(client[g], h[0])
            if c is None or abs(c[0] - h[0]) > 600 or c[8] is None:
                continue
            host_ms += 1000 if h[8] & 8 else 0
            client_ms += 1000 if c[8] & 8 else 0
            if c[8] & 8 and not h[8] & 8:
                only_client_ms += 1000
                if first is None:
                    first, away = h[0] - alive[0][0], pawn_away(client, c)
        print(f"static {g:#x}: alive and static ~{host_ms} ms on the host, ~{client_ms} ms on the client; client only "
              f"~{only_client_ms} ms (first at +{first if first is not None else '-'} ms, client pawn {away} uu away)")


def pawn_away(side, sample):
    """Distance from a sample's capsule to that side's own pawn at the nearest time, or '-' without pawn rows."""
    pawn = side.get(PAWN)
    if not pawn:
        return "-"
    return f"{math.dist(nearest(pawn, sample[0])[1], sample[1]):.0f}"


def corpses(host, client, both):
    """Dead NPCs: how far each side's mesh (the ragdoll) sits from its capsule, and host mesh to client mesh."""
    apart = 0
    for g in both:
        dead = [h for h in host[g] if h[3] <= 0]
        if not dead:
            continue
        worst, host_off, client_off, hidden, fell = 0.0, 0.0, 0.0, 0, 0
        for h in dead:
            c = nearest(client[g], h[0])
            if c is None or abs(c[0] - h[0]) > 600:
                continue
            # A mesh hidden on both sides (sprite-rendered trooper, bug in static-mesh mode) is not what either
            # player sees. Hidden on one side only stays a mismatch.
            if h[8] is not None and h[8] & 4 and c[8] is not None and c[8] & 4:
                hidden += 1
                continue
            # The vanilla fall (a bug that died in static-mesh mode, shown again with no floor): fallen on both
            # sides is a match.
            if h[1][2] - h[4][2] > FAR and c[1][2] - c[4][2] > FAR:
                fell += 1
                continue
            worst = max(worst, math.dist(h[4], c[4]))
            host_off = max(host_off, math.dist(h[4], h[1]))
            client_off = max(client_off, math.dist(c[4], c[1]))
        apart += 1 if worst > FAR else 0
        print(f"corpse {g:#x}: host mesh to client mesh worst {worst:.0f} uu; mesh off capsule host {host_off:.0f} "
              f"client {client_off:.0f}; hidden on both {hidden} samples, fallen on both {fell}; last host mesh {tuple(round(v) for v in dead[-1][4])}; at death, own pawn "
              f"{pawn_away(host, dead[0])} uu away on the host, {pawn_away(client, dead[0])} on the client")
    print(f"corpses apart (> {FAR:.0f} uu mesh to mesh): {apart}")


if __name__ == "__main__":
    main()
