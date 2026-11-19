#!/usr/bin/env python3
"""Static sweep: call sites whose LocalPlayer / LocalPlayer-subsystem result may be used unchecked.

Usage: python3 -I tools/find_lp_derefs.py [--publics FILE | --pdb PDB] [--exe EXE]
                                          [--window N] [--all]
                                          [--targets lp|authority|nav|playerinput]

--targets lp (default; card 004), found by name in the PDB publics:
  - APlayerController::GetLocalPlayer
  - every ULocalPlayer::GetSubsystem<T> instantiation
  - FSubsystemCollectionBase::GetSubsystemInternal, only where the collection argument is
    built as `lea <reg>, [<base> + 0x138]` (ULocalPlayer's subsystem collection, the offset
    the GetSubsystem<T> instantiations use) within the 8 instructions before the call

--targets authority (card 005): getters whose result exists only on the server, so a null
return is the normal case on a client:
  - UGameplayStatics::GetGameMode
  - USTCommonUtilityLibrary::GetSTGameplayGameMode (GetGameMode plus a class check)
UWorld::GetAuthGameMode has no public (it is inlined as a load of World+0x1A8), and
AGameModeBase::GameSession is a plain field; neither is a call, so this sweep cannot see them.
The same holds for game code that reads World+0x1A8 directly.

--targets nav (card 006): navigation-system getters. Clients have no navigation system (the UE
default), so a null return is the normal case there:
  - UNavigationSystemV1::GetCurrent(UWorld*)
  - UNavigationSystemV1::GetNavigationSystem(UObject*)
FNavigationSystem::GetCurrent<T>(World) is a template and is inlined as a load of
World+0x1A0 (UWorld::NavigationSystem) followed by an IsA check against
UNavigationSystemV1::StaticClass(). Those sites are listed as INLINED: every call to
Z_Construct_UClass_UNavigationSystemV1_NoRegister (the StaticClass of that check). The nav pointer
is not in rax there, so they are not classified; each needs reading by hand.

--targets playerinput (card 007): loads of APlayerController::PlayerInput (+0x420). Only a local
PC runs InitInputSystem, so a remote client's PC on the listen server has PlayerInput == null.
There is no getter to follow; the sites are instructions `mov <reg>, qword ptr [<base> + 0x420]`
(found by byte pattern: REX.W 8B, ModRM mod=10, optional SIB, disp32 0x420, then confirmed in
the decoded function), and the loaded register is followed like a returned rax below. Only game
code is printed (see below). The base object's type is not known statically: a site counts as a
PC load when its function belongs to a class whose name ends in PlayerController; other game
classes with an unrelated field at +0x420 are printed after them, marked "not a PC". Rows are
ranked: PC loads in functions the server runs for every PC (Tick, BeginPlay, possession,
SetPlayer, OnRep_*, ...) first, then other PC loads, then the rest. Report only; nothing gates.

Every `call` (E8 rel32) in an executable section to a target is a site (lp/authority/nav). The caller is
disassembled with llvm-objdump and the returned rax is followed for at most --window
instructions (default 24), straight-line, following unconditional in-function jumps:
  - a memory operand based or indexed on the value (or on `lea x, [value + d]`) is a deref;
  - passing it in rcx/rdx/r8/r9 to a direct call is a deref when that callee (followed the same
    way, three levels deep, 2x window) dereferences it;
  - `test r, r` then je/jne on the value is a null test. The null path is then followed too: if it
    reaches a deref the site is UNCHECKED (the guarded block only skips work, as in
    ASTPlayerController::SetPlayer).
Verdicts: CHECKED (null-tested first, null path clean), UNCHECKED (deref reachable with null),
UNSURE (window ran out, value stored/returned/passed indirectly, or a callee was unclear).
This is a heuristic over linear code, not a dataflow proof; conditional branches other than the
null test are taken as fall-through.

Rows are printed for game-module callers (decorated scope names with an ST/AD class prefix,
e.g. ASTPlayerController, UADGASHealthComponent); --all prints every row. Counts cover all sites.

Standard library only; run with `python3 -I`. Exit status != 0 on any failure.
"""

import argparse
import bisect
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import crash_stack  # noqa: E402  (PE sections, publics -> function table)
import pdb_rva  # noqa: E402  (defaults)

IMAGE_BASE = 0x140000000
LP_COLLECTION_OFFSET = 0x138
PLAYER_INPUT_OFFSET = 0x420  # APlayerController::PlayerInput (InitInputSystem tests and sets it)
LEA_LOOKBACK = 8
MAX_FUNC_BYTES = 0x40000
CALLEE_DEPTH = 3
ARG_REGS = ("rcx", "rdx", "r8", "r9")
VOLATILE = ("rax", "rcx", "rdx", "r8", "r9", "r10", "r11")
GET_LOCAL_PLAYER = "?GetLocalPlayer@APlayerController@@QEBAPEAVULocalPlayer@@XZ"
NAV_GETTERS = (
    "?GetCurrent@UNavigationSystemV1@@SAPEAV1@PEAVUWorld@@@Z",
    "?GetNavigationSystem@UNavigationSystemV1@@SAPEAV1@PEAVUObject@@@Z",
)
NAV_STATIC_CLASS = "?Z_Construct_UClass_UNavigationSystemV1_NoRegister@@YAPEAVUClass@@XZ"
GET_SUBSYSTEM_INTERNAL = "?GetSubsystemInternal@FSubsystemCollectionBase@@IEBAPEAVUSubsystem@@PEAVUClass@@@Z"
AUTHORITY_GETTERS = (
    "?GetGameMode@UGameplayStatics@@SAPEAVAGameModeBase@@PEBVUObject@@@Z",
    "?GetSTGameplayGameMode@USTCommonUtilityLibrary@@SAPEAVASTGameplayGameMode@@PEBVUObject@@@Z",
)
RE_LP_GETSUBSYSTEM = re.compile(r"^\?\?\$GetSubsystem@V(\w+)@@@ULocalPlayer@@")
RE_GAME_SCOPE = re.compile(r"@[AUFSE](ST|AD)[A-Z]")
RE_PC_SCOPE = re.compile(r"^\?\w+@\w*PlayerController@@")
# Members the server runs for every PlayerController, remote ones included.
RE_SERVER_EVERY_PC = re.compile(
    r"^\?(Tick|TickActor|PlayerTick|BeginPlay|PostInitializeComponents|PreInitializeComponents|"
    r"OnPossess|OnUnPossess|Possess|UnPossess|SetPawn|SetPlayer|ReceivedPlayer|InitPlayerState|"
    r"OnRep_\w+|EndPlay|Destroyed|PawnLeavingGame|GetAndApplyGameUserSettings)@")
RE_INSN = re.compile(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*?)\s*$")
RE_MEM = re.compile(r"\[([^\]]*)\]")
RE_HEX_TARGET = re.compile(r"^0x([0-9a-f]+)")
PREFIXES = {"lock", "rep", "repe", "repne", "notrack", "data16"}
NO_WRITE = {"test", "cmp", "push", "bt", "call", "jmp", "ret", "nop", "ucomiss", "ucomisd", "comiss"}
FLAG_NEUTRAL = {"mov", "lea", "movzx", "movsxd", "movsx", "push", "pop"}
MOVE = {"mov", "movzx", "movsxd", "movsx"}
ARITH = {"add", "sub", "or", "and", "shl", "shr", "inc", "dec"}


def die(msg):
    print(f"find_lp_derefs: error: {msg}", file=sys.stderr)
    sys.exit(1)


def build_reg_map():
    m = {}
    for r in ("a", "b", "c", "d"):
        for alias in (f"r{r}x", f"e{r}x", f"{r}x", f"{r}l", f"{r}h"):
            m[alias] = f"r{r}x"
    for r in ("si", "di", "bp", "sp"):
        for alias in (f"r{r}", f"e{r}", r, f"{r}l"):
            m[alias] = f"r{r}"
    for n in range(8, 16):
        for suf in ("", "d", "w", "b"):
            m[f"r{n}{suf}"] = f"r{n}"
    return m


REG = build_reg_map()
RE_REG = re.compile(r"\b(" + "|".join(sorted(REG, key=len, reverse=True)) + r")\b")


class Insn:
    __slots__ = ("addr", "mnem", "ops")

    def __init__(self, addr, mnem, ops):
        self.addr, self.mnem, self.ops = addr, mnem, ops


def disassemble(exe_path, start_rva, end_rva):
    out = pdb_rva.run(["llvm-objdump", "-d", "--no-show-raw-insn", "-M", "intel",
                       f"--start-address=0x{IMAGE_BASE + start_rva:X}",
                       f"--stop-address=0x{IMAGE_BASE + end_rva:X}", str(exe_path)])
    insns = []
    for line in out.splitlines():
        m = RE_INSN.match(line)
        if not m or m.group(2) == "<unknown>":
            continue
        mnem, rest = m.group(2), m.group(3)
        if mnem in PREFIXES and rest:
            parts = rest.split(None, 1)
            mnem, rest = parts[0], (parts[1] if len(parts) > 1 else "")
        rest = rest.split("#", 1)[0].strip()
        ops = [o.strip() for o in rest.split(",")] if rest else []
        insns.append(Insn(int(m.group(1), 16) - IMAGE_BASE, mnem, ops))
    return insns


class Funcs:
    """Function publics sorted by RVA; disassembly cache keyed by function start."""

    def __init__(self, exe_path, rvas, names):
        self.exe_path, self.rvas, self.names = exe_path, rvas, names
        self.cache = {}

    def index(self, rva):
        i = bisect.bisect_right(self.rvas, rva) - 1
        return i if i >= 0 else None

    def name_off(self, rva):
        i = self.index(rva)
        return ("?", rva) if i is None else (self.names[i], rva - self.rvas[i])

    def insns(self, rva):
        """(insns, addr->index) for the function containing rva."""
        i = self.index(rva)
        if i is None:
            return None
        start = self.rvas[i]
        if start not in self.cache:
            end = self.rvas[i + 1] if i + 1 < len(self.rvas) else start + 0x1000
            insns = disassemble(self.exe_path, start, min(end, start + MAX_FUNC_BYTES))
            self.cache[start] = (insns, {x.addr: k for k, x in enumerate(insns)})
        return self.cache[start]


def reg_of(op):
    """Canonical 64-bit register if the operand is a bare register, else None."""
    return REG.get(op) if op in REG else None


def mem_regs(op):
    m = RE_MEM.search(op)
    return set(REG[r] for r in RE_REG.findall(m.group(1))) if m else set()


def branch_target(insn):
    m = RE_HEX_TARGET.match(insn.ops[0]) if insn.ops else None
    return int(m.group(1), 16) - IMAGE_BASE if m else None


def update_tracking(insn, exact, derived):
    """Apply one instruction's register write to the tracked sets."""
    if insn.mnem in NO_WRITE or not insn.ops:
        return
    dst = reg_of(insn.ops[0])
    if dst is None:
        return
    src = reg_of(insn.ops[1]) if len(insn.ops) > 1 else None
    was = dst in exact or dst in derived
    src_exact, src_derived = src in exact, src in derived
    lea_from = insn.mnem == "lea" and bool(mem_regs(insn.ops[1]) & (exact | derived))
    exact.discard(dst)
    derived.discard(dst)
    if insn.mnem == "xor" and src == dst:
        return
    if insn.mnem in MOVE and src_exact:
        exact.add(dst)
    elif (insn.mnem in MOVE and src_derived) or lea_from:
        derived.add(dst)
    elif insn.mnem in ARITH and was:
        derived.add(dst)
    elif insn.mnem.startswith("cmov") and (src_exact or src_derived or was):
        derived.add(dst)


class Walker:
    """Follows one value through straight-line code. See the module docstring for the rules."""

    def __init__(self, funcs, window):
        self.funcs, self.window = funcs, window
        self.callee_cache = {}

    def callee_verdict(self, rva, reg, depth):
        key = (rva, reg, depth)
        if key not in self.callee_cache:
            self.callee_cache[key] = "unsure"  # breaks recursion cycles
            got = self.funcs.insns(rva)
            if got is None or got[1].get(rva) is None:
                self.callee_cache[key] = "unsure"
            else:
                v, _, _ = self.walk(got, got[1][rva], {reg}, set(), self.window * 2, depth)
                self.callee_cache[key] = v
        return self.callee_cache[key]

    def pass_to_call(self, target, exact, derived, depth):
        """'deref' if a direct callee dereferences a tracked argument; 'unsure' if unknown."""
        args = [r for r in ARG_REGS if r in exact or r in derived]
        if not args:
            return None
        if target is None or depth <= 0:
            return "unsure"
        verdicts = [self.callee_verdict(target, r, depth - 1) for r in args]
        if "deref" in verdicts:
            return "deref"
        return "unsure" if "unsure" in verdicts else None

    def walk(self, func, idx, exact, derived, budget, depth):
        """Return (verdict, reason, null_path) with verdict in deref|checked|unsure."""
        insns, by_addr = func
        exact, derived = set(exact), set(derived)
        pending_test, doubt = None, None
        for _ in range(budget):
            if idx >= len(insns) or not (exact or derived):
                return "unsure", doubt or "value no longer in a register", None
            ins = insns[idx]
            if ins.mnem not in ("lea", "nop") and any(mem_regs(o) & (exact | derived) for o in ins.ops):
                return "deref", f"{ins.mnem} {', '.join(ins.ops)} @rva 0x{ins.addr:X}", None
            if ins.mnem in ("je", "jne") and pending_test is not None:
                tgt = by_addr.get(branch_target(ins))
                null_idx = tgt if ins.mnem == "je" else idx + 1
                return "checked", f"{ins.mnem} @rva 0x{ins.addr:X}", (null_idx, exact, derived)
            if (ins.mnem == "test" and len(ins.ops) == 2 and ins.ops[0] == ins.ops[1]
                    and reg_of(ins.ops[0]) in exact):
                pending_test = ins.ops[0]
            elif ins.mnem not in FLAG_NEUTRAL:
                pending_test = None
            if ins.mnem == "ret":
                return "unsure", "returned" if "rax" in exact | derived else doubt or "ret", None
            if ins.mnem in ("call", "jmp"):
                tgt = branch_target(ins)
                inside = tgt is not None and tgt in by_addr
                if ins.mnem == "jmp" and inside:
                    idx = by_addr[tgt]
                    continue
                v = self.pass_to_call(tgt, exact, derived, depth)
                if v == "deref":
                    name, _ = self.funcs.name_off(tgt)
                    return "deref", f"passed to {short(name)} which derefs @rva 0x{ins.addr:X}", None
                doubt = doubt or (f"passed to unclear callee @rva 0x{ins.addr:X}" if v == "unsure" else None)
                if ins.mnem == "jmp":
                    return "unsure", doubt or "jmp out", None
                exact.difference_update(VOLATILE)
                derived.difference_update(VOLATILE)
            else:
                update_tracking(ins, exact, derived)
            idx += 1
        return "unsure", doubt or f"window of {budget} ran out", None


def short(name):
    m = re.match(r"^\?(\w+)@(\w+)@@", name)
    if m:
        return f"{m.group(2)}::{m.group(1)}"
    m = RE_LP_GETSUBSYSTEM.match(name)
    return f"ULocalPlayer::GetSubsystem<{m.group(1)}>" if m else name[:80]


def classify(walker, func, call_idx):
    """Verdict for the rax returned by the call at func[call_idx]."""
    return classify_value(walker, func, call_idx, "rax")


def classify_value(walker, func, idx, reg):
    """Verdict for the value in reg right after the instruction at func[idx]."""
    v, why, null_path = walker.walk(func, idx + 1, {reg}, set(), walker.window, CALLEE_DEPTH)
    if v == "deref":
        return "UNCHECKED", why
    if v == "unsure":
        return "UNSURE", why
    null_idx, exact, derived = null_path
    if null_idx is None:
        return "UNSURE", f"null test {why}, null path leaves the function"
    v2, why2, _ = walker.walk(func, null_idx, exact, derived, walker.window, CALLEE_DEPTH)
    if v2 == "deref":
        return "UNCHECKED", f"null test {why} but null path derefs: {why2}"
    return "CHECKED", f"{why}" + ("" if v2 != "unsure" else f" (null path: {why2})")


def lp_collection_arg(func, call_idx):
    """True if rcx was built as [x + 0x138] in the few instructions before the call."""
    insns = func[0]
    want = {"rcx"}
    for k in range(call_idx - 1, max(call_idx - 1 - LEA_LOOKBACK, -1), -1):
        ins = insns[k]
        dst = reg_of(ins.ops[0]) if ins.ops else None
        if dst not in want or ins.mnem in NO_WRITE:
            continue
        if ins.mnem == "lea" and re.search(rf"\+ 0x{LP_COLLECTION_OFFSET:x}\]$", ins.ops[1]):
            return True
        if ins.mnem == "mov" and reg_of(ins.ops[1]) is not None:
            want = {reg_of(ins.ops[1])}
            continue
        return False
    return False


def find_targets(rvas, names, kind):
    if kind == "nav":
        wanted = NAV_GETTERS + (NAV_STATIC_CLASS,)
        targets = {rva: name for rva, name in zip(rvas, names) if name in wanted}
        if len(set(targets.values())) != len(wanted):
            die("a navigation-system getter public was not found")
        return targets
    if kind == "authority":
        targets = {rva: name for rva, name in zip(rvas, names) if name in AUTHORITY_GETTERS}
        if len(set(targets.values())) != len(AUTHORITY_GETTERS):
            die("an authority getter public was not found")
        return targets
    targets = {}
    for rva, name in zip(rvas, names):
        if name == GET_LOCAL_PLAYER or name == GET_SUBSYSTEM_INTERNAL or RE_LP_GETSUBSYSTEM.match(name):
            targets[rva] = name
    if GET_LOCAL_PLAYER not in targets.values() or GET_SUBSYSTEM_INTERNAL not in targets.values():
        die("GetLocalPlayer or GetSubsystemInternal public not found")
    return targets


def raw_call_sites(exe, secs, targets):
    """Yield (site_rva, target_rva) for every E8 rel32 byte pattern landing on a target."""
    for _, va, vsize, roff, rsize, chars in secs:
        if not chars & crash_stack.SCN_MEM_EXECUTE:
            continue
        end = roff + min(vsize, rsize) - 5
        pos = exe.find(b"\xe8", roff, end)
        while pos != -1:
            rva = va + pos - roff
            dest = (rva + 5 + int.from_bytes(exe[pos + 1:pos + 5], "little", signed=True)) & 0xFFFFFFFF
            if dest in targets:
                yield rva, dest
            pos = exe.find(b"\xe8", pos + 1, end)


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--publics", type=Path, help="saved `llvm-pdbutil dump --publics` output")
    src.add_argument("--pdb", type=Path, default=pdb_rva.DEFAULT_PDB)
    ap.add_argument("--exe", type=Path, default=pdb_rva.DEFAULT_EXE)
    ap.add_argument("--window", type=int, default=24, help="instructions to follow (default 24)")
    ap.add_argument("--all", action="store_true", help="print engine rows too")
    ap.add_argument("--targets", choices=("lp", "authority", "nav", "playerinput"), default="lp",
                    help="lp: LocalPlayer getters (default); authority: server-only getters; "
                         "nav: navigation-system getters; playerinput: PC+0x420 loads")
    a = ap.parse_args()
    if not 4 <= a.window <= 256:
        die("--window must be in 4..256")
    return a


def collect(exe, secs, funcs, walker, targets):
    """Return (rows, stats); rows are (caller, off, target, verdict, reason, is_game)."""
    rows, stats = [], {"byte_hits": 0, "not_insn": 0, "other_collection": 0}
    for site, dest in raw_call_sites(exe, secs, targets):
        stats["byte_hits"] += 1
        func = funcs.insns(site)
        idx = func[1].get(site) if func else None
        if idx is None or func[0][idx].mnem != "call":
            stats["not_insn"] += 1
            continue
        tname = targets[dest]
        if tname == GET_SUBSYSTEM_INTERNAL:
            if not lp_collection_arg(func, idx):
                stats["other_collection"] += 1
                continue
            tname = "GetSubsystemInternal(lp+0x138)"
        caller, off = funcs.name_off(site)
        if tname == NAV_STATIC_CLASS:
            verdict, why = "INLINED", "inlined Cast<UNavigationSystemV1>(World+0x1A0); not classified"
            tname = "UNavigationSystemV1::StaticClass"
        else:
            verdict, why = classify(walker, func, idx)
        rows.append((caller, off, short(tname), verdict, why, bool(RE_GAME_SCOPE.search(caller)), 0))
    return rows, stats


def player_input_loads(exe, secs):
    """Yield the RVA of every byte pattern `REX.W 8B modrm(mod=10) [SIB] 20 04 00 00`."""
    disp = PLAYER_INPUT_OFFSET.to_bytes(4, "little")
    for _, va, vsize, roff, rsize, chars in secs:
        if not chars & crash_stack.SCN_MEM_EXECUTE:
            continue
        end = roff + min(vsize, rsize)
        pos = exe.find(disp, roff + 4, end)
        while pos != -1:
            modrm = exe[pos - 1]
            has_sib = modrm >> 6 == 2 and modrm & 7 == 4
            start = pos - 4 if has_sib else pos - 3
            modrm = exe[start + 2]
            if 0x48 <= exe[start] <= 0x4F and exe[start + 1] == 0x8B and modrm >> 6 == 2 \
                    and (modrm & 7 == 4) == has_sib:
                yield va + start - roff
            pos = exe.find(disp, pos + 1, end)


def player_input_rank(caller):
    """0: PC member the server runs for every PC; 1: other PC member; 2: not a PC class."""
    if not RE_PC_SCOPE.match(caller):
        return 2
    name = caller.split("@", 1)[0] + "@"
    return 0 if RE_SERVER_EVERY_PC.match(name) else 1


def collect_player_input(exe, secs, funcs, walker, show_all):
    """Like collect(), for loads of PC+0x420; the loaded register is the followed value."""
    rows, stats = [], {"byte_hits": 0, "not_insn": 0, "not_game": 0}
    want = f"+ 0x{PLAYER_INPUT_OFFSET:x}]"
    for site in player_input_loads(exe, secs):
        stats["byte_hits"] += 1
        caller, off = funcs.name_off(site)
        is_game = bool(RE_GAME_SCOPE.search(caller))
        if not (is_game or show_all):
            stats["not_game"] += 1
            continue
        func = funcs.insns(site)
        idx = func[1].get(site) if func else None
        ins = func[0][idx] if idx is not None else None
        dst = reg_of(ins.ops[0]) if ins is not None and len(ins.ops) == 2 else None
        if ins is None or ins.mnem != "mov" or dst is None or not ins.ops[1].endswith(want):
            stats["not_insn"] += 1
            continue
        verdict, why = classify_value(walker, func, idx, dst)
        rank = player_input_rank(caller)
        tname = "PlayerInput(+0x420)" + (" not a PC" if rank == 2 else "")
        rows.append((caller, off, tname, verdict, why, is_game, rank))
    return rows, stats


def print_report(rows, stats, show_all):
    shown = [r for r in rows if show_all or r[5]]
    order = {"UNCHECKED": 0, "UNSURE": 1, "INLINED": 2, "CHECKED": 3}
    shown.sort(key=lambda r: (r[6], order[r[3]], r[0], r[1]))
    for caller, off, tname, verdict, why, _, _ in shown:
        print(f"{short(caller)}+0x{off:X}  {tname}  {verdict}  ; {why}")
    if "other_collection" in stats:
        print(f"--- E8 byte hits on targets: {stats['byte_hits']}; not a decoded call: {stats['not_insn']}; "
              f"GetSubsystemInternal on non-LocalPlayer collections: {stats['other_collection']}")
    else:
        print(f"--- +0x420 load byte patterns: {stats['byte_hits']}; outside game code (skipped): "
              f"{stats['not_game']}; not a decoded qword mov: {stats['not_insn']}")
        ranks = [sum(1 for r in rows if r[6] == k) for k in range(3)]
        print(f"--- rank: server-every-PC={ranks[0]}  other PC={ranks[1]}  not a PC={ranks[2]}")
    for label, sel in (("all", rows), ("game", [r for r in rows if r[5]])):
        counts = {v: sum(1 for r in sel if r[3] == v) for v in ("CHECKED", "UNCHECKED", "UNSURE", "INLINED")}
        print(f"--- {label} sites: {len(sel)}  " + "  ".join(f"{k}={n}" for k, n in counts.items()))


def main():
    args = parse_args()
    exe = crash_stack.read_file(args.exe)
    secs = crash_stack.pe_sections(exe)
    rvas, names = crash_stack.function_table(crash_stack.publics_text(args), secs)
    funcs = Funcs(args.exe, rvas, names)
    walker = Walker(funcs, args.window)
    if args.targets == "playerinput":
        rows, stats = collect_player_input(exe, secs, funcs, walker, args.all)
    else:
        rows, stats = collect(exe, secs, funcs, walker, find_targets(rvas, names, args.targets))
    print_report(rows, stats, args.all)


if __name__ == "__main__":
    main()
