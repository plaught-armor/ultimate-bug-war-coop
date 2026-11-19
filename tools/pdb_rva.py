#!/usr/bin/env python3
"""Resolve decorated public symbol names from the Shipping PDB to image RVAs.

Source of truth: `llvm-pdbutil dump --publics` (segment:offset, offset in decimal) and
`llvm-pdbutil dump --section-headers` (segment N == section header #N, 1-based).
RVA = section[N].virtual_address + offset.

Subcommands:
  lookup NAME...   print "RVA  NAME" for each decorated name
  gen              read tools/symbols.txt, write mod/src/offsets.generated.h

Before resolving, the PDB GUID/age is checked against the exe's CodeView debug entry
(`llvm-readobj --coff-debug-directory`); a mismatch is fatal.

Run with `python3 -I tools/pdb_rva.py ...`. Exit status != 0 on any failure.
"""

import argparse
import re
import subprocess
import sys
import uuid
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BIN_DIR = REPO / "game/StarshipTroopers_UBW/Binaries/Win64"
DEFAULT_EXE = BIN_DIR / "StarshipTroopers_UBW-Win64-Shipping.exe"
DEFAULT_PDB = BIN_DIR / "StarshipTroopers_UBW-Win64-Shipping.pdb"
DEFAULT_SYMBOLS = REPO / "tools/symbols.txt"
DEFAULT_OUT = REPO / "mod/src/offsets.generated.h"

RE_PUB = re.compile(r"^\s*\d+ \| S_PUB32 \[size = \d+\] `(.*)`\s*$")
RE_ADDR = re.compile(r"^\s*flags = (.*), addr = (\d{4}):(\d+)\s*$")
RE_SECT = re.compile(r"^\s*SECTION HEADER #(\d+)\s*$")
RE_SECT_FIELD = re.compile(r"^\s*([0-9A-Fa-f]+) (virtual size|virtual address|name)\s*$")
RE_SECT_NAME = re.compile(r"^\s*(\S+) name\s*$")
RE_GUID = re.compile(r"^\s*(?:GUID|PDBGUID): (\{[0-9A-Fa-f-]+\})\s*$")
RE_AGE = re.compile(r"^\s*(?:Age|PDBAge): (\d+)\s*$")
RE_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def die(msg):
    print(f"pdb_rva: error: {msg}", file=sys.stderr)
    sys.exit(1)


def run(args):
    try:
        res = subprocess.run(args, check=False, capture_output=True, text=True)
    except OSError as e:
        die(f"cannot run {args[0]}: {e}")
    if res.returncode != 0:
        die(f"{' '.join(map(str, args))} exited {res.returncode}: {res.stderr.strip()}")
    return res.stdout


def guid_age(text, what):
    guid = age = None
    for line in text.splitlines():
        m = RE_GUID.match(line)
        if m and guid is None:
            guid = uuid.UUID(m.group(1))
        m = RE_AGE.match(line)
        if m and age is None:
            age = int(m.group(1))
    if guid is None or age is None:
        die(f"no GUID/age found in {what}")
    return guid, age


def check_identity(pdb, exe):
    pdb_id = guid_age(run(["llvm-pdbutil", "dump", "--summary", str(pdb)]), "PDB summary")
    exe_id = guid_age(run(["llvm-readobj", "--coff-debug-directory", str(exe)]), "exe debug directory")
    if pdb_id != exe_id:
        die(f"PDB {pdb_id} does not match exe {exe_id}")
    return pdb_id


def sections(pdb):
    """Return {segment_index: (name, virtual_address, virtual_size)}."""
    out = {}
    cur = None
    fields = {}
    for line in run(["llvm-pdbutil", "dump", "--section-headers", str(pdb)]).splitlines():
        m = RE_SECT.match(line)
        if m:
            if cur is not None:
                out[cur] = fields
            cur, fields = int(m.group(1)), {}
            continue
        if cur is None:
            continue
        m = RE_SECT_FIELD.match(line)
        if m and m.group(2) != "name":
            fields[m.group(2)] = int(m.group(1), 16)
            continue
        m = RE_SECT_NAME.match(line)
        if m and "name" not in fields:
            fields["name"] = m.group(1)
    if cur is not None:
        out[cur] = fields
    if not out:
        die("no section headers parsed from PDB")
    result = {}
    for idx, f in out.items():
        if not {"name", "virtual address", "virtual size"} <= f.keys():
            die(f"section header #{idx} incomplete: {f}")
        result[idx] = (f["name"], f["virtual address"], f["virtual size"])
    return result


def resolve(pdb, names):
    """Return {name: rva}. Every name must resolve to exactly one function (or global variable) address."""
    wanted = set(names)
    secs = sections(pdb)
    found = {}
    pending = None
    for line in run(["llvm-pdbutil", "dump", "--publics", str(pdb)]).splitlines():
        if pending is not None:
            m = RE_ADDR.match(line)
            if not m:
                die(f"unexpected line after public {pending!r}: {line!r}")
            flags, seg, off = m.group(1), int(m.group(2)), int(m.group(3))
            # MSVC encodes a global variable as "?name@@3<type>"; those are data symbols (card 118). Anything
            # else must be a function.
            if "function" not in flags.split(" | ") and not any(f"@@{d}" in pending for d in "0123"):  # card 222: @@0-2 static members
                die(f"{pending} is not a function (flags = {flags})")
            if seg not in secs:
                die(f"{pending}: segment {seg} has no section header")
            name, va, vsize = secs[seg]
            if off >= vsize:
                die(f"{pending}: offset {off:#x} outside section {name} (size {vsize:#x})")
            rva = va + off
            if pending in found and found[pending] != rva:
                die(f"{pending}: duplicate publics with different RVAs {found[pending]:#x}, {rva:#x}")
            found[pending] = rva
            pending = None
            continue
        m = RE_PUB.match(line)
        if m and m.group(1) in wanted:
            pending = m.group(1)
    missing = sorted(wanted - found.keys())
    if missing:
        die("symbols not found in PDB publics:\n  " + "\n  ".join(missing))
    return found


def read_symbols(path):
    entries = []
    seen = set()
    try:
        text = path.read_text()
    except OSError as e:
        die(f"cannot read {path}: {e}")
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 2:
            die(f"{path}:{n}: expected '<identifier> <decorated name>', got {raw!r}")
        ident, mangled = parts
        if not RE_IDENT.match(ident):
            die(f"{path}:{n}: bad C++ identifier {ident!r}")
        if ident in seen:
            die(f"{path}:{n}: duplicate identifier {ident}")
        seen.add(ident)
        entries.append((ident, mangled))
    if not entries:
        die(f"{path}: no symbols")
    return entries


def gen_header(entries, rvas, guid, age, pdb):
    gb = ", ".join(f"0x{b:02X}" for b in guid.bytes_le)
    lines = [
        "// GENERATED by tools/pdb_rva.py from tools/symbols.txt -- do not edit.",
        f"// PDB: {pdb.name}",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "namespace ubw::offsets {",
        "",
        "// CodeView RSDS identity of the exe these RVAs belong to (GUID in on-disk byte order).",
        f"// GUID {{{str(guid).upper()}}}, age {age}",
        f"inline constexpr uint8_t kPdbGuid[16] = {{{gb}}};",
        f"inline constexpr uint32_t kPdbAge = {age};",
        "",
        "struct Symbol {",
        "    const char* name;",
        "    uint32_t rva;",
        "};",
        "",
    ]
    for ident, mangled in entries:
        lines.append(f"// {mangled}")
        lines.append(f"inline constexpr Symbol {ident} = {{\"{ident}\", 0x{rvas[mangled]:08X}u}};")
    lines += ["", "} // namespace ubw::offsets", ""]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pdb", type=Path, default=DEFAULT_PDB)
    ap.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    sub = ap.add_subparsers(dest="cmd", required=True)
    lk = sub.add_parser("lookup")
    lk.add_argument("names", nargs="+")
    gn = sub.add_parser("gen")
    gn.add_argument("--symbols", type=Path, default=DEFAULT_SYMBOLS)
    gn.add_argument("--out", type=Path, default=DEFAULT_OUT)
    a = ap.parse_args()

    guid, age = check_identity(a.pdb, a.exe)
    if a.cmd == "lookup":
        rvas = resolve(a.pdb, a.names)
        for n in a.names:
            print(f"0x{rvas[n]:08X}  {n}")
        return
    entries = read_symbols(a.symbols)
    rvas = resolve(a.pdb, [m for _, m in entries])
    text = gen_header(entries, rvas, guid, age, a.pdb)
    try:
        a.out.write_text(text)
    except OSError as e:
        die(f"cannot write {a.out}: {e}")
    for ident, mangled in entries:
        print(f"0x{rvas[mangled]:08X}  {ident}")
    print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
