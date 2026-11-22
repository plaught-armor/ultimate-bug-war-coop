#!/usr/bin/env python3
"""Card 527: whole-frame self-time profile of the game from a perf sample dump.

Wine copies the PE image into anonymous memory (the .text file offset 0x600 differs from its RVA 0x1000), so perf
cannot name exe samples itself. This tool maps each sample IP to the nearest preceding PDB public function.

Record (host or client game process, during the peak rounds, about 20 s):
    perf record -F 499 -p <pid> -o <out>.data -- sleep 20
    perf script -i <out>.data -F comm,tid,ip,dso > <out>.txt
Map:
    tools/perf_frame.py <out>.txt <exe base hex from the log "hooks: exe base" row> [--top 40]
    tools/perf_frame.py <out>.txt --log logs/<tag>-host.log [--mod-map ...]   (bases read from the log)
The exe base can move between runs (c263L351: 00006FFFE99E0000, every earlier run 00006FFFF39F0000), so read it
from the same run's log every time; --log does that for the exe and the mod dll.

Output, per thread name: sample count and share, then the top functions and top classes by self samples.
Samples outside the exe go to their DSO name (wine, driver, kernel, the mod dll).

Callers (no frame pointers in the shipping exe, so no stack walk): record with `-b` (AMD LBRv2, 16 newest branches)
and dump with `-F comm,tid,ip,dso,brstack`. Then `--callers <text>` attributes each sample whose function name holds
<text> to the function of the newest branch that lands on that function's first instruction (a call or tail jump)
from outside it. Returns from callees land mid-function and are not entries. Samples whose 16 branches hold no
entry are counted as "no entry seen". The top call sites are also printed as exe RVAs, so a caller inside a
function with no public symbol can be found by disassembly. With `dso` in -F, perf prints "0xA (dso)/0xB (dso)/";
the branch pattern accepts that form too.

Mod samples: Wine maps the mod dll anonymously as well, so its samples land in "[perf-<pid>.map]". Pass the mod
base (the log "proxy module" address) with --mod-base and the link map kept beside the build
(build-b5/dwmapi-bNNN-<hash>.map, from the /MAP link option) with --mod-map. Those samples are then named after the
nearest preceding mod symbol (its namespace, ubw::, marks it). The map must come from the same build as the dll that ran.
"""
import argparse
import bisect
import collections
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PUBLICS = REPO / "build/publics.txt"
PDB = REPO / "game/StarshipTroopers_UBW/Binaries/Win64/StarshipTroopers_UBW-Win64-Shipping.pdb"
TEXT_SEG = 1
TEXT_VA = 0x1000
IMAGE_SIZE = 0x9DDE000  # SizeOfImage from the hooks row; samples past it are not the exe
RE_PUB = re.compile(r"^\s*\d+ \| S_PUB32 \[size = \d+\] `(.*)`\s*$")
RE_ADDR = re.compile(r"^\s*flags = (.*), addr = (\d{4}):(\d+)\s*$")
RE_SAMPLE = re.compile(r"^\s*(.+?)\s+(\d+)\s+([0-9a-f]+)\s+\(([^)]*)\)(.*)$")
RE_MAP = re.compile(r"^\s*0001:[0-9a-f]+\s+(\S+)\s+([0-9a-f]{16})\s")
MOD_PREFERRED = 0x180000000
RE_BRANCH = re.compile(r"0x([0-9a-f]+)(?: \([^)]*\))?/0x([0-9a-f]+)(?: \([^)]*\))?/")


def load_publics(path):
    """Return (sorted rvas, names) for every function public in .text."""
    rows = []
    pending = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if pending is not None:
                m = RE_ADDR.match(line)
                if m and "function" in m.group(1) and int(m.group(2)) == TEXT_SEG:
                    rows.append((TEXT_VA + int(m.group(3)), pending))
                pending = None
                continue
            m = RE_PUB.match(line)
            if m:
                pending = m.group(1)
    if not rows:
        sys.exit(f"perf_frame: no .text function publics in {path}")
    rows.sort()
    return [r for r, _ in rows], [n for _, n in rows]


def load_mod_map(path):
    """Return (sorted rvas, names) for the .text symbols of an lld /MAP file (public and static)."""
    rows = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = RE_MAP.match(line)
            if m:
                rows.append((int(m.group(2), 16) - MOD_PREFERRED, m.group(1)))
    if not rows:
        sys.exit(f"perf_frame: no .text symbols in {path}")
    rows.sort()
    return [r for r, _ in rows], [n for _, n in rows]


def demangle(names):
    """Batch-demangle MSVC names with llvm-undname; keep the input on failure."""
    names = list(names)
    if not names:
        return {}
    res = subprocess.run(["llvm-undname"], input="\n".join(names) + "\n", capture_output=True, text=True, check=False)
    out = {n: n for n in names}
    lines = [l.strip() for l in res.stdout.splitlines() if l.strip()]
    # llvm-undname echoes each input line, then prints the result line. It exits nonzero when any one name fails,
    # so pair echo and result lines instead of trusting the exit code; failed names keep their mangled form.
    wanted = set(names)
    for i in range(len(lines) - 1):
        if lines[i] in wanted and not lines[i + 1].startswith("error") and lines[i + 1] not in wanted:
            out[lines[i]] = lines[i + 1]
    return out


def class_of(dem):
    """UE class or namespace that owns a demangled function, else the whole name."""
    sig = re.split(r"\b__(?:cdecl|fastcall|thiscall|stdcall)\b", dem)[-1].split("(")[0].strip()
    if "::" not in sig:
        return dem[:80]
    return sig.rsplit("::", 1)[0].split("<")[0].strip()


def caller_of(entry, branches, func_of, base):
    """(caller function, call site RVA) of the newest branch that lands on `entry` (an absolute address) from another
    function (brstack is newest first); ("(no entry seen)", None) when none does."""
    for m in RE_BRANCH.finditer(branches):
        src_addr, dst_addr = int(m.group(1), 16), int(m.group(2), 16)
        if dst_addr != entry:
            continue
        src = func_of(src_addr)
        if src == func_of(entry):
            continue
        return (src if src is not None else "[outside exe]"), src_addr - base
    return "(no entry seen)", None


def read_log_bases(path):
    """The exe base ("hooks: exe base") and mod dll base ("proxy module") rows of a game log."""
    out = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.search(r"hooks: exe base ([0-9A-Fa-f]+)", line)
            if m and "exe" not in out:
                out["exe"] = m.group(1)
            m = re.search(r"proxy module ([0-9A-Fa-f]+)", line)
            if m and "mod" not in out:
                out["mod"] = m.group(1)
            if len(out) == 2:
                break
    print(f"perf_frame: bases from {path}: exe {out.get('exe')}, mod {out.get('mod')}")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("samples")
    ap.add_argument("base", nargs="?", help="exe base, hex (or --log)")
    ap.add_argument("--log", help="the same run's game log: exe and mod bases are read from it")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--mod-base", help="mod dll base, hex (log 'proxy module' row)")
    ap.add_argument("--mod-map", help="lld /MAP file of the same mod build")
    ap.add_argument("--callers", action="append", default=[], help="name substring; repeatable; needs brstack")
    args = ap.parse_args()
    if args.log:
        logged = read_log_bases(args.log)
        args.base = args.base or logged.get("exe")
        args.mod_base = args.mod_base or (logged.get("mod") if args.mod_map else None)
    if args.base is None:
        sys.exit("perf_frame: give the exe base or --log")
    base = int(args.base, 16)
    rvas, names = load_publics(PUBLICS)
    if (args.mod_base is None) != (args.mod_map is None):
        sys.exit("perf_frame: --mod-base and --mod-map go together")
    mod_base = int(args.mod_base, 16) if args.mod_base else None
    mod_rvas, mod_names = load_mod_map(args.mod_map) if args.mod_map else ([], [])
    mod_end = mod_rvas[-1] + 0x10000 if mod_rvas else 0  # past the last symbol: not the mod

    def func_of(addr):
        rva = addr - base
        if not 0 <= rva < IMAGE_SIZE:
            return None
        i = bisect.bisect_right(rvas, rva) - 1
        return names[i] if i >= 0 else f"exe+{rva:#x}"

    callers = {c: collections.Counter() for c in args.callers}
    sites = {c: collections.Counter() for c in args.callers}
    per_thread = collections.Counter()
    funcs = collections.defaultdict(collections.Counter)
    total = 0
    with open(args.samples, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = RE_SAMPLE.match(line)
            if not m:
                continue
            comm, ip, dso = m.group(1).strip(), int(m.group(3), 16), m.group(4)
            total += 1
            per_thread[comm] += 1
            rva = ip - base
            if 0 <= rva < IMAGE_SIZE:
                i = bisect.bisect_right(rvas, rva) - 1
                key = names[i] if i >= 0 else f"exe+{rva:#x}"
            elif mod_base is not None and 0 <= ip - mod_base < mod_end:
                j = bisect.bisect_right(mod_rvas, ip - mod_base) - 1
                key = mod_names[j] if j >= 0 else f"mod+{ip - mod_base:#x}"
            else:
                key = f"[{Path(dso).name or dso}]"
            funcs[comm][key] += 1
            for want, counter in callers.items():
                if want in key and 0 <= rva < IMAGE_SIZE and i >= 0:
                    who, site = caller_of(base + rvas[i], m.group(5), func_of, base)
                    counter[who] += 1
                    if site is not None:
                        sites[want][site] += 1
    if total == 0:
        sys.exit("perf_frame: no samples parsed")

    wanted = {k for c in funcs.values() for k, _ in c.most_common(args.top) if not k.startswith("[")}
    for counter in callers.values():
        wanted.update(k for k in counter if not k.startswith(("[", "(")))
    for c in funcs.values():
        wanted.update(k for k in c if not k.startswith("["))
    dem = demangle(sorted(wanted))
    print(f"samples {total}")
    for comm, n in per_thread.most_common(12):
        print(f"\n== thread {comm}: {n} samples ({100.0 * n / total:.1f}%)")
        classes = collections.Counter()
        for k, v in funcs[comm].items():
            classes[k if k.startswith("[") else class_of(dem.get(k, k))] += v
        print("  top classes:")
        for k, v in classes.most_common(args.top // 2):
            print(f"    {100.0 * v / n:5.1f}%  {v:6d}  {k}")
        print("  top functions:")
        for k, v in funcs[comm].most_common(args.top):
            print(f"    {100.0 * v / n:5.1f}%  {v:6d}  {dem.get(k, k)[:160]}")
    for want, counter in callers.items():
        n = sum(counter.values())
        print(f"\n== callers of *{want}*: {n} samples")
        for k, v in counter.most_common(args.top):
            print(f"    {100.0 * v / max(n, 1):5.1f}%  {v:6d}  {dem.get(k, k)[:160]}")
        print("  top call sites (exe RVA of the branch source):")
        for k, v in sites[want].most_common(args.top // 2):
            print(f"    {100.0 * v / max(n, 1):5.1f}%  {v:6d}  exe+{k:#x}")


if __name__ == "__main__":
    main()
