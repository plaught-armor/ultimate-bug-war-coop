"""Card 138 step 1: from a kis.py listing, walk ReceiveBeginPlay's synchronous control flow and print every statement
that reads the local player's pawn (or controller), with the statements that use the result.

Usage: bpreach.py listing.txt
Stops a path at a latent call (a LatentActionInfo naming ExecuteUbergraph: the rest runs on a later frame) and marks it.
"""
import re
import sys

GETTER = re.compile(r"CallFunc_(GetSTPlayerCharacter|GetPlayerCharacter|GetPlayerPawn|GetPlayerController|"
                    r"GetOwningPlayerPawn|GetSTPlayerController|GetControlledPawn|K2_GetPawn)\w*")
STMT = re.compile(r"^\s+(\d+): (.*)$")
MAX_STEPS = 20000


def parse(path):
    secs, cur = {}, None
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("=== "):
            cur = line[4:].strip()
            secs[cur] = []
            continue
        m = STMT.match(line)
        if m and cur is not None:
            secs[cur].append((int(m.group(1)), m.group(2)))
    return secs


def walk(secs):
    ug = next((n for n in secs if n.startswith("ExecuteUbergraph_")), None)
    index = {n: {o: i for i, (o, _) in enumerate(s)} for n, s in secs.items()}
    local = [n for n in secs if not n.startswith("ExecuteUbergraph_")]
    callre = re.compile(r"(?:^|[ =(])(" + "|".join(re.escape(n) for n in local) + r")\(") if local else None
    work = [("ReceiveBeginPlay", 0, ())]
    seen, hits, latent, steps = set(), [], [], 0
    while work and steps < MAX_STEPS:
        sec, off, flow = work.pop()
        if sec not in secs or off not in index[sec] or (sec, off) in seen:
            continue
        i = index[sec][off]
        flow = list(flow)
        while i < len(secs[sec]) and steps < MAX_STEPS:
            steps += 1
            o, t = secs[sec][i]
            if (sec, o) in seen:
                break
            seen.add((sec, o))
            if GETTER.search(t) and "=" in t.split("(")[0]:
                hits.append((sec, o, t))
            if "name:ExecuteUbergraph" in t:
                latent.append((sec, o, t))
                break
            for m in re.finditer(r"export:(ExecuteUbergraph_\w+)\((\d+)\)", t):
                work.append((m.group(1), int(m.group(2)), ()))
            if callre:
                for m in callre.finditer(t):
                    work.append((m.group(1), secs[m.group(1)][0][0] if secs[m.group(1)] else 0, ()))
            m = re.match(r"(?:if not \(.*\) )?goto (\d+)$", t)
            if m:
                work.append((sec, int(m.group(1)), tuple(flow)))
                if t.startswith("goto"):
                    break
            if t.startswith("push_flow "):
                flow.append(int(t.split()[1]))
            elif t == "pop_flow":
                if flow:
                    work.append((sec, flow.pop(), tuple(flow)))
                break
            elif t.startswith("pop_flow_if_not") and flow:
                work.append((sec, flow[-1], tuple(flow[:-1])))
            if t.startswith("return") or t == "END" or t.startswith("computed_goto"):
                break
            i += 1
    return ug, hits, latent, secs


def uses(secs, sec, off, var, n=4):
    out = []
    for o, t in secs[sec]:
        if o > off and var in t:
            out.append(f"{o}: {t[:150]}")
            if len(out) >= n:
                break
    return out


def main():
    secs = parse(sys.argv[1])
    if "ReceiveBeginPlay" not in secs:
        print("  no ReceiveBeginPlay")
        return 0
    _, hits, latent, secs = walk(secs)
    for sec, o, t in sorted(set(hits)):
        var = t.split(" = ")[0].strip()
        print(f"  HIT {sec}@{o}: {t[:160]}")
        for u in uses(secs, sec, o, var):
            print(f"      use {u}")
    for sec, o, t in latent:
        print(f"  LATENT {sec}@{o}: {t[:120]}")
    if not hits:
        print("  no pawn read on the synchronous BeginPlay path")
    return 0


if __name__ == "__main__":
    sys.exit(main())
