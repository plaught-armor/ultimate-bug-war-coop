#!/usr/bin/env python3
"""Symbolize a UE crash minidump against the Shipping exe's PDB publics.

Usage: python3 -I tools/crash_stack.py <dump> [--publics FILE | --pdb PDB] [--exe EXE] [--depth N]

Prints the exception code, the faulting address, the registers with RIP as symbol+offset,
then a bounded scan of the faulting thread's stack for values that point into the exe's
executable sections. A candidate is marked `(call-preceded)` when the bytes just before it
in the exe on disk decode as a `call` (E8 rel32 or FF /2), i.e. it is a plausible return
address; unmarked candidates are usually stale data. This is a scan, not an unwind.

Publics: `--publics FILE` reads saved `llvm-pdbutil dump --publics` output; otherwise the
tool runs llvm-pdbutil on `--pdb` (default: the game PDB) and checks the PDB matches the exe.
Segment N of a public is the exe's section N (1-based), read from the exe's PE headers.
The dump's module record for the exe must match the exe on disk: same CodeView GUID/age when
the record carries one, otherwise (Wine/Proton dumps leave it empty) same PE TimeDateStamp and
SizeOfImage.

Standard library only; run with `python3 -I`. Exit status != 0 on any failure.
"""

import argparse
import bisect
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pdb_rva  # noqa: E402  (sibling tool: llvm-pdbutil runner, publics regexes, defaults)

STREAM_MODULE_LIST = 4
STREAM_MEMORY_LIST = 5
STREAM_EXCEPTION = 6
STREAM_MEMORY64_LIST = 9
MAX_STREAMS = 4096
MAX_MODULES = 4096
MAX_MEMORY_RANGES = 1 << 20
MODULE_RECORD_SIZE = 108
EXCEPTION_ACCESS_VIOLATION = 0xC0000005
SCN_MEM_EXECUTE = 0x20000000
RSDS = b"RSDS"
IMAGE_DEBUG_TYPE_CODEVIEW = 2
MAX_DEBUG_ENTRIES = 64
# AMD64 CONTEXT offsets: Rax..R15 are contiguous from 0x78, Rip at 0xF8.
GPR_NAMES = ("rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
             "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15")
CTX_GPR_BASE = 0x78
CTX_RIP = 0xF8
CTX_MIN_SIZE = 0x100


def die(msg):
    print(f"crash_stack: error: {msg}", file=sys.stderr)
    sys.exit(1)


def u(fmt, buf, off, what):
    try:
        return struct.unpack_from(fmt, buf, off)
    except struct.error:
        die(f"truncated {what} at offset {off:#x}")


def read_file(path):
    try:
        return Path(path).read_bytes()
    except OSError as e:
        die(f"cannot read {path}: {e}")


# --- PE (exe on disk) -------------------------------------------------------------------------

def pe_sections(exe):
    """Return [(name, va, vsize, raw_off, raw_size, characteristics)] in header order."""
    pe = u("<I", exe, 0x3C, "DOS header")[0]
    if exe[:2] != b"MZ" or exe[pe:pe + 4] != b"PE\0\0":
        die("exe is not a PE image")
    nsec, = u("<H", exe, pe + 6, "COFF header")
    opt_size, = u("<H", exe, pe + 20, "COFF header")
    first = pe + 24 + opt_size
    out = []
    for i in range(nsec):
        o = first + 40 * i
        name = exe[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rsize, roff = u("<IIII", exe, o + 8, "section header")
        chars, = u("<I", exe, o + 36, "section header")
        out.append((name, va, vsize, roff, rsize, chars))
    return out


def pe_codeview(exe, secs):
    """Return the exe's RSDS record bytes (signature, GUID, age) from its debug directory."""
    pe = u("<I", exe, 0x3C, "DOS header")[0]
    dir_rva, dir_size = u("<II", exe, pe + 24 + 112 + 8 * 6, "debug data directory")
    count = dir_size // 28
    if dir_rva == 0 or count == 0 or count > MAX_DEBUG_ENTRIES:
        die(f"exe debug directory missing or implausible (rva={dir_rva:#x} size={dir_size:#x})")
    base = rva_to_file(secs, dir_rva)
    for i in range(count):
        typ, = u("<I", exe, base + 28 * i + 12, "debug directory")
        size, _, raw = u("<III", exe, base + 28 * i + 16, "debug directory")
        if typ == IMAGE_DEBUG_TYPE_CODEVIEW and size >= 24 and exe[raw:raw + 4] == RSDS:
            return exe[raw:raw + 24]
    die("no RSDS CodeView record in exe")


def rva_to_file(secs, rva):
    off = rva_to_file_or_none(secs, rva)
    if off is None:
        die(f"rva {rva:#x} is not backed by file data")
    return off


# --- Publics --------------------------------------------------------------------------------

def publics_text(args):
    if args.publics is not None:
        return read_file(args.publics).decode("utf-8", "replace")
    pdb_rva.check_identity(args.pdb, args.exe)
    return pdb_rva.run(["llvm-pdbutil", "dump", "--publics", str(args.pdb)])


def function_table(text, secs):
    """Return (sorted rvas, names) for every function public in an executable section."""
    pairs = []
    pending = None
    for line in text.splitlines():
        if pending is not None:
            m = pdb_rva.RE_ADDR.match(line)
            if not m:
                die(f"unexpected line after public {pending!r}: {line!r}")
            seg, off = int(m.group(2)), int(m.group(3))
            if "function" in m.group(1).split(" | ") and 1 <= seg <= len(secs):
                _, va, vsize, _, _, chars = secs[seg - 1]
                if chars & SCN_MEM_EXECUTE and off < vsize:
                    pairs.append((va + off, pending))
            pending = None
            continue
        m = pdb_rva.RE_PUB.match(line)
        if m:
            pending = m.group(1)
    if not pairs:
        die("no function publics parsed")
    pairs.sort()
    return [p[0] for p in pairs], [p[1] for p in pairs]


# --- Minidump -------------------------------------------------------------------------------

def streams(dmp):
    if dmp[:4] != b"MDMP":
        die("not a minidump (no MDMP signature)")
    n, dir_rva = u("<II", dmp, 8, "minidump header")
    if n > MAX_STREAMS:
        die(f"implausible stream count {n}")
    out = {}
    for i in range(n):
        typ, size, rva = u("<III", dmp, dir_rva + 12 * i, "stream directory")
        out.setdefault(typ, (size, rva))
    return out


def modules(dmp, strm):
    """Return [(base, size, short_name, cv_bytes, time_date_stamp)]."""
    if STREAM_MODULE_LIST not in strm:
        die("dump has no module list stream")
    rva = strm[STREAM_MODULE_LIST][1]
    count, = u("<I", dmp, rva, "module list")
    if count > MAX_MODULES:
        die(f"implausible module count {count}")
    out = []
    for i in range(count):
        o = rva + 4 + MODULE_RECORD_SIZE * i
        base, size = u("<QI", dmp, o, "module record")
        stamp, = u("<I", dmp, o + 16, "module record")
        name_rva, = u("<I", dmp, o + 20, "module record")
        cv_size, cv_rva = u("<II", dmp, o + 76, "module record")
        name_len, = u("<I", dmp, name_rva, "module name")
        name = dmp[name_rva + 4:name_rva + 4 + name_len].decode("utf-16-le", "replace")
        short = name.replace("/", "\\").split("\\")[-1]
        out.append((base, size, short, dmp[cv_rva:cv_rva + cv_size], stamp))
    return out


def exception(dmp, strm):
    """Return (thread_id, code, address, params, context_bytes)."""
    if STREAM_EXCEPTION not in strm:
        die("dump has no exception stream")
    rva = strm[STREAM_EXCEPTION][1]
    tid, = u("<I", dmp, rva, "exception stream")
    code, _, _, addr, nparams = u("<IIQQI", dmp, rva + 8, "exception record")
    params = u("<15Q", dmp, rva + 40, "exception record")[:min(nparams, 15)]
    ctx_size, ctx_rva = u("<II", dmp, rva + 160, "exception stream")
    if ctx_size < CTX_MIN_SIZE:
        die(f"thread context too small ({ctx_size:#x} bytes)")
    return tid, code, addr, params, dmp[ctx_rva:ctx_rva + ctx_size]


def memory_ranges(dmp, strm):
    """Return [(start, bytes)] from the memory list and/or memory64 list streams."""
    out = []
    if STREAM_MEMORY_LIST in strm:
        rva = strm[STREAM_MEMORY_LIST][1]
        n, = u("<I", dmp, rva, "memory list")
        for i in range(min(n, MAX_MEMORY_RANGES)):
            start, size, data = u("<QII", dmp, rva + 4 + 16 * i, "memory descriptor")
            out.append((start, dmp[data:data + size]))
    if STREAM_MEMORY64_LIST in strm:
        rva = strm[STREAM_MEMORY64_LIST][1]
        n, data = u("<QQ", dmp, rva, "memory64 list")
        for i in range(min(n, MAX_MEMORY_RANGES)):
            start, size = u("<QQ", dmp, rva + 16 + 16 * i, "memory64 descriptor")
            out.append((start, dmp[data:data + size]))
            data += size
    if not out:
        die("dump has no memory list streams")
    return out


# --- Symbolization --------------------------------------------------------------------------

class Symbolizer:
    def __init__(self, mods, exe_mod, exe_secs, rvas, names):
        self.mods, self.exe_mod, self.secs = mods, exe_mod, exe_secs
        self.rvas, self.names = rvas, names

    def exe_code_rva(self, addr):
        """RVA if addr lies in an executable section of the exe, else None."""
        rva = addr - self.exe_mod[0]
        for _, va, vsize, _, _, chars in self.secs:
            if chars & SCN_MEM_EXECUTE and va <= rva < va + vsize:
                return rva
        return None

    def name(self, addr):
        rva = self.exe_code_rva(addr)
        if rva is not None:
            i = bisect.bisect_right(self.rvas, rva) - 1
            if i >= 0:
                return f"{self.exe_mod[2]}+0x{rva:X} {self.names[i]}+0x{rva - self.rvas[i]:X}"
            return f"{self.exe_mod[2]}+0x{rva:X}"
        for base, size, mname, _, _ in self.mods:
            if base <= addr < base + size:
                return f"{mname}+0x{addr - base:X}"
        return "<no module>"


def ff2_length(code, p):
    """Length of an `FF /2` (call r/m64) at code[p], optional REX before it; 0 if not one."""
    q = p + 1 if 0x40 <= code[p] <= 0x4F else p
    if q + 1 >= len(code) or code[q] != 0xFF:
        return 0
    modrm = code[q + 1]
    mod, reg, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
    if reg != 2:
        return 0
    n = q + 2 - p
    if mod == 3:
        return n
    if rm == 4:
        if q + 2 >= len(code):
            return 0
        n += 1
        if mod == 0 and (code[q + 2] & 7) == 5:
            n += 4
    elif mod == 0 and rm == 5:
        n += 4
    return n + (1 if mod == 1 else 4 if mod == 2 else 0)


def call_preceded(exe, secs, rva):
    """True if the instruction ending at rva is a call (E8 rel32 or FF /2, 2..8 bytes)."""
    end = rva_to_file_or_none(secs, rva)
    if end is None or end < 8:
        return False
    code = exe[end - 8:end]
    if code[3] == 0xE8:
        return True
    return any(ff2_length(code, 8 - k) == k for k in range(2, 9))


def rva_to_file_or_none(secs, rva):
    for _, va, vsize, roff, rsize, _ in secs:
        if va <= rva < va + min(vsize, rsize):
            return roff + (rva - va)
    return None


# --- Report ---------------------------------------------------------------------------------

def pe_stamp_and_size(exe):
    pe = u("<I", exe, 0x3C, "DOS header")[0]
    stamp, = u("<I", exe, pe + 8, "COFF header")
    size, = u("<I", exe, pe + 24 + 56, "optional header")
    return stamp, size


def find_exe_module(mods, exe, secs, exe_name):
    """Return the dump's module record for exe_name after checking it is the same build."""
    m = next((m for m in mods if m[2].lower() == exe_name.lower()), None)
    if m is None:
        die(f"dump has no module named {exe_name}")
    if m[3]:
        if m[3][:24] != pe_codeview(exe, secs):
            die(f"dump module {m[2]} CodeView GUID/age differs from {exe_name} on disk")
        print("build identity: CodeView GUID/age match")
        return m
    stamp, size = pe_stamp_and_size(exe)
    if (m[4], m[1]) != (stamp, size):
        die(f"dump module {m[2]} TimeDateStamp/SizeOfImage 0x{m[4]:08X}/0x{m[1]:X} "
            f"differ from exe on disk 0x{stamp:08X}/0x{size:X}")
    print("build identity: dump module has no CodeView record; TimeDateStamp and SizeOfImage match")
    return m


def print_exception(sym, tid, code, addr, params, regs, rip):
    print(f"exception: code=0x{code:08X} thread={tid} at 0x{addr:X}")
    if code == EXCEPTION_ACCESS_VIOLATION and len(params) >= 2:
        kind = {0: "reading", 1: "writing", 8: "executing"}.get(params[0], f"op {params[0]}")
        print(f"faulting address: 0x{params[1]:X} ({kind})")
    print(f"RIP 0x{rip:X} {sym.name(rip)}")
    for i in range(0, len(GPR_NAMES), 4):
        print("  " + "  ".join(f"{GPR_NAMES[j]:>3}=0x{regs[j]:016X}" for j in range(i, i + 4)))


def print_stack(sym, exe, mem, rsp, depth):
    rng = next(((s, b) for s, b in mem if s <= rsp < s + len(b)), None)
    if rng is None:
        die(f"rsp 0x{rsp:X} is not inside any dumped memory range")
    start, buf = rng
    first = rsp - start
    last = min(len(buf) - 7, first + 8 * depth)
    print(f"stack scan from rsp, {depth} qwords max (dumped range ends at +0x{len(buf) - first:X}):")
    shown = 0
    for off in range(first, last, 8):
        val, = struct.unpack_from("<Q", buf, off)
        rva = sym.exe_code_rva(val)
        if rva is None:
            continue
        mark = "  (call-preceded)" if call_preceded(exe, sym.secs, rva) else ""
        print(f"  rsp+0x{off - first:04X}  {sym.name(val)}{mark}")
        shown += 1
    print(f"{shown} candidates")


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dump", type=Path)
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--publics", type=Path, help="saved `llvm-pdbutil dump --publics` output")
    src.add_argument("--pdb", type=Path, default=pdb_rva.DEFAULT_PDB)
    ap.add_argument("--exe", type=Path, default=pdb_rva.DEFAULT_EXE)
    ap.add_argument("--depth", type=int, default=1000, help="stack qwords to scan (default 1000)")
    a = ap.parse_args()
    if not 1 <= a.depth <= 1 << 20:
        die("--depth must be in 1..1048576")
    return a


def main():
    args = parse_args()
    exe = read_file(args.exe)
    secs = pe_sections(exe)
    rvas, names = function_table(publics_text(args), secs)
    dmp = read_file(args.dump)
    strm = streams(dmp)
    mods = modules(dmp, strm)
    exe_mod = find_exe_module(mods, exe, secs, args.exe.name)
    tid, code, addr, params, ctx = exception(dmp, strm)
    regs = u("<16Q", ctx, CTX_GPR_BASE, "thread context")
    rip, = u("<Q", ctx, CTX_RIP, "thread context")
    sym = Symbolizer(mods, exe_mod, secs, rvas, names)
    print(f"exe: {exe_mod[2]} base 0x{exe_mod[0]:X} size 0x{exe_mod[1]:X} ({len(rvas)} function publics)")
    print_exception(sym, tid, code, addr, params, regs, rip)
    print_stack(sym, exe, memory_ranges(dmp, strm), regs[GPR_NAMES.index("rsp")], args.depth)


if __name__ == "__main__":
    main()
