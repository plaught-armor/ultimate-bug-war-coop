#!/usr/bin/env python3
"""Card 063 test runner: drive both games through their command files, wait on their state and log files.

Usage: python3 -I tools/drive.py <scenario> [--bin DIR] [--up-timeout S]

Each game, started with UBWCOOP_CMD=1 and UBWCOOP_LOG_TAG=<role>, polls UBWCoop-<role>.cmd and rewrites
UBWCoop-<role>.state next to its log (DIR, default the game's Binaries/Win64). No window focus, no xdotool.

Scenario lines (blank lines and lines starting with # are skipped):
  <role> key <FKeyName> [hold_ms [<FKey2> <at_ms>]]  press and release, through the PC's InputKey; FKey2 is tapped at_ms into the hold (card 107 chord)
  <role> axis <FKeyName> <value> <ms>      one axis event per tick for <ms>
  <role> shot <name>                       screenshot Saved/Screenshots/Windows/<role>-[<prefix>-]<name>.png
  <role> quit                              RequestEngineExit
  <role> goto <x> <y> <z> [radius] [sprint]   walk there on the navmesh (card 064; radius default 150 uu) with W,
                                           mouse axis turns and stuck recoveries (card 124); sprint holds LeftShift
  <role> goto actor <name> [radius] [sprint] [live]   walk to a placed actor (object name, or the nearest of a class
                                           name); live skips corpses and taken pickups (card 244 b208)
  <role> look <yaw> <pitch> [smooth]       set the control rotation; smooth: turn there with mouse axis events (124)
  <role> look actor <name> [dz] [smooth]   face the actor (its z + dz uu) from the view point
  <role> look actor <name> [dz] bounds [smooth]  face the actor's bounds centre (+ dz uu) instead (131)
  <role> look actor <name> [dz] hole [smooth]    b232: face the actor's nearest grenade hole component (nests)
  <role> look point <x> <y> <z> [smooth]   face that point from the view point
  host grenade fill <n>                    b234, test only: every player's frag grenade count raised to n (1 to 20)
  <role> look ... within=<R>              card 243: VOID when the look row's distance is over R uu (card 504: under
                                           try, a failed look gate is "TRY-FAIL gate not held" plus "not proved" rows
                                           for it and every action it gated, never a VOID)
  <role> look ... hit=<prefix>            card 264: VOID (card 504: under try, not proved) unless the aim row's hit actor name starts
                                           with prefix (fire, throw, damage: what the shot hits)
  <role> look ... hit=<prefix> walk=<N>   card 316: while the only fault is a blocked muzzle line, walk with real
                                           keys (W 600, A 600, D 1200, W 600, A 1200, D 1200 ms; the first N) and
                                           look again after each, until the gates hold
  <role> look ... focus=<prefix>          card 264: VOID (card 504: under try, not proved) unless the aim focus row's actor name starts
                                           with prefix (E use: what E acts on)
  <role> look actor <name> [dz] bounds sweep   b229: the focus=<name> bounds sweep with no gate (focus none is no VOID)
  <role> clear <radius> [seconds] [max-left=N] card 265: kill the live bugs within radius by weapon fire (cap
                                           seconds, default 90);
                                           VOID (even under try) when more than N (default 0) are left
                                           or its start row (before it fires) has under 60 of its primary ammo
  host clear <radius> kill [max-left=N]    user rule 2026-11-13 (objective tests): InstaKillTarget on every live bug
                                           within radius of the host's pawn, no fire, no ammo; VOID (even under try)
                                           when the row `cmd: N clear kill: K killed, L left` has L over N (default 0)
  <role> pickup <class> <max>              walk to the nearest <class> only if its path is at most max uu
  <role> warp actor|client <name> [dist]   teleport own pawn (client: host's server copy) dist uu short of it (075)
  <role> warp actor|client point <x> <y> <z>   teleport exactly there, no lift (card 106)
  <role> warp actor|client <name> <dist> behind   dist uu behind the actor along its forward axis (card 123); trailing live (b210): nearest untaken/alive
  host warp client2 ...                     as warp client, on the second server copy in PC order (card 209)
  <role> charge <slot> <value> <ready>     set own pawn's tactical support slot charge and ready flag (card 106)
  host charge client <slot> <value> <ready>  the same on the server copy of the client's pawn (card 324)
  host spawn bug <x> <y> <z> <yaw> [<kind>] one BP_AI_<kind> there (WarriorBug default), facing yaw degrees (card 125)
  both god all|host                          host: only the host's own pawn under UBWCOOP_GOD (card 132)
  host stamina on|off                        card 490: the host infinite stamina option for this session
  <role> console <words>                    card 573 (b392+): APlayerController::ConsoleCommand on that side's PC, 1-6 words
  host discard <n>                           the client copy's next n rounds (1-10) become misses: claims take made rounds (281)
  host down host|client|client2            put that player into card 043 Downed, the lethal-hit entry (card 109;
                                           client2: roster slot 2, card 209)
  <role> ui continue                       close the mission brief / any-key screen (card 067; the game fails it in 5 s)
  <role> ui click <prefix>                 a menu button click: the WB_Stack_C widget function named <prefix>... (121)
  <role> disconnect                        console "disconnect" (card 056); a client's card 045 follow reconnects
  <role> crash av                          test only: an access violation, to show the card 118 crash trace (the game dies)
  <role> objective complete <id>|all      host: complete an incomplete objective, or all of them (card 095)
  host objective now                       card 284: log every open step (current, doable, open) and container under
                                           the active parent; the current step's stand point, needs and, for marker
                                           and other, the nearest BP_PlayerPresence_C; the doable ids
  host objective expect <id>               card 284: passes once <id> is doable (polls 3 s) and makes it current; else
                                           VOID with the doable ids, even under try
  host objective go [host|client|client2|all]   card 284: warp the named players (default all) to the current step's
                                           stand point; after a 1 s settle rows each pawn; fails on kind other, a
                                           pawn moved over 50 uu since the warp, or not inside the volume
  host objective next                      b238: the next doable step after the current one becomes current (sparse
                                           order, wrap); a satchel site no player can interact with is skipped; none:
                                           the current step stays
  host objective wait <id> [seconds]       card 284: done when <id> completes; fails at seconds (default 60, 1-900)
                                           with the step's count and the live active step
  host objective do [<id>]                 card 504: expect <id> (if named), now, then the routine of the
                                           current step's kind (nest, presence, use; a use step on a Pickup class:
                                           pickup, every player; docs/reference/objective-routines.md) and the
                                           client's replay row; a kind with no routine, or 3 failed tries, is VOID.
                                           Gate failures print `objective go: <kind> gate failed: <why>`
  <role> objectives                        log the objective list panel and every objective's count (card 066)
  <role> door list|open <name>|close <name>  list doors and blockers near the pawn, or open/close a placed door (095)
  <role> wait state k=v [k=v ...] [timeout=S] [void]   every k=v holds in one state line; with void (card 264)
                                               a timeout is VOID, so failsoft cannot carry a wrong map on
  <role> mark                                  remember the role's log length now, for wait log ... since-mark
  <role> wait log "<regex>" [count=N] [new|since-mark] [timeout=S]   N lines (default 1) of the role's log match (the whole
                                               log, or with new only the lines written after the wait began);
                                               its groups feed {g1}..{g9} in later steps, {g1+60} adds (card 107)
  <role> wait near <x> <y> <z> <radius> [timeout=S]   the role's own pawn (state x y z) is within radius (card 107)
  <role> wait stand <x> <y> <z> <radius> [timeout=S]  within radius and two position reads 0.5 s apart equal
                                                     (z `any`: x y distance only, for a rest z never measured)
                                               (card 242); a timeout ends the run as VOID, failsoft or not
  <role> wait goto [timeout=S]               the role's last goto ends; fails unless it reached the goal (card 124)
  & <role> <command> ... / & both <command> ...   post the command and go on at once (card 124); its result is
                                               checked by a later join or wait goto
  join [<role>]                                wait for every & step of that role (no role: of both); fails if any failed
  both <command> ...                           the command goes to host and client at once; waits for both (card 118)
  try <step>                                   run <step>; a failure is logged as "TRY-FAIL" and the scenario goes on.
                                               Card 504: a look gate (hit=, focus=, within=, line=, walk=) that does not
                                               hold is "TRY-FAIL gate not held", the actions it gates are skipped, and
                                               each is a "not proved" row (step, gate, detail); the end has "not proved:
                                               N". Only proof lines (steps without try) decide PASS, FAIL or VOID
  must <step>                                  card 310 range: run <step>; a failure or void ends the run (exit 1)
                                               under failsoft and voidsoft too: the first such step is the one result
  retry <N> <step> ;; <step> ;; ...            card 243 r10: run the steps in order; when one of them voids on a clear or
                                               look gate (calm=, max-left=, within=, hit=, focus=), start the whole list
                                               again, at most N more times; the last attempt's VOID is the result. Any
                                               other failure ends the list at once and is the result
  sleep <seconds>
  <role> stall <seconds>                       SIGSTOP the role's game process, wait, SIGCONT it (card 128: a stalled
                                               machine); the process is the one with that role's log tag in this lane
  unless <role> log "<regex>" count=N <step>   skip <step> when N lines of the role's log already match (card 065)
  stuck <role> log "<regex>" count=N           b215 speed-up: when N lines of the role's log match (its regex takes
                                               {gN}), log the step as STUCK and skip every step up to the next
                                               "# ===" stop comment (a soak's next block); otherwise pass
  when <role> last "<regex>" "<test>" <step>   run <step> only when the role's last log line matching <regex>
                                               also matches <test> (card 065: fire only while a target is up)
  void unless <roleA> log "<rxA>" before <roleB> log "<rxB>"   card 137: end the run as VOID when rxB's first line
                                               in roleB's log has an earlier [ ms] stamp than rxA's first line in
                                               roleA's (both games run on one machine: one GetTickCount64 clock), or
                                               rxB matched and rxA never did; otherwise the step passes
  void unless <role> dll <hash16>              card 264: VOID unless the dwmapi.dll in the role's game directory
                                               hashes (sha256, first 16 hex) to <hash16>: a stale deploy fails at once
  failsoft                                     card 137 (user, run 4): from this line on, a failed step logs
                                               "scenario: wait FAILED line N (<text>), continuing" (or "step FAILED")
                                               and the scenario goes on. A void step, or a failure while a role's game
                                               process is gone, still ends it; the exit code is 2 when any line failed
  abort-on objective                           card 316: from this line on, a failed `<role> objective wait` ends the
                                               run (exit 1) under failsoft and voidsoft too: every later step depends
                                               on it (run 3 went on 28 minutes after SO3 stayed open)
  voidsoft                                     card 243 r9 (senior, discovery mode): from this line on, a VOID step
                                               is logged as before (its "VOID ..." row and the fail shots) and the
                                               scenario goes on; the end line is "scenario finished VOID with N void
                                               line(s): ..." and the exit code is 3. A run with any void proves
                                               nothing for the voided steps and is not a soak. Still terminal: a
                                               wrong DLL hash, a wrong map or pawn (`wait state ... void`), a role's
                                               game gone
Command steps wait for the game's "cmd: <seq> ... -> done" line (default timeout 10 s, goto 620 s: the game ends a
goto itself at its own timeout, at most 600 s); any command step takes a trailing timeout=S. In the game a goto walks
in the background (card 124): look, key, shot, axis and warp run while it walks, a new goto replaces the running one,
and the goto's result line comes at the walk's end, so a plain goto step still waits for the walk. The first failed
step ends the scenario (unless failsoft is on): both roles take a "fail-<step>" shot and the exit code is 1.
"""
import argparse
import math
import glob
import hashlib
import os
import re
import shlex
import signal
import sys
import time

DEFAULT_BIN = "/path/to/ultimate-bug-war-coop/game/StarshipTroopers_UBW/Binaries/Win64"
COMMANDS = {"key", "axis", "shot", "quit", "goto", "look", "ui", "disconnect", "objectives", "warp", "door", "objective", "kill", "charge", "down", "travel", "crash", "spawn", "autoplay", "speed", "inventory", "offshot", "god", "winkey", "clear", "ammo", "levelcall", "discard", "stamina", "grenade", "console", "resupplylevel"}  # levelcall: b184 test-only level script call; ammo: `ammo fill` (card 243 run A decision, test only); resupplylevel: card 556 b440 test only
CMD_TIMEOUT_S = 10.0
CLEAR_TIMEOUT_S = 100.0  # card 265: the game ends a clear at 90 s
GOTO_TIMEOUT_S = 620.0
WAIT_TIMEOUT_S = 30.0
POLL_S = 0.1
MAX_STEPS = 3200  # 2026-11-19: C5M2 BLOCKS=42 needs about 3000 steps. 2026-11-18: c244 skip ledger adds 2 steps per block; card 243 r7: the full C3M2 soak with per-action clears is about 2040 steps
GROUP_RE = re.compile(r"\{g([1-9])([+-]\d+(?:\.\d+)?)?\}")
last_groups = []  # the last matched wait log's groups, for {gN} (card 107)
DONE_RE = re.compile(r"cmd: (\d+) .* -> (done|failed \(.*\))\s*$")
SKIP_RE = re.compile(r"WARN cmd: (\d+) (\".*\" skipped: .*)$")


class Role:
    """One game's files and how many command lines this runner has written to it."""

    def __init__(self, bin_dir, name):
        self.name = name
        self.bin_dir = bin_dir
        self.cmd = os.path.join(bin_dir, f"UBWCoop-{name}.cmd")
        self.state = os.path.join(bin_dir, f"UBWCoop-{name}.state")
        self.log_glob = os.path.join(bin_dir, f"UBWCoop-{name}-[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9].log")
        self.sent = 0
        self.pending = []  # (seq, line, timeout_s) posted by & steps, awaited by join (card 124)
        self.last_goto = None  # (seq, timeout_s) of the last goto posted, for wait goto
        self.mark = 0  # log lines at the role's last `mark` step, for wait log ... since-mark
        self.gate_void = None  # b12 item 2: the role's last look voided its hit=/focus= gate (its void detail)


def role_log(role):
    """The role's newest mod log: one per launch, UBWCoop-<role>-YYYYMMDD-HHMMSS.log (card 130 item 16).
    run_pair.sh removes old logs before a launch, so the newest is this launch's. "" when none exists yet."""
    logs = sorted(glob.glob(role.log_glob))
    return logs[-1] if logs else ""


def read_text(path):
    try:
        with open(path, "r", encoding="ascii", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


_LOGS = {}  # log path -> [bytes read, complete lines, partial last line]: each byte is read and decoded once
_SCAN = {}  # (log path, regex) -> [lines scanned, match count, last matching line]


def log_lines(path):
    """The complete lines of a log, read incrementally (c244b219a's host log was 110 MB and every `when` re-read it).
    A line still being written is held back until its newline. A log shorter than what was read starts over."""
    e = _LOGS.setdefault(path, [0, [], b""])
    try:
        with open(path, "rb") as f:
            if os.fstat(f.fileno()).st_size < e[0]:
                e[:] = [0, [], b""]
                for k in [k for k in _SCAN if k[0] == path]:
                    del _SCAN[k]
            f.seek(e[0])
            data = f.read()
    except OSError:
        return e[1]
    if data:
        e[0] += len(data)
        parts = (e[2] + data).split(b"\n")
        e[2] = parts.pop()
        e[1].extend(x.decode("ascii", "replace").rstrip("\r") for x in parts)
    return e[1]


def log_scan(path, pattern):
    """(match count, last matching line) of a log for a regex, scanning only lines not scanned before."""
    lines = log_lines(path)
    e = _SCAN.setdefault((path, pattern), [0, 0, None])
    rx = re.compile(pattern)
    for i in range(e[0], len(lines)):
        if rx.search(lines[i]):
            e[1] += 1
            e[2] = lines[i]
    e[0] = len(lines)
    return e[1], e[2]


def wait_until(test, timeout_s):
    """Polls test() until it returns a truthy value or the timeout passes; returns that value or None."""
    deadline = time.monotonic() + timeout_s
    for _ in range(int(timeout_s / POLL_S) + 2):
        got = test()
        if got:
            return got
        if time.monotonic() >= deadline:
            return None
        time.sleep(POLL_S)
    return None


def parse_state(text):
    fields = {}
    for tok in shlex.split(text.strip()) if text.strip() else []:
        k, sep, v = tok.partition("=")
        if sep:
            fields[k] = v
    return fields


def post(role, line, timeout_s=CMD_TIMEOUT_S):
    """Appends one command line; returns its seq (the game numbers lines from 1)."""
    with open(role.cmd, "a", encoding="ascii") as f:
        f.write(line + "\n")
    role.sent += 1
    if line.split()[0] == "goto":
        role.last_goto = (role.sent, timeout_s)
    return role.sent


def await_result(role, seq, timeout_s):
    """Waits for the game's result line for seq. Returns (ok, detail)."""

    def result():
        for x in log_lines(role_log(role)):
            m = DONE_RE.search(x) or SKIP_RE.search(x)
            if m and int(m.group(1)) == seq:
                return (m.group(2) == "done", m.group(2))
        return None

    got = wait_until(result, timeout_s)
    return got if got else (False, f"no result for cmd {seq} in {timeout_s:.0f} s")


def send(role, line, timeout_s=CMD_TIMEOUT_S):
    """Appends one command line, then waits for the game's result line for its seq. Returns (ok, detail)."""
    return await_result(role, post(role, line, timeout_s), timeout_s)


GATES = ("within=", "hit=", "focus=", "max-left=", "calm=", "walk=", "line=")
# card 316: the moves of a hit= look with walk=N while the muzzle line is blocked, in order (key, hold ms): forward
# toward the target first (run 3: a rise 350-550 uu in front of the muzzle), then sideways both ways.
WALK_MOVES = (("W", 600), ("A", 600), ("D", 1200), ("W", 600), ("A", 1200), ("D", 1200))
MUZZLE_BLOCKED = "muzzle line blocked"
# card 243 r9: voids that end the run under voidsoft too (wrong DLL, wrong map or pawn); a gone game is checked apart
TERMINAL_VOID = re.compile(r"VOID (?:\w+ )?(?:dll |wait state)")
RESERVE_FLOOR = 60  # card 243 r8: a clear that starts with less in its primary ammo type's reserve is a VOID
# The floor applies to the first ammo type on the clear's start row: the primary weapon's type (Morita on the C3M2
# route, Laser on FED_C1M1 after the armory; c181-b5 voided on a Morita-only floor).


def split_gates(verb, args):
    """The gate words of a command step (card 243 within=, card 264 hit= and focus=, card 265 max-left=) and the
    rest."""
    gates = {a.split("=", 1)[0]: a.split("=", 1)[1] for a in args if a.startswith(GATES)}
    if verb == "clear":
        gates.setdefault("max-left", "0")
        if "kill" in args:  # user rule 2026-11-13: `clear <radius> kill`, code kills, no fire: the kill row's gate only
            gates["kill"] = "1"
    if verb == "objective" and args[:1] == ["expect"]:  # card 284: a wrong active step voids the run, even under try
        gates["expect"] = "1"
    rest = [a for a in args if not a.startswith(GATES)]
    if verb == "look" and "focus" in gates:  # b14: the DLL scans the actor's bounds for this focus; the last word
        rest.append(f"focus={gates['focus']}")
    elif verb == "look" and "sweep" in rest and rest[:1] == ["actor"] and len(rest) > 1:
        # b229 (card 244 Stadium_SO1_D): the DLL's focus sweep without the void gate. c244b228a's stand bounds look hit
        # C_3 at 126 uu, pitch -26.2, focus none; c244b220a's gated look swept from the same stand to pitch -3.7 and
        # focused it. `sweep` sends focus=<the actor name> so the DLL steps the bounds points until the interactor
        # focuses it; a miss is focus none, not a VOID.
        rest = [a for a in rest if a != "sweep"]
        rest.append(f"focus={rest[1]}")
    return rest, gates


def last_row(role, rx):
    found = [m for m in map(rx.search, log_lines(role_log(role))) if m]
    return found[-1] if found else None


def check_gates(role, seq, ok, detail, gates):
    """VOID (fails even under try) when a gate does not hold for cmd seq's rows:
    within=R (card 243): the look row (`cmd: N look: ... at .. from ..: yaw .. pitch .., D uu`) has D over R;
    hit=P (card 264): `cmd: N aim: hit <name> (` has no name starting with P; card 316: also when the muzzle row
    (`cmd: N aim: muzzle at X Y Z, line clear to <name>, D uu`) is missing, `muzzle none`, or `line blocked by <name>
    at D uu` (the bullets leave from the muzzle, the aim trace from the camera);
    focus=P (card 264): `cmd: N aim: focus <name>` has no name starting with P;
    line=P (card 310 range, on a `look actor ... bounds`): the row `cmd: N aim: centre muzzle at X Y Z, line clear to
    <name>, D uu` (the card 316 muzzle line to the actor's bounds centre, no camera hit needed) is missing, or blocked,
    or names no actor starting with P;
    max-left=N (card 265, set on every clear): the row `cmd: N clear: killed K, left L within` has L over N, or the
    clear's start row (`cmd: N clear: start, ... ammo <Type>=map/count/max ...`, printed before it fires) has the
    primary (first) ammo type's count under RESERVE_FLOOR (card 243 r8: the one floor; it reads the pawn's own AmmoMap and never
    fights);
    calm=S (card 243 r10, on a clear): the host log has a melee hit on the role's pawn (`playlog: host t=T hit slot
    <0 host, 1 client> ... instigator BP_AI_...`) with T in the S seconds up to the clear row's [ ms] stamp (both games
    stamp with one GetTickCount64 clock): the clear ended quiet or dropped a bug while a bug was on the pawn."""
    if not gates:
        return ok, detail
    if not ok:
        return False, f"VOID cmd {seq}: {detail}"
    notes = [detail]
    if "within" in gates:
        span = float(gates["within"])
        m = last_row(role, re.compile(rf"cmd: {seq} look: .* at .* from .*, (\d+) uu\s*$"))
        if not m:
            return False, f"VOID look {seq}: no distance row ({detail})"
        if int(m.group(1)) > span:
            return False, f"VOID look {seq}: {m.group(1)} uu over within {span:.0f}"
        notes.append(f"{m.group(1)} uu within {span:.0f}")
    if "hit" in gates or "focus" in gates:
        hit = last_row(role, re.compile(rf"cmd: {seq} aim: (hit (\S+) \(.*|no hit .*)$"))
        focus = last_row(role, re.compile(rf"cmd: {seq} aim: focus (\S+)"))
        if not hit or not focus:
            return False, f"VOID look {seq}: no aim rows ({detail})"
        if "hit" in gates and not (hit.group(2) or "").startswith(gates["hit"]):
            return False, f"VOID look {seq}: aim {hit.group(1)}, focus {focus.group(1)}, not hit={gates['hit']}"
        if "hit" in gates:
            muzzle = last_row(role, re.compile(rf"cmd: {seq} aim: muzzle (none|at .*, line (clear to|blocked by) .*)$"))
            if not muzzle or muzzle.group(2) != "clear to":
                got = muzzle.group(1) if muzzle else "no muzzle row"
                why = MUZZLE_BLOCKED if muzzle and muzzle.group(2) else "no muzzle line"
                return False, f"VOID look {seq}: {why}: aim {hit.group(1)}, muzzle {got}"
            notes.append(f"muzzle {muzzle.group(1)}")
        if "focus" in gates and not focus.group(1).startswith(gates["focus"]):
            return False, f"VOID look {seq}: aim {hit.group(1)}, focus {focus.group(1)}, not focus={gates['focus']}"
        notes.append(f"aim {hit.group(1)}, focus {focus.group(1)}")
    if "line" in gates:
        m = last_row(role, re.compile(rf"cmd: {seq} aim: centre muzzle (none|at .*, line (clear to|blocked by) (\S+).*)$"))
        if not m or m.group(2) != "clear to" or not m.group(3).startswith(gates["line"]):
            return False, f"VOID look {seq}: centre line {m.group(1) if m else 'missing'}, not line={gates['line']}"
        notes.append(f"centre muzzle {m.group(1)}")
    if "max-left" in gates and "kill" in gates:
        m = last_row(role, re.compile(rf"cmd: {seq} clear kill: (\d+ killed, (\d+) left\b.*)$"))
        if not m:
            return False, f"VOID clear {seq}: no clear kill row ({detail})"
        if int(m.group(2)) > int(gates["max-left"]):
            return False, f"VOID clear {seq}: {m.group(1)}, over max-left={gates['max-left']}"
        notes.append(m.group(1))
    elif "max-left" in gates:
        m = last_row(role, re.compile(rf"cmd: {seq} clear: (killed \d+, left (\d+) within .*)$"))
        if not m:
            return False, f"VOID clear {seq}: no clear row ({detail})"
        if int(m.group(2)) > int(gates["max-left"]):
            return False, f"VOID clear {seq}: {m.group(1)}, over max-left={gates['max-left']}"
        r = re.search(r"reserve (-?\d+), rounds used (-?\d+)", m.group(1))
        if not r:
            return False, f"VOID clear {seq}: {m.group(1)}, no reserve"
        s = last_row(role, re.compile(rf"cmd: {seq} clear: start, .* ammo (AmmunitionType\.\w+)=-?\d+/(-?\d+)/"))
        if not s:
            return False, f"VOID clear {seq}: no ammo type on its start row ({detail})"
        if int(s.group(2)) < RESERVE_FLOOR:
            return False, f"VOID clear {seq}: started with {s.group(1)} {s.group(2)}, under {RESERVE_FLOOR}"
        notes.append(f"started with {s.group(1)} {s.group(2)}, ended with reserve {r.group(1)}")
        notes.append(m.group(1))
    if "calm" in gates:
        hit = melee_before(role, seq, float(gates["calm"]))
        if hit:
            return False, f"VOID clear {seq}: melee hit on the pawn {hit} before the clear's end"
        notes.append(f"no melee hit in the last {gates['calm']} s")
    return True, ", ".join(notes)


# The host's card hit rows: slot 0 its own pawn, slot 1 the client's copy, slot 2 client2's copy (it joins second).
MELEE_SLOT = {"host": 0, "client": 1, "client2": 2}


def melee_before(role, seq, secs):
    """'<ms> ms' for the last BP_AI_ hit row on the role's pawn in the host log within secs before the clear row of
    cmd seq, or None (card 243 r10 calm= gate)."""
    rx = re.compile(rf"cmd: {seq} clear: killed ")
    end = None
    for line in log_lines(role_log(role)):
        if rx.search(line):
            m = STAMP_RX.match(line)
            end = int(m.group(1)) if m else None
    if end is None:
        return "with no clear row stamp"
    hit_rx = re.compile(rf"playlog: host t=(\d+) hit slot {MELEE_SLOT[role.name]} .*instigator BP_AI_")
    last = None
    for line in log_lines(role_log(Role(role.bin_dir, "host"))):
        h = hit_rx.search(line)
        if h and end - secs * 1000 <= int(h.group(1)) <= end:
            last = int(h.group(1))
    return f"{end - last} ms" if last is not None else None


def send_gated(role, line, timeout_s, gates):
    """The command with its gates. Card 316: a hit= look with walk=N whose only fault is a blocked muzzle line walks
    (real input, WALK_MOVES in order, at most N moves) and looks again after each move, until the gates hold."""
    seq = post(role, line, timeout_s)
    ok, detail = check_gates(role, seq, *await_result(role, seq, timeout_s), gates)
    moved = []
    for key, ms in WALK_MOVES[: int(gates.get("walk", "0"))]:
        if ok or MUZZLE_BLOCKED not in detail:
            break
        kok, kdetail = send(role, f"key {key} {ms}")
        if not kok:
            return False, f"VOID walk {key} {ms}: {kdetail} (after {detail})"
        moved.append(f"{key} {ms}")
        seq = post(role, line, timeout_s)
        ok, detail = check_gates(role, seq, *await_result(role, seq, timeout_s), gates)
    return ok, detail + (f"; walked {', '.join(moved)}" if moved else "")


# b12 item 2 (b11 C1M1 soak: E pressed on a zipline at 3328 uu, 133 rounds fired at walls after failed gates):
# an action step is skipped while the role's last look voided its hit= or focus= gate. The gate a step follows is
# the role's last look: a look with hit= or focus= sets it (void or held), a look without either clears it (no
# gate), and a host `objective expect` (a new step) clears it on every role. Applies under voidsoft, try, both and &.
ACTION_VERBS = ("key", "axis", "winkey")


def note_gate(roles, role, verb, args, ok, detail):
    """After a command step: track the role's look gate (b12 item 2)."""
    if verb == "look":
        gated = any(a.startswith(("hit=", "focus=", "line=")) for a in args)
        role.gate_void = detail if gated and not ok and detail.startswith("VOID") else None
    elif verb == "objective" and args[:1] == ["expect"] and ok:
        for r in roles.values():
            r.gate_void = None


def gate_skip(targets, verb):
    """'skipped ...' when an action verb follows a voided look gate on any target role, else None (b12 item 2)."""
    if verb not in ACTION_VERBS:
        return None
    hit = [f"{r.name}: {r.gate_void}" for r in targets if r.gate_void]
    return f"SKIPPED {verb}: last look gate voided ({'; '.join(hit)})" if hit else None


def command_line(verb, args, prefix):
    """The command line for `verb args` (shot names get the run prefix) and its result timeout."""
    if verb == "shot" and len(args) == 1 and prefix:
        args = [f"{prefix}-{args[0]}"]
    default = {"goto": GOTO_TIMEOUT_S, "clear": CLEAR_TIMEOUT_S}.get(verb, CMD_TIMEOUT_S)
    if verb == "objective" and args[:1] == ["wait"]:  # card 284: the game's own wait (default 60 s) plus slack
        wait_s = args[2] if len(args) > 2 and args[2].isdigit() else "60"
        default = int(wait_s) + CMD_TIMEOUT_S
    rest, timeout_s = split_timeout(args, default)
    return " ".join([verb] + rest), timeout_s


def run_background(roles, words, prefix):
    """`& <role> <command> ...` or `& both <command> ...` (card 124): post only; join or wait goto checks it."""
    if len(words) < 3 or (words[1] != "both" and words[1] not in roles) or words[2] not in COMMANDS:
        return False, "& takes <role> or both, then a command step"
    line, timeout_s = command_line(words[2], words[3:], prefix)
    targets = list(roles.values()) if words[1] == "both" else [roles[words[1]]]
    skip = gate_skip(targets, words[2])
    if skip:
        return True, skip
    started = []
    for r in targets:
        seq = post(r, line, timeout_s)
        r.pending.append((seq, line, timeout_s))
        started.append(f"{r.name} cmd {seq}")
    return True, "started " + ", ".join(started)


def run_join(roles, words):
    """`join [<role>]` (card 124): every & step of the role (or of all roles) has its result; fails if any failed."""
    if len(words) > 2 or (len(words) == 2 and words[1] not in roles):
        return False, "join takes no role or one role"
    targets = [roles[words[1]]] if len(words) == 2 else list(roles.values())
    ok, notes = True, []
    for r in targets:
        for seq, line, timeout_s in r.pending:
            good, detail = await_result(r, seq, timeout_s)
            ok = ok and good
            notes.append(f"{r.name} {seq} ({line.split()[0]}): {detail}")
        r.pending = []
    return ok, "; ".join(notes) if notes else "nothing to join"


def wait_goto(role, args, timeout_s):
    """`wait goto` (card 124): the role's last goto has ended; ok only when it reached its goal."""
    if args:
        return False, "wait goto takes only timeout=S"
    if role.last_goto is None:
        return False, "no goto was posted to this role"
    seq, _ = role.last_goto
    return await_result(role, seq, timeout_s)


def run_both(roles, words, prefix):
    """`both <command> ...` (card 118): the same command goes to every role at once, then the step waits for all
    of them, so host and client act together (both run up a ramp side by side)."""
    if len(words) < 2 or words[1] not in COMMANDS:
        return False, "both takes a command step without a role"
    skip = gate_skip(roles.values(), words[1])
    if skip:
        return True, skip
    args, gates = split_gates(words[1], words[2:])
    line, timeout_s = command_line(words[1], args, prefix)
    seqs = [(r, post(r, line, timeout_s)) for r in roles.values()]
    results = [(r.name, check_gates(r, seq, *await_result(r, seq, timeout_s), gates)) for r, seq in seqs]
    for r, (_, res) in zip(roles.values(), results):
        note_gate(roles, r, words[1], words[2:], *res)
    ok = all(res[0] for _, res in results)
    detail = "; ".join(f"{name}: {res[1]}" for name, res in results)
    void = any(res[1].startswith("VOID") for _, res in results)
    return ok, f"VOID {detail}" if void else detail  # a VOID role voids the step under try too


def split_timeout(args, default):
    rest = [a for a in args if not a.startswith("timeout=")]
    t = [float(a.split("=", 1)[1]) for a in args if a.startswith("timeout=")]
    return rest, (t[-1] if t else default)


def wait_state(role, args, timeout_s):
    if "void" in args:  # card 264: a wrong map or pawn ends the run here
        ok, detail = wait_state(role, [a for a in args if a != "void"], timeout_s)
        return ok, detail if ok else f"VOID wait state: {detail}"
    want = dict(a.split("=", 1) for a in args)
    last = {}

    def test():
        nonlocal last
        last = parse_state(read_text(role.state))
        return all(last.get(k) == v for k, v in want.items())

    if wait_until(test, timeout_s):
        return True, "state " + " ".join(f"{k}={last.get(k)}" for k in want)
    return False, "timeout; last state " + " ".join(f"{k}={last.get(k, '?')}" for k in want)


def split_count(args):
    rest = [a for a in args if not a.startswith("count=")]
    n = [int(a.split("=", 1)[1]) for a in args if a.startswith("count=")]
    return rest, (n[-1] if n else 1)


def nth_match(role, rx, count, skip=0):
    """The count-th line of the role's log that matches rx, or None while fewer lines match. The first `skip` lines
    of the log are not searched (`wait log ... new`)."""
    hits = [line for line in log_lines(role_log(role))[skip:] if rx.search(line)]
    return hits[count - 1] if len(hits) >= count else None


def wait_log(role, args, timeout_s):
    args, count = split_count(args)
    new = "new" in args[1:]
    since_mark = "since-mark" in args[1:]
    args = args[:1] + [a for a in args[1:] if a not in ("new", "since-mark")]
    if len(args) != 1 or count < 1 or (new and since_mark):
        return False, "wait log takes one quoted regex, count >= 1 and an optional new or since-mark"
    rx = re.compile(args[0])
    # since-mark: lines from the role's last mark step, so a line written while an earlier step ran (a held key)
    # still counts.
    skip = len(log_lines(role_log(role))) if new else (role.mark if since_mark else 0)
    line = wait_until(lambda: nth_match(role, rx, count, skip), timeout_s)
    if line:
        last_groups[:] = list(rx.search(line).groups())
    return (True, line.strip()[:160]) if line else (False, f"timeout after {timeout_s:.0f} s")


DRIVE_START_S = time.time()  # wait file: a file older than this replay is a stale one from an earlier run
REPO_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def wait_file(role, args, timeout_s):
    """`wait file <path>`: the file (relative to the repo) exists and was written after this replay started. An
    outside script releases the step this way (c569b395b: the capture script writes it after 20 clean seconds,
    replacing a fixed quiet hold)."""
    if len(args) != 1:
        return False, "wait file takes one path"
    path = os.path.join(REPO_DIR, args[0])

    def test():
        try:
            return os.stat(path).st_mtime >= DRIVE_START_S
        except OSError:
            return False

    if wait_until(test, timeout_s):
        return True, f"file {args[0]}"
    return False, f"timeout after {timeout_s:.0f} s; no fresh {args[0]}"


def wait_near(role, args, timeout_s):
    """`wait near x y z r`: the state's own-pawn x y z is within r uu of the point (card 107)."""
    if len(args) != 4:
        return False, "wait near takes <x> <y> <z> <radius>"
    want = [float(a) for a in args]
    last = [float("inf")]

    def test():
        st = parse_state(read_text(role.state))
        try:
            d = sum((float(st[k]) - want[i]) ** 2 for i, k in enumerate("xyz")) ** 0.5
        except (KeyError, ValueError):
            return False
        last[0] = d
        return d <= want[3]

    ok = wait_until(test, timeout_s)
    return (bool(ok), f"{'within' if ok else 'timeout; last'} {last[0]:.0f} uu of ({args[0]}, {args[1]}, {args[2]})")


STAND_GAP_S = 0.5  # card 242: the two position reads of a stand check (the state file is rewritten every 250 ms)


def wait_stand(role, args, timeout_s):
    """`wait stand x y z r`: within r uu of the point and two reads STAND_GAP_S apart equal (card 242). A timeout is
    VOID, so failsoft cannot carry a scenario past a stand the pawn never reached. z `any` (card 243 r6): the
    distance is x y only, for a stand whose rest z was never measured; the row still prints the settled point."""
    if len(args) != 4:
        return False, "wait stand takes <x> <y> <z> <radius>"
    dims = 2 if args[2] == "any" else 3
    want = [float(a) for a in (args[:2] + ["0"] + args[3:] if dims == 2 else args)]
    last = ["no state"]

    def pos():
        st = parse_state(read_text(role.state))
        try:
            return tuple(float(st[k]) for k in "xyz")
        except (KeyError, ValueError):
            return None

    def test():
        a = pos()
        if a is None:
            return False
        d = sum((a[i] - want[i]) ** 2 for i in range(dims)) ** 0.5
        last[0] = f"{d:.0f} uu at ({a[0]:.0f}, {a[1]:.0f}, {a[2]:.0f})"
        if d > want[3]:
            return False
        time.sleep(STAND_GAP_S)
        b = pos()
        if b != a:
            last[0] += f", moving to {b}"
            return False
        return True

    ok = wait_until(test, timeout_s)
    if ok:
        return True, f"standing, {last[0]}, still over {STAND_GAP_S:.1f} s"
    return False, f"VOID stand ({args[0]}, {args[1]}, {args[2]}) r {args[3]} not held: last {last[0]}"


def substitute(words):
    """{gN} or {gN+k}: group N of the last matched wait log, plus k (card 107). A numeric group comes out as %.1f;
    a bare {gN} whose group is not a number (an actor name) comes out as its raw text (b16)."""
    def one(m):
        n = int(m.group(1))
        if n > len(last_groups) or last_groups[n - 1] is None:
            raise ValueError(f"no group {n} from the last wait log")
        g = last_groups[n - 1]
        if not m.group(2):
            try:
                return f"{float(g):.1f}"
            except ValueError:
                return g
        v = float(g) + float(m.group(2))
        return f"{v:.1f}"
    return [GROUP_RE.sub(one, w) for w in words]


# c245b220a/b221a line 659: the client looked 100 ms after the host warp, before the Brain Bug replicated
# ("no such actor"); the retry warps again, then looks.
RETRY_VOID = re.compile(r"VOID (?:clear|look) \d+|VOID cmd \d+: failed \(no such actor\)")
MAX_RETRY = 3


def retry_parts(words):
    """The N and step lists of `retry N a ;; b ;; ...`, or a reason string."""
    if len(words) < 3 or not words[1].isdigit() or not 0 < int(words[1]) <= MAX_RETRY:
        return f"retry takes N (1-{MAX_RETRY}) and steps split by ;;"
    parts, cur = [], []
    for w in words[2:]:
        if w == ";;":
            parts.append(cur)
            cur = []
        else:
            cur.append(w)
    parts.append(cur)
    if any(not p for p in parts):
        return "retry has an empty step"
    return int(words[1]), parts


def run_retry(roles, words, prefix):
    """`retry N a ;; b ;; ...` (card 243 r10): the list again, at most N more times, after a clear or look gate void."""
    got = retry_parts(words)
    if isinstance(got, str):
        return False, got
    n, parts = got
    notes = []
    for attempt in range(n + 1):
        ok, detail = True, "no steps"
        for p in parts:
            ok, detail = run_step(roles, p, prefix)
            if not ok:
                break
        if ok or not RETRY_VOID.search(detail) or attempt == n:
            done = f"attempt {attempt + 1}: {detail}"
            return ok, (f"VOID {done}" if detail.startswith("VOID") else done) + "".join(notes)
        notes.append(f"; attempt {attempt + 1} {detail}")
    return False, "unreachable"


def run_unless(roles, words, prefix):
    """`unless <role> log "<regex>" count=N <step>`: the step runs only while fewer than N log lines match."""
    if len(words) < 6 or words[1] not in roles or words[2] != "log" or not words[4].startswith("count="):
        return False, "unless takes <role> log \"<regex>\" count=N <step>"
    count = int(words[4].split("=", 1)[1])
    if nth_match(roles[words[1]], re.compile(words[3]), count):
        return True, "skipped"
    return run_step(roles, words[5:], prefix)


def run_stuck(roles, words):
    """`stuck <role> log "<regex>" count=N`: (True, "STUCK ...") once N lines match; the run loop then skips to the next
    "# ===" stop comment. The regex was substituted ({gN}) by run_step."""
    if len(words) != 5 or words[1] not in roles or words[2] != "log" or not words[4].startswith("count="):
        return False, "stuck takes <role> log \"<regex>\" count=N"
    count = int(words[4].split("=", 1)[1])
    n, _ = log_scan(role_log(roles[words[1]]), words[3])
    if n >= count:
        return True, f"STUCK: {n} of {count} lines match"
    return True, f"not stuck: {n} of {count} lines match"


def run_when(roles, words, prefix):
    """`when <role> last "<regex>" "<test>" <step>`: the step runs only when the last matching line passes <test>."""
    if len(words) < 6 or words[1] not in roles or words[2] != "last":
        return False, "when takes <role> last \"<regex>\" \"<test>\" <step>"
    _, last = log_scan(role_log(roles[words[1]]), words[3])
    if last is None or not re.search(words[4], last):
        return True, "skipped"
    return run_step(roles, words[5:], prefix)


STAMP_RX = re.compile(r"^\[\s*(\d+) ms\]")


def first_stamp(role, rx):
    """The [ ms] stamp of the first line of the role's log that matches rx, or None."""
    for line in log_lines(role_log(role)):
        if rx.search(line):
            m = STAMP_RX.match(line)
            return int(m.group(1)) if m else None
    return None


def run_void(roles, words):
    """`void unless <roleA> log "<rxA>" before <roleB> log "<rxB>"`: (False, "VOID ...") when B came first.
    `void unless <role> dll <hash16>` (card 264): VOID unless the role's deployed dwmapi.dll has that sha256 prefix."""
    if len(words) == 5 and words[1] == "unless" and words[2] in roles and words[3] == "dll":
        path = os.path.join(roles[words[2]].bin_dir, "dwmapi.dll")
        try:
            with open(path, "rb") as f:
                got = hashlib.sha256(f.read()).hexdigest()[:16]
        except OSError as e:
            return False, f"VOID dll {path}: {e}"
        if got != words[4]:
            return False, f"VOID {words[2]} dll {got}, not {words[4]}"
        return True, f"{words[2]} dll {got}"
    if len(words) != 9 or words[1] != "unless" or words[2] not in roles or words[3] != "log" or words[5] != "before" \
            or words[6] not in roles or words[7] != "log":
        return False, "void takes unless <role> log \"<regex>\" before <role> log \"<regex>\""
    a = first_stamp(roles[words[2]], re.compile(words[4]))
    b = first_stamp(roles[words[6]], re.compile(words[8]))
    if b is not None and (a is None or b < a):
        return False, f"VOID {words[6]} line at {b} ms came before {words[2]} line at {a} ms"
    return True, f"{words[2]} at {a} ms, {words[6]} at {b} ms: order kept"


PICKUP_WALK_S = 20  # a walk of at most a few hundred uu; halt() below ends one that hangs


def pickup(role, args):
    """`<role> pickup <class> <max>` (card 243 r7, spawner drop walk): goto the nearest <class> actor only when the
    path is at most max uu (the goto start row's length); a longer one, or one not done in PICKUP_WALK_S, is ended
    by halt() (a goto to the pawn's own point). Ok unless the halt has no state: a drop out of reach is not a fault;
    the next clear's start row judges the floor."""
    if len(args) != 2:
        return False, "pickup takes <class> <max uu>"
    cls, most = args[0], float(args[1])
    seq = post(role, f"goto actor {cls} 30", PICKUP_WALK_S)
    rx = re.compile(rf"cmd: {seq} goto: from .* points, (\d+) uu, walk")
    row = wait_until(lambda: last_row(role, rx) or result_of(role, seq), CMD_TIMEOUT_S)
    m = row if isinstance(row, re.Match) else None
    if m and int(m.group(1)) <= most:
        ok, detail = await_result(role, seq, PICKUP_WALK_S)
        if ok:
            return True, f"walked {m.group(1)} uu: {detail}"
    ok, detail = halt(role)
    if not ok:
        return False, detail
    return True, f"no walk: {'path ' + m.group(1) + ' uu' if m else 'no start row'} (max {most:.0f}), {detail}"


HALT_UU = 150  # the halt goto's radius: inside the DLL's 10-5000, so a pawn at its own point has reached it


def halt(role):
    """`<role> halt` (card 243 r9): end the role's running goto with a goto to the pawn's own point, radius HALT_UU,
    reached on its first tick. The DLL ends the running goto ("replaced") before it starts the new one
    (testrun.cpp Op::Goto), so the walk stops even when the halt goto fails to plan; ok unless there is no state to
    read. Replaces `goto actor <class> 100000`, which the DLL skips (radius over 5000), so the walk went on."""
    st = parse_state(read_text(role.state))
    try:
        here = " ".join(f"{float(st[k]):.0f}" for k in "xyz")
    except (KeyError, ValueError):
        return False, "VOID halt: no state to read the pawn's point"
    seq = post(role, f"goto {here} {HALT_UU}", CMD_TIMEOUT_S)
    ok, detail = await_result(role, seq, CMD_TIMEOUT_S)
    return True, f"halt goto {here} {HALT_UU} {'ok' if ok else 'failed'}: {detail}"


def result_of(role, seq):
    """The (ok, detail) result of cmd seq if the game wrote it, else None."""
    for x in log_lines(role_log(role)):
        r = DONE_RE.search(x) or SKIP_RE.search(x)
        if r and int(r.group(1)) == seq:
            return (r.group(2) == "done", r.group(2))
    return None


# Card 504: a look gate's void under try (hit=, focus=, within=, line=, walk=; a gated look whose command failed).
LOOK_GATE_VOID = re.compile(r"VOID (?:look \d+|walk )")
OTHER_VOID = re.compile(r"VOID (?:clear|wait|objective|dll|state)")
NOT_HELD = "TRY-FAIL gate not held: "


def look_gate_void(roles, words, detail):
    """True when the VOID `detail` of step `words` is a look gate that did not hold (card 504), not a clear, wait,
    objective or DLL void."""
    head = detail.split("; attempt ")[0]  # a retry's earlier attempts follow its result
    if OTHER_VOID.search(head):
        return False
    if LOOK_GATE_VOID.search(head):
        return True
    verb = words[1] if len(words) > 1 and (words[0] in roles or words[0] == "both") else None
    return verb == "look" and "VOID cmd " in head


# Card 504 (user 2026-11-19: "each bughole gets killed the same"): `host objective do [<id>]` runs the routine of the
# step's kind (docs/reference/objective-routines.md) from the existing gated blocks: expect <id> (if named), now (the
# current step's kind and actor[0] row), then the kind's routine (its tries run the go). A kind with no routine yet is a VOID.
NOW_CUR_RX = re.compile(r"cmd: (\d+) objective now: parent \S+; current (\S+) kind (\w+)")
NOW_ACTOR_RX = re.compile(r"cmd: (\d+) objective now:   actor\[0\] (\S+) \((\S+)\)")
# c569b390b: after FED_C1M1 Assault entry the script hides Assault_SO0 (marker 0, never completed) and the
# DLL still names it current while Assault_SO2 is the doable, marker-1 sibling; objective do expects that sibling first.
NOW_DOABLE_RX = re.compile(r"cmd: (\d+) objective now: parent \S+; doable (\S+) kind (\w+) .*marker 1$")
HOLE_RX = re.compile(r"cmd: (\d+) look: hole (\S+) \(\S+\) at (-?\d+) (-?\d+) (-?\d+), nearest of")
AIM_RX = re.compile(r"cmd: (\d+) aim: (?:hit (\S+) \(.*?\)(?: guid 0x[0-9a-f]+)? at (-?\d+) (-?\d+) (-?\d+), "
                    r"(\d+) uu, pitch (-?[\d.]+)|no hit)")
NEST_TRIES = (0, 450, 300)  # try 1 from the go stand (600 uu); tries 2 and 3 warp this far short of the nest
NEST_HOLE_NEAR_UU = 300     # gate: the crosshair hits the nest this close to its hole point
NEST_RANGE_UU = 1600        # gate: and no further than a frag lob reaches
NEST_WAIT_S = 10            # the Complete row after a throw


def note(text):
    """A routine row in the .drive log (main's say format, no run clock)."""
    print(f"{time.strftime('%H:%M:%S')} {text}", flush=True)


def seq_rows(role, seq, rx):
    return [m for m in map(rx.search, log_lines(role_log(role))) if m and int(m.group(1)) == seq]


def nest_gate(host, nest, shot):
    """Aim at the nest's grenade hole, then the aim-proof shot; None when the crosshair gate holds, else why not."""
    ok, detail = send(host, f"look actor {nest} 0 hole")
    seq = host.sent
    if not ok:
        return f"look: {detail}"
    send(host, shot)  # aim-proof-shots rule: a shot after every teleport and look, before the gate
    hole, aim = seq_rows(host, seq, HOLE_RX), seq_rows(host, seq, AIM_RX)
    if not hole or not aim:
        return f"no {'hole' if not hole else 'aim'} row for look {seq}"
    hole, aim = hole[-1], aim[-1]
    if aim.group(2) is None:
        return "crosshair hits nothing"
    if not aim.group(2).startswith(nest):
        return f"crosshair hits {aim.group(2)} at {aim.group(6)} uu, pitch {aim.group(7)}, not {nest}"
    d = math.dist([float(hole.group(i)) for i in (3, 4, 5)], [float(aim.group(i)) for i in (3, 4, 5)])
    if d > NEST_HOLE_NEAR_UU or int(aim.group(6)) > NEST_RANGE_UU:
        return f"hit {aim.group(2)} {d:.0f} uu from hole {hole.group(2)} (near {NEST_HOLE_NEAR_UU}), " \
               f"{aim.group(6)} uu off (range {NEST_RANGE_UU}), pitch {aim.group(7)}"
    return None


def routine_nest(roles, step, nest, prefix):
    """Host: clear, aim at the hole, gate, fill, throw a frag (G), wait for the Complete row; at most 3 tries."""
    host, why = roles["host"], "no try"
    for n, short in enumerate(NEST_TRIES, 1):
        ok, detail = send(host, f"warp actor {nest} {short}") if short else send(host, "objective go all")
        if not ok:
            why = f"{f'warp {short}' if short else 'go all'}: {detail}"
            note(f"objective go: nest gate failed: try {n} {why}")
            continue
        send(host, "clear 3000 kill", CLEAR_TIMEOUT_S)
        # the nest class name is 46 chars, over the 40-char shot name cap: the step id names the shot
        why = nest_gate(host, nest, command_line("shot", [shot_tag(step, n)], prefix)[0])
        if why:
            note(f"objective go: nest gate failed: try {n} {why}")
            continue
        send(host, "grenade fill 2")  # at least one frag in hand before the throw
        ok, detail = send(host, "key G 200")
        if not ok:
            why = f"key G: {detail}"
            continue
        ok, detail = send(host, f"objective wait {step} {NEST_WAIT_S}", NEST_WAIT_S + CMD_TIMEOUT_S)
        if ok:
            return True, f"objective do: nest {step} {nest} complete on try {n}"
        why = f"thrown, no Complete row in {NEST_WAIT_S} s ({detail})"
        note(f"objective go: nest try {n}: {why}")
    return False, f"VOID objective do: nest {step} {nest}: 3 tries, last: {why}"


ROUTINE_TRIES = 3     # every routine: at most 3 tries, then VOID with the last gate reason
STEP_WAIT_S = 10      # presence and use: the Complete row after the stand or the press
TAKE_WAIT_S = 10      # pickup: the take (card 495) or Complete row after one player's E
PICKUP_NEAR_UU = 120  # pickup: walk in this close from the go stand (c502 C1M1 recipe)
PLAYERS = ("host", "client", "client2")


# Card 513 (c504k2 Metro_SO2, lesson 16): a step whose trigger ends the level logs no Complete row; the level-end
# widget row counts as that step's done.
LEVEL_END_RX = r"card018: host SetInputMode_UIOnlyEx called by BP function ExecuteUbergraph_WB_TrooperLevelEnd"
# Card 513: `objective do` runs per step id, counted here; shot tags carry the count so a second do of the same step
# keeps the first do's shots (c504k2: do 2 overwrote Metro_SO2 try1-3).
STEP_DOS = {}


def shot_tag(step, n):
    return f"c504-{step}-d{STEP_DOS.get(step, 1)}-try{n}"


def complete_rx(step):
    return rf"card049: host t=\d+ Complete\w*Objective\((?:\w+, )?{step}\)"


def done_rx(step):
    """The step's Complete row, or its native row: a step completed through its parent has no card049 row of its own
    (c504b4: LOS_O04_SO02 only as card097 "native CompleteObjective LOS_O04/LOS_O04_SO02")."""
    return complete_rx(step) + rf"|card097: host t=\d+ native CompleteObjective (?:\w+/)?{step} " + "|" + LEVEL_END_RX


def done_since(host, step, start):
    return any(re.search(done_rx(step), line) for line in log_lines(role_log(host))[start:])


# Lesson 55 (c569b392a FED_C1M1 Assault_SO0): entering a step's volume can make the level script hand the step off.
# It hides the step (`SetObjectiveVisibility(step) bools 00`), adds siblings and completes one of them
# (Assault_SO1, the same presence C_2) in the same ms; the step itself never gets a Complete row. A hide followed
# within HANDOFF_MS by a sibling's CompleteSubObjective under the same parent counts as the step's done. The 15 s
# marker blinks (Assault_SO2, ShootingRange_SO0 bools 11/01) never hide with 00.
# Lesson 56 (c569b392a ShootingRange_SO0, presence C_24): the hand-off can be an Add instead. Entering C_24 hides
# the step (bools 00) and adds ShootingRange_SO1 (the Morita pickup) 2 ms later; SO0 only completes at the range's
# end (card097 ObjectiveComplete), so every earlier soak VOIDed it. A sibling Add after the hide counts the same.
HANDOFF_MS = 50
HIDE_RX = r"card049: host t=(\d+) SetObjectiveVisibility\({}\) bools 00"
SIB_DONE_RX = re.compile(r"card049: host t=(\d+) (?:Complete|Add|AddCount)SubObjective\((\w+), (\w+)\)")


def handed_off(host, step, start):
    """The sibling step id the script completed or added right after hiding <step> since line <start>, else None."""
    lines = log_lines(role_log(host))
    parents = [m.group(1) for m in map(re.compile(rf"AddSubObjective\((\w+), {step}\)").search, lines) if m]
    if not parents:
        return None
    hide_rx = re.compile(HIDE_RX.format(step))
    hides = [int(m.group(1)) for m in map(hide_rx.search, lines[start:]) if m]
    for m in map(SIB_DONE_RX.search, lines[start:]):
        if m and m.group(2) == parents[-1] and m.group(3) != step and \
                any(0 <= int(m.group(1)) - t <= HANDOFF_MS for t in hides):
            return m.group(3)
    return None


# Card 504 stay phase (senior design 2026-11-19, lesson 10): kind marker steps complete on presence, on their wave
# end (C2M1 LOS_O01_SO01: card503 wave end, Complete in the same ms as a clear) or by script after a hold (C2M1
# LOS_O04_SO02: 57.6-70.0 s after both players entered, c504b2-b4). One routine covers all three.
# Card 513 item 5 (senior 2026-11-19, c504k2 Stadium_SO2 closed 173 s after its Add, past the 120 s cap): the last
# try's stay follows encounter progress, not a fixed time. A period whose clear killed more than 0, or that logged a
# wave (card111) or StartEncounter (card245) row, keeps the stay going; STAY_QUIET periods in a row with neither end
# it; STAY_S is the hard cap. Static source (kis.py is lost; cards 244/247 hold the only tables): Stadium_SO2 has no
# timer, it completes inside the Add Stadium_SO4 path, which the script retries every 10 s until the stadium fight
# allows it; Stadium_SO4 (count 2) counts encounter EM_2 ends. In c504k2 the Stadium_SO2 window never had 3 quiet
# periods in a row (kills or waves every period or two), nor did c504b4 C2M1 LOS_O04_SO02's 59 s hold.
STAY_S = 300          # hard cap of the last try's stay
STAY_QUIET = 3        # periods in a row with no kill and no wave/encounter row end the stay
# Card 604 (c604b458a line 229, lesson 96): Stadium_SO2 closes after the stadium fight chain plus a fixed 20 + 18 + 5 s
# show (lesson 81) that has no kill and no wave row. With few bugs the stay went quiet at 47 s, 3 s before the close
# (c604b459a closed at 50 s because its kills kept the stay going). A quiet stay ends only after this long.
STAY_QUIET_MIN_S = 60
STAY_KILL_RX = re.compile(r"cmd: \d+ clear kill: (\d+) killed")
STAY_WAVE_RX = re.compile(r"card111: wave budget |card245: host t=\d+ StartEncounter on ")
STAY_EVERY_S = 5      # stay period: one `clear 4000 kill`, then up to 5 s on the step's done row
# Card 512 (c504k1 Bridge_Marker/SO1, lesson 13): a guide marker's own stand is not the step; the trigger was the
# third presence try (13086 uu off). A try that is not the last gets one stay period (c504k1 LZ_SO2 completed on the
# first clear after the wait), then the next go stands at the next presence; only the last try runs the full stay.
STAY_TRY_S = STAY_EVERY_S
STAY_LEFT_UU = 1500   # farther than this from where the stay began = left the stand (c504b4 off-navmesh drift 461 uu)
GO_DRIFT_RX = re.compile(r"moved (\d+) uu after the warp")
USE_DRIFT_UU = 200     # a use stand may settle this far after the warp; the focus gate still decides


def pawn_xyz(role):
    st = parse_state(read_text(role.state))
    try:
        return tuple(float(st[k]) for k in "xyz")
    except (KeyError, ValueError):
        return None


def stay(roles, step, start, cap_s=STAY_S):
    """Every player holds the stand; each period clears by code and waits on the step's done row since `start`.
    Returns (done, left, how): left is True when a player moved over STAY_LEFT_UU from the stand (the caller
    re-goes); neither done nor left means the cap (the caller voids)."""
    host = roles["host"]
    stands = {w: pawn_xyz(roles[w]) for w in PLAYERS if w in roles}
    t0, k, quiet = time.monotonic(), 0, 0
    for k in range(1, cap_s // STAY_EVERY_S + 1):
        at = len(log_lines(role_log(host)))
        send(host, "clear 4000 kill", CLEAR_TIMEOUT_S)
        host.mark = start
        ok, _ = wait_log(host, [done_rx(step), "since-mark"], STAY_EVERY_S)
        if ok:
            return True, False, f"stay, {time.monotonic() - t0:.0f} s, {k} clears"
        period = log_lines(role_log(host))[at:]
        killed = sum(int(m.group(1)) for x in period for m in [STAY_KILL_RX.search(x)] if m)
        waves = sum(1 for x in period if STAY_WAVE_RX.search(x))
        quiet = 0 if killed or waves else quiet + 1
        if quiet >= STAY_QUIET and time.monotonic() - t0 >= STAY_QUIET_MIN_S:
            return False, False, f"stay quiet {time.monotonic() - t0:.0f} s, {k} clears, {quiet} periods with no " \
                                 f"kill and no wave row, no done row"
        for w, at in stands.items():
            now = pawn_xyz(roles[w])
            if at is not None and now is not None and math.dist(at, now) > STAY_LEFT_UU:
                return False, True, f"{w} left the stand ({math.dist(at, now):.0f} uu) after " \
                                    f"{time.monotonic() - t0:.0f} s, {k} clears"
    return False, False, f"stay cap {time.monotonic() - t0:.0f} s, {k} clears, no done row"


def step_go(host, home=False):
    """A try's go: `objective go all` warps every player onto the kind's stand (presence: inside the volume, else
    the go fails; use: the DLL steps the client, or the host alone, in to the switch). Then clear by code. None when
    the go holds, else why."""
    # Card 514: `home` stands at the step's own (first go) stand without counting a go.
    ok, detail = send(host, "objective go all home" if home else "objective go all")
    if not ok:
        return f"go all: {detail}"
    send(host, "clear 1500 kill", CLEAR_TIMEOUT_S)
    return None


def focus_gate(role, actor, shot, focus=None):
    """Look at the actor (origin, then bounds, each with the DLL focus scan), the aim-proof shot, then the focus gate:
    None when the interactor focuses an actor whose name starts with `focus` (default `actor`), else why not."""
    why = "no look"
    for form in ("", " bounds"):
        line = f"look actor {actor} 0{form} focus={focus or actor}"
        seq = post(role, line, CMD_TIMEOUT_S)
        ok, detail = check_gates(role, seq, *await_result(role, seq, CMD_TIMEOUT_S), {"focus": focus or actor})
        if ok:
            break
        why = f"look{form or ' origin'}: {detail}"
    send(role, shot)
    return None if ok else why


# Lesson 24 amend (c263g, senior 2026-11-19): a guide marker's wait ends when the sibling sharing its actor completes
# (`guide marker ended by`), but the script may hide the marker only after the players enter another presence volume
# (FED_C3M2 PowerPlant_Marker: the hide and the SO2..SO6 show came only after try 3 stood in BP_PlayerPresence_C_16,
# host line 17553). So the marker routine waits MARKER_HIDE_S for the script's next visibility row on the marker and
# else goes on to its next presence try.
MARKER_HIDE_S = 20


def marker_held(host, step, start, kind):
    """True when a kind marker wait ended only on its sibling and the script set no visibility on the marker within
    MARKER_HIDE_S after that row (c263g: the hide and the SO2..SO6 show come in one ms)."""
    ended = re.compile(r"c284: objective wait " + re.escape(step) + r": guide marker ended by ")
    lines = log_lines(role_log(host))
    at = next((start + k for k, x in enumerate(lines[start:]) if ended.search(x)), None)
    if kind != "marker" or at is None:
        return False
    shown = re.compile(r"card049: host t=\d+ SetObjectiveVisibility\(" + re.escape(step) + r"\) bools ")
    seen = wait_until(lambda: any(shown.search(x) for x in log_lines(role_log(host))[at:]), MARKER_HIDE_S)
    note(f"objective go: marker {step}: guide marker ended; visibility row " +
         ("seen" if seen else f"none in {MARKER_HIDE_S} s"))
    return not seen


def routine_presence(roles, step, actor, prefix, kind="presence"):
    """Every player into the volume (the go's inside check is the gate), the shot, the Complete row in STEP_WAIT_S,
    else the stay phase. A re-go only when the go failed or a player left the stand; the stay cap voids."""
    host, why = roles["host"], "no try"
    start = len(log_lines(role_log(host)))
    for n in range(1, ROUTINE_TRIES + 1):
        # Lesson 4 (c504s2): a script can complete the step after a try gave up; a new go would warp onto the next step.
        if n > 1 and done_since(host, step, start):
            return True, f"objective do: presence {step} {actor} complete before try {n}"
        if n > 1 and handed_off(host, step, start):
            return True, f"objective do: presence {step} {actor} handed off to {handed_off(host, step, start)} before try {n}"
        gate = step_go(host)
        send(host, command_line("shot", [shot_tag(step, n)], prefix)[0])
        drift = GO_DRIFT_RX.search(gate or "")
        if drift and int(drift.group(1)) <= STAY_LEFT_UU:
            # c504b4 LOS_O01_SO01: an off-navmesh marker stand settles 461 uu down; the players are still at the place.
            note(f"objective go: presence try {n}: go drift held ({gate})")
            gate = None
        last = n == ROUTINE_TRIES
        if gate:
            why = gate
            note(f"objective go: presence gate failed: try {n} {why}")
            if not last:
                continue
            # Card 516 fix 3 (c244b319a Stadium_SO2, lesson 33): a failed gate on the last try still gets the home go
            # and the stay; `continue` here voided a flag step 35 s before it closed.
            detail = f"go gate failed: {gate}"
        else:
            ok, detail = send(host, f"objective wait {step} {STEP_WAIT_S}", STEP_WAIT_S + CMD_TIMEOUT_S)
            if ok and not marker_held(host, step, start, kind):
                return True, f"objective do: presence {step} {actor} complete on try {n} (presence)"
            sib = None if ok else handed_off(host, step, start)
            if sib:
                return True, f"objective do: presence {step} {actor} handed off to {sib} on try {n} (script hid it, then completed or added {sib})"
            if ok:
                detail = f"guide marker ended, no visibility row in {MARKER_HIDE_S} s"
        # Card 516: fix 1 (c514a Bridge_Marker, 311 s at the try-1 stand) limited the home go to kind presence, which
        # dropped Stadium_SO2 (kind marker, a flag step, lesson 18). Fix 3 restores it for kind marker: guide markers
        # now end in the DLL wait (`handed on`, `zone guide passed`), so the home go only reaches real marker steps.
        if last and n > 1:
            # Card 514 (lesson 18, c513a Stadium_SO2): a step can close on a script flag that its own presence volume
            # sets (C_5 sets inStadium; Add Stadium_SO4 retries every 10 s). The long stay stands back in the step's
            # own volume, the first try's stand, not at the last try's next presence.
            why = step_go(host, home=True)
            if why:
                note(f"objective go: presence try {n}: home go failed: {why}")
        done, left, how = stay(roles, step, start, STAY_S if last else STAY_TRY_S)
        if done:
            return True, f"objective do: presence {step} {actor} complete on try {n} ({how})"
        why = f"no Complete row in {STEP_WAIT_S} s ({detail}), then {how}"
        note(f"objective go: presence try {n}: {why}" + ("" if last else "; next presence try"))
        if last:
            break
    return False, f"VOID objective do: presence {step} {actor}: last: {why}"


def routine_use(roles, step, actor, prefix):
    """The presser (the client when there is one: the go's on_stand pawn, stepped in to the switch; else the host)
    looks at the switch, focus gate, E, then the Complete row."""
    host, why = roles["host"], "no try"
    presser = roles.get("client", host)
    # soak3m1b366a: FED_C3M1 Beach_SO1_A-C actor[0] is `BP_Switch_C (BP_PanelSwitch_WithoutScreen_C)`, a name stem,
    # not a class. `look actor` matches an exact name or an exact class, so it found nothing ("no such actor") and
    # 0 of 18 rows were done. Look at the step's own instance (card070 row, BP_Switch_C_4) when there is one.
    actor = step_instance(host, step) or actor
    for n in range(1, ROUTINE_TRIES + 1):
        why = step_go(host)
        drift = GO_DRIFT_RX.search(why or "")
        if drift and int(drift.group(1)) <= USE_DRIFT_UU:
            # soak3m1b366b Beach_SO1_B: the switch stand settles 59 uu after the warp on every try, so the go gate
            # failed 3 of 3 tries in each of 48 rounds. The focus gate below is the real check that the presser
            # can use the switch, so a small settle is held as in routine_presence.
            note(f"objective go: use try {n}: go drift held ({why})")
            why = None
        why = why or focus_gate(presser, actor, command_line("shot", [shot_tag(step, n)], prefix)[0])
        if why:
            note(f"objective go: use gate failed: try {n} {why}")
            continue
        ok, detail = send(presser, "key E")
        if not ok:
            why = f"key E: {detail}"
            note(f"objective go: use gate failed: try {n} {why}")
            continue
        ok, detail = send(host, f"objective wait {step} {STEP_WAIT_S}", STEP_WAIT_S + CMD_TIMEOUT_S)
        if ok:
            return True, f"objective do: use {step} {actor} complete on try {n}"
        why = f"pressed, no Complete row in {STEP_WAIT_S} s ({detail})"
        note(f"objective go: use try {n}: {why}")
    return False, f"VOID objective do: use {step} {actor}: {ROUTINE_TRIES} tries, last: {why}"


def pickup_take(roles, who, step, actor, shot, go):
    """One player takes the pickup: from the go's use stand (every player beside this step's pickup, so the nearest
    of its class is the right one; only while the step is open: after its Complete the go would stand everyone at
    the next step) walk in, focus gate, E, then the host's take row (card 495) or the step's Complete row.
    (None, row) on a take, else (why, None)."""
    host, role = roles["host"], roles[who]
    why = step_go(host) if go else None
    if why:
        return why, None
    # soak1m1b366a: `goto actor X 120 timeout=30` in one post is 5 words, which the DLL parses as a coordinate goto
    # and skips ("bad coordinate or radius"). The walk is the goto, then the scenario's own `wait goto`.
    run_step(roles, [who, "goto", "actor", actor, str(PICKUP_NEAR_UU)], "")
    run_step(roles, [who, "wait", "goto", "timeout=30"], "")
    why = focus_gate(role, actor, shot)
    if why:
        return why, None
    host.mark = len(log_lines(role_log(host)))
    ok, detail = send(role, "key E")
    if not ok:
        return f"key E: {detail}", None
    rx = rf"card495: host t=\d+ (?:held|released) Pickup_Implementation on {actor}\S* .*taken by \d+ of \d+|" \
         + complete_rx(step)
    ok, detail = wait_log(host, [rx, "since-mark"], TAKE_WAIT_S)
    return (None, detail) if ok else (f"pressed, no take row in {TAKE_WAIT_S} s ({detail})", None)


def routine_pickup(roles, step, actor, prefix):
    """Each player takes their own copy (card 495, each-player-pickup: an early Complete or a mirror give does not
    count), at most 3 tries each. The go runs only while the step is open; once its Complete row is in, later
    players walk in from where they stand. Then the Complete row."""
    host = roles["host"]
    start = len(log_lines(role_log(host)))
    for who in (w for w in PLAYERS if w in roles):
        why = "no try"
        for n in range(1, ROUTINE_TRIES + 1):
            go = not any(re.search(complete_rx(step), line) for line in log_lines(role_log(host))[start:])
            why, row = pickup_take(roles, who, step, actor,
                                   command_line("shot", [f"c504-{step}-{who}{n}"], prefix)[0], go)
            if not why:
                break
            note(f"objective go: pickup gate failed: {who} try {n} {why}")
        if why:
            return False, f"VOID objective do: pickup {step} {actor}: {who} {ROUTINE_TRIES} tries, last: {why}"
        note(f"objective do: pickup {step}: {who} took it: {row[:160]}")
    ok, detail = send(host, f"objective wait {step} {STEP_WAIT_S}", STEP_WAIT_S + CMD_TIMEOUT_S)
    if not ok:
        return False, f"VOID objective do: pickup {step} {actor}: every player took it, no Complete row ({detail})"
    return True, f"objective do: pickup {step} {actor} complete"


# Card 504 satchel routine (senior draft 2026-11-19, composed from the c244-c4m1 lines 77-94 recipe; no DLL build).
# Steps whose actor[0] class has "Satchel" (BP_SatchelBuilding_*_C, BP_SatchelPlacementSite_C) are kind other in
# the DLL; objective_do sends them here by class.
SATCHEL_HOLD_MS = 3200     # c244: key E 3200 completes the plant (card456 completed 1)
SATCHEL_CLEAR_UU = 1200    # out of the blast before the wait
SATCHEL_WAIT_S = 20        # fuse plus BuildingDestroyed plus Complete
HOLD_RX = r"card456: client t=\d+ hold (?:released|interrupted) on SatchelSiteCAC_\S* .*completed (\d)"
LANDED_RX = r"card490: client t=\d+ pc 0 \S+ role 2: .*MovementMode 1,"  # card 515: the client walks again
LANDED_WAIT_S = 2  # best effort: card 490 rows are logged on change and capped per map


def satchel_take(roles):
    """Best effort: the client takes the nearest live satchel pickup. No take row is required (a full carrier gets
    none); the site's hold below is the gate that the client carries a charge."""
    host, client = roles["host"], roles["client"]
    ok, detail = send(host, "warp client BP_SatchelItemPickup_C 100 live")
    if not ok:
        return f"pickup warp: {detail}"
    seq = post(client, "look actor BP_SatchelItemPickup_C 0 bounds live focus=BP_SatchelItemPickup_C", CMD_TIMEOUT_S)
    ok, detail = check_gates(client, seq, *await_result(client, seq, CMD_TIMEOUT_S),
                             {"focus": "BP_SatchelItemPickup_C"})
    if not ok:
        return f"pickup focus: {detail}"
    send(client, "key E")
    return None


def satchel_plant(roles, shot):
    """From the go stand: the client to the nearest site (front, then behind), focus gate on SatchelSiteCAC_, hold E,
    then the client's card456 row must say completed 1. None on a plant, else why."""
    host, client = roles["host"], roles["client"]
    why = "no side"
    for side in ("front", "behind"):
        client.mark = len(log_lines(role_log(client)))
        ok, detail = send(host, f"warp client BP_SatchelPlacementSite_C 200 {side}")
        if not ok:
            why = f"site warp {side}: {detail}"
            continue
        # Card 515: the warp lifts the pawn by kC75WarpAbove; look and press E only once it walks again.
        landed, _ = wait_log(client, [LANDED_RX, "since-mark"], LANDED_WAIT_S)
        if not landed:
            note(f"satchel plant: {side}: no walking row within {LANDED_WAIT_S} s of the warp, going on")
        seq = post(client, "look actor BP_SatchelPlacementSite_C 0 focus=SatchelSiteCAC_", CMD_TIMEOUT_S)
        ok, detail = check_gates(client, seq, *await_result(client, seq, CMD_TIMEOUT_S), {"focus": "SatchelSiteCAC_"})
        if ok:
            break
        why = f"site focus {side}: {detail}"
    else:
        return why
    send(client, shot)
    send(host, "clear 1500 kill", CLEAR_TIMEOUT_S)
    client.mark = len(log_lines(role_log(client)))
    ok, detail = send(client, f"key E {SATCHEL_HOLD_MS}", SATCHEL_HOLD_MS / 1000 + CMD_TIMEOUT_S)
    if not ok:
        return f"key E {SATCHEL_HOLD_MS}: {detail}"
    ok, detail = wait_log(client, [HOLD_RX, "since-mark"], 5)
    if not ok:
        return f"no card456 hold row ({detail})"
    # wait_log cuts detail at 160 chars, before "completed N"; its full-line groups are in last_groups.
    return None if last_groups[:1] == ["1"] else f"hold not completed ({detail})"


TAKE_UU = 120   # c423 recipe: warp 120 uu from the pickup, then the focus look


def step_instance(host, step):
    """The step's actor instance from its card070 row (the now row's actor[0] names only the class)."""
    rx = re.compile(rf"card070: host t=\d+ {step} actors \d+: {step} (\S+)")
    found = [m.group(1) for m in map(rx.search, log_lines(role_log(host))) if m]
    return found[-1] if found else None


def routine_satchel_take(roles, step, actor, prefix):
    """C2M1 LOS_O03_SO02a-c (c504b4 trace): one satchel pickup per step, taken by the host (the c423 planter). Gate on
    the step's own Complete row (lesson 9), at most 3 tries, each with a fresh focus look. Focus none at 221 uu is
    card 510 (focus scan off the body bounds): noted, not worked around."""
    host, why = roles["host"], "no try"
    inst = step_instance(host, step)
    if inst is None:
        return False, f"VOID objective do: satchel-take {step}: no card070 actor row"
    start = len(log_lines(role_log(host)))
    for n in range(1, ROUTINE_TRIES + 1):
        if done_since(host, step, start):
            return True, f"objective do: satchel-take {step} {inst} complete before try {n}"
        send(host, "clear 3000 kill", CLEAR_TIMEOUT_S)
        ok, detail = send(host, f"warp actor {inst} {TAKE_UU}")
        if not ok:
            why = f"warp: {detail}"
            note(f"objective go: satchel-take try {n}: {why}")
            continue
        why = focus_gate(host, inst, command_line("shot", [shot_tag(step, n)], prefix)[0],
                         focus="BP_SatchelItemPickup_C")
        if not why:
            # Lesson 11 (c504b6): the gate matches focus= as a class prefix, so a neighbour pickup passes too;
            # the passing look is the last focus row, and its full name must be this step's own instance.
            focus = last_row(host, re.compile(r"cmd: \d+ aim: focus (\S+)"))
            got = focus.group(1) if focus else "no focus row"
            if got != inst:
                why = f"focus {got}, not {inst}"
        if why:
            note(f"objective go: satchel-take try {n}: {why}" + (" (card 510)" if "none" in why else ""))
            continue
        host.mark = len(log_lines(role_log(host)))
        send(host, "key E")
        ok, detail = wait_log(host, [complete_rx(step), "since-mark"], TAKE_WAIT_S)
        if ok:
            return True, f"objective do: satchel-take {step} {inst} complete on try {n}"
        why = f"key E, no Complete row in {TAKE_WAIT_S} s ({detail})"
        note(f"objective go: satchel-take try {n}: {why}")
    return False, f"VOID objective do: satchel-take {step} {inst}: {ROUTINE_TRIES} tries, last: {why}"


def routine_satchel(roles, step, actor, prefix):
    """Satchel building or site: go, clear, satchel pickup (best effort), go back to the stand, plant on the nearest
    site, step out of the blast, then the Complete row. The client plants (client-side hold is the co-op path the
    user plays; card 133 HoldDone reaches the host). At most 3 tries."""
    host, why = roles["host"], "no try"
    if "client" not in roles:
        return False, f"VOID objective do: satchel {step} {actor}: needs a client (client plant path)"
    start = len(log_lines(role_log(host)))
    for n in range(1, ROUTINE_TRIES + 1):
        # A late fuse completes the step after its try gave up; a new try would plant on another step's site.
        if any(re.search(complete_rx(step), line) for line in log_lines(role_log(host))[start:]):
            return True, f"objective do: satchel {step} {actor} complete before try {n}"
        why = step_go(host)
        if not why:
            took = satchel_take(roles)  # best effort: the plant gate below is the only gate
            if took:
                note(f"objective go: satchel pickup skipped: try {n} {took}")
            why = step_go(host) or satchel_plant(roles, command_line("shot", [shot_tag(step, n)], prefix)[0])
        if why:
            note(f"objective go: satchel gate failed: try {n} {why}")
            continue
        send(host, f"warp client {actor} {SATCHEL_CLEAR_UU}")
        ok, detail = send(host, f"objective wait {step} {SATCHEL_WAIT_S}", SATCHEL_WAIT_S + CMD_TIMEOUT_S)
        if ok:
            return True, f"objective do: satchel {step} {actor} complete on try {n}"
        why = f"planted, no Complete row in {SATCHEL_WAIT_S} s ({detail})"
        note(f"objective go: satchel try {n}: {why}")
    return False, f"VOID objective do: satchel {step} {actor}: {ROUTINE_TRIES} tries, last: {why}"


# ---- Board (mech), senior draft 2026-11-19. Step: FED_C2M1 RallyPoint_Marker, actor[0] BP_MechDropPlatform_C_3 (kind
# other today). The level script completes it on the mech boarding: c423b191a host 591807695 card255
# "OnWantsToBoardMech mirrored to" then CompleteSubObjective(LOS_O05, RallyPoint_Marker) 2 ms later. The press target
# is the dropped BP_BaseMech_C, not the platform. Recipe from c423-gates-c2m1.txt lines 778-786 (client boards), plus
# the focus gate it lacked. objective_do override: kind other + "MechDropPlatform" in the class -> routine_board.
# Stands 250, 200 (c504b3: a 150 ring has no static floor near the body and falls back to a 3372 uu body ring). The
# mech is at rest from its spawn row (pod destroyed the same ms). The focus scan aims at the whole-actor bounds,
# which on BP_BaseMech_C sit about 1850 uu SW of the body, so only a stand whose line to that centre crosses the
# body focuses (c504b2 try 2 by luck of side; c504b3 0 of 3). Board stays unproven until the DLL scan also tries
# the origin and root/mesh bounds (lessons 6 and 7 in docs/reference/objective-routines.md).
BOARD_DISTS = (250, 200)
BOARD_RX = r"card255: host t=\d+ OnWantsToBoardMech mirrored to "


def routine_board(roles, step, actor, prefix):
    host, why = roles["host"], "no try"
    if "client" not in roles:
        return False, f"VOID objective do: board {step} {actor}: needs a client (client board path)"
    client = roles["client"]
    ok, detail = wait_log(host, [r"card098: host spawn BP_BaseMech_C "], 30)  # drop lands ~2.6 s after the Add
    if not ok:
        return False, f"VOID objective do: board {step} {actor}: no mech spawn row in 30 s ({detail})"
    send(host, "clear 3000 kill", CLEAR_TIMEOUT_S)
    for n, dist in enumerate(BOARD_DISTS, 1):
        ok, detail = send(host, f"warp client BP_BaseMech_C {dist}")
        if not ok:
            why = f"warp {dist}: {detail}"
            note(f"objective go: board gate failed: try {n} {why}")
            continue
        why = focus_gate(client, "BP_BaseMech_C", command_line("shot", [shot_tag(step, n)], prefix)[0])
        if why:
            note(f"objective go: board gate failed: try {n} {why}")
            continue
        host.mark = len(log_lines(role_log(host)))
        ok, detail = send(client, "key E")
        if not ok:
            why = f"key E: {detail}"
            note(f"objective go: board gate failed: try {n} {why}")
            continue
        ok, detail = wait_log(host, [BOARD_RX, "since-mark"], 15)
        if not ok:
            why = f"pressed, no card255 board row in 15 s ({detail})"
            note(f"objective go: board try {n}: {why}")
            continue
        ok, detail = send(host, f"objective wait {step} {STEP_WAIT_S}", STEP_WAIT_S + CMD_TIMEOUT_S)
        if ok:
            return True, f"objective do: board {step} {actor} complete on try {n}"
        why = f"boarded, no Complete row in {STEP_WAIT_S} s ({detail})"
        note(f"objective go: board try {n}: {why}")
    return False, f"VOID objective do: board {step} {actor}: {ROUTINE_TRIES} tries, last: {why}"
# Mech spawn: c423b191a host 591702290 `card098: host spawn BP_BaseMech_C`, 2.6 s after AddSubObjective(RallyPoint_Marker).


# kind from `objective now`; the DLL's use kind covers pickups too (c126 Use incl. WeaponPickup): a use step whose
# actor[0] class names a Pickup runs the pickup routine
# kind marker: the go stands at the marker or in its nearest presence volume (c504b2: LOS_O01_SO02 and LOS_O02_SO02
# each completed on the plain go), so the presence routine runs it.
ROUTINES = {"nest": routine_nest, "presence": routine_presence, "marker": lambda r, s, a, p: routine_presence(r, s, a, p, "marker"),
            "use": routine_use}


# Card 512 (c504k1: Stadium_SO1_B-D voided 45 times, 519 s, and used 45 of 60 loop rounds): per step id, the VOID
# count and the host log length at the last VOID. At VOID_CAP the routine is not run again (the loop's own
# `objective next` moves on) until a new Add row for the id appears after that last VOID.
VOID_CAP = 2
STEP_VOIDS = {}


def add_rx(step):
    return rf"card049: host t=\d+ Add\w*Objective\((?:\w+, )?{step}\)"


def void_capped(host, step):
    """Why the step is capped, or None (and the count reset when the step was added again since its last VOID)."""
    count, at = STEP_VOIDS.get(step, (0, 0))
    if count < VOID_CAP:
        return None
    if any(re.search(add_rx(step), line) for line in log_lines(role_log(host))[at:]):
        STEP_VOIDS.pop(step)
        note(f"objective do: {step} added again after its last VOID, VOID count reset")
        return None
    return f"{count} VOIDs this run (cap {VOID_CAP}), no Add row since"


# soak1m1b366a: TacSupport_SO3 (kind other, actor[0] BP_EncounterManager_C) had no routine and was capped; SO4-SO7
# and ShootingRange_SO3 need a tac call, a tac pickup, a flare or ten range targets, which their kind does not say.
# A step with a tools/recipes/<step>.txt runs that file's lines (the proven c335-range-load slice, written by
# tools/gen_recipes.py) in place of its kind routine. The first failed line ends the recipe as a VOID.
RECIPE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "recipes")


def routine_recipe(roles, step, path, prefix):
    """Every line of the step's recipe through run_step; VOID on the first line that fails."""
    steps = load(path)
    for n, words in steps:
        ok, detail = run_step(roles, words, prefix)
        if not ok:
            return False, f"VOID objective do: recipe {step} line {n} ({' '.join(words)[:120]}): {detail[:200]}"
    return True, f"objective do: recipe {step} complete ({len(steps)} lines)"


def objective_do(roles, args, prefix):
    """`objective do [<id>]`: the step's routine, with its VOIDs counted per id (card 512 cap)."""
    host = roles["host"]
    seen = {}  # objective_do_once writes the live step id it ran a routine on
    ok, detail = objective_do_once(roles, args, prefix, seen)
    step = seen.get("step")
    if not ok and step and detail.startswith("VOID") and "capped" not in detail:
        count, _ = STEP_VOIDS.get(step, (0, 0))
        STEP_VOIDS[step] = (count + 1, len(log_lines(role_log(host))))
    return ok, detail


def objective_do_once(roles, args, prefix, seen):
    """`objective do [<id>]`: expect, now, the kind's routine (its tries run the go), then the client's replay row."""
    host = roles["host"]
    if args:
        ok, detail = send(host, f"objective expect {args[0]}")
        if not ok:
            return False, f"VOID objective do: expect {args[0]}: {detail}"
        if seq_rows(host, host.sent, re.compile(r"cmd: (\d+) objective expect \S+: already complete")):
            return True, f"objective do: {args[0]} already complete, nothing to do"
    ok, detail = send(host, "objective now")
    seq = host.sent
    cur, act = seq_rows(host, seq, NOW_CUR_RX), seq_rows(host, seq, NOW_ACTOR_RX)
    if not ok or not cur:
        return False, f"VOID objective do: no current step ({detail})"
    doable = seq_rows(host, seq, NOW_DOABLE_RX)
    if cur[0].string.rstrip().endswith("marker 0") and doable and not args:
        # Hidden current step with a visible doable sibling: `objective expect` makes the sibling current, so the
        # routine's `objective go` stands at the sibling's volume, then `objective now` is read again.
        eok, edetail = send(host, f"objective expect {doable[0].group(2)}")
        if not eok:
            return False, f"VOID objective do: expect doable {doable[0].group(2)} over hidden current: {edetail}"
        ok, detail = send(host, "objective now")
        seq = host.sent
        cur, act = seq_rows(host, seq, NOW_CUR_RX), seq_rows(host, seq, NOW_ACTOR_RX)
        if not ok or not cur:
            return False, f"VOID objective do: no current step after expect ({detail})"
    step, kind = cur[0].group(2), cur[0].group(3)
    if args and step != args[0]:  # never run a routine on a step the line did not name
        return False, f"VOID objective do: asked {args[0]}, current step is {step}"
    seen["step"] = step
    capped = void_capped(host, step)
    if capped:
        return False, f"VOID objective do: {step} capped: {capped}"
    STEP_DOS[step] = STEP_DOS.get(step, 0) + 1
    start = len(log_lines(role_log(host)))
    lines = log_lines(role_log(host))  # actor[0] is the row right after the current row
    at = next(i for i, x in enumerate(lines) if NOW_CUR_RX.search(x) and int(NOW_CUR_RX.search(x).group(1)) == seq)
    m = NOW_ACTOR_RX.search(lines[at + 1]) if at + 1 < len(lines) else None
    recipe = os.path.join(RECIPE_DIR, f"{step}.txt")
    if os.path.exists(recipe):
        ok, detail = routine_recipe(roles, step, recipe, prefix)
    elif m is None and kind == "other":
        # c504b3 trace: an actorless kind-other step (C2M1 LOS_O02_SO01, LOS_O03_SO01: "actors 0") completes when a
        # player enters the presence volume the go's actorless stand picks (nearest to the sibling marker actor).
        ok, detail = routine_presence(roles, step, "actorless", prefix)
        detail = detail.replace("objective do: presence ", "objective do: presence-actorless ", 1) if ok else \
            f"VOID objective do: {step} actorless kind other: not a presence step? ({detail})"
    elif m is None:
        return False, f"VOID objective do: {step} kind {kind}: no actor[0] row"
    else:
        routine = ROUTINES.get(kind)
        if kind == "use" and "Pickup" in m.group(2):
            routine = routine_pickup
        # c504b4 trace: C2M1 LOS_O03_SO02a-c are kind other with actor[0] BP_SatchelItemPickup_C (a consumable
        # satchel take, lesson 9), not a site; without the Pickup check they would run the plant routine.
        elif kind == "other" and "SatchelItemPickup" in m.group(3):
            routine = routine_satchel_take
        elif kind == "other" and "Satchel" in m.group(3):
            routine = routine_satchel
        elif kind == "other" and "MechDropPlatform" in m.group(3):
            routine = routine_board
        if routine is None:
            return False, f"VOID objective do: {step} kind {kind} class {m.group(3)}: routine not built"
        ok, detail = routine(roles, step, m.group(2), prefix)
    if ok and any(re.search(LEVEL_END_RX, x) for x in log_lines(role_log(host))[start:]):
        return True, detail + "; level end (WB_TrooperLevelEnd) counts as the step's done, no client replay row"
    if not ok or "client" not in roles:
        return ok, detail
    rx = (rf"(?:card049: client t=\d+ replayed Complete\w*Objective\((?:\w+, )?{step}\)"
          rf"|card097: client t=\d+ subsystem ObjectiveComplete\({step}\))")
    cok, cdetail = wait_log(roles["client"], [rx], 30)
    return True, detail + ("; client replayed" if cok else f"; TRY-FAIL client replay row: {cdetail}")


def run_step(roles, words, prefix):
    """One scenario line; returns (ok, detail). Shot names get `prefix` (the run tag) so reruns keep their shots."""
    if words[0] == "must" and len(words) > 1:  # the run loop ends the run on a failure (card 310 range)
        return run_step(roles, words[1:], prefix)
    if words[0] == "try" and len(words) > 1:
        ok, detail = run_step(roles, words[1:], prefix)
        if not ok and detail.startswith("VOID") and look_gate_void(roles, words[1:], detail):
            return True, NOT_HELD + re.sub(r"\bVOID ", "", detail)  # card 504: not proved, no VOID
        if not ok and detail.startswith("VOID"):  # card 243/265/284: a max-left=, kill or expect gate voids under try
            return False, detail
        return True, detail if ok else f"TRY-FAIL {detail}"
    # card 242: unless/when substitute only in their inner step (the recursive run_step), so a skipped step never
    # reads {gN} (241 run 10: a matched unless still failed on the substitution).
    if words[0] not in ("unless", "when"):
        try:
            words = substitute(words)
        except ValueError as e:
            return False, str(e)
    if words[0] == "unless":
        return run_unless(roles, words, prefix)
    if words[0] == "when":
        return run_when(roles, words, prefix)
    if words[0] == "void":
        return run_void(roles, words)
    if words[0] == "stuck":
        return run_stuck(roles, words)
    if words[0] == "retry":
        return run_retry(roles, words, prefix)
    if words[0] == "both":
        return run_both(roles, words, prefix)
    if words[0] == "&":
        return run_background(roles, words, prefix)
    if words[0] == "join":
        return run_join(roles, words)
    if words[0] == "sleep" and len(words) == 2:
        time.sleep(float(words[1]))
        return True, "slept"
    if len(words) < 2 or words[0] not in roles:
        return False, f"unknown role or step: {' '.join(words)}"
    role, verb, args = roles[words[0]], words[1], words[2:]
    if verb == "objective" and args[:1] == ["do"] and role.name == "host":
        return objective_do(roles, args[1:], prefix)
    if verb in COMMANDS:
        skip = gate_skip([role], verb)
        if skip:
            return True, skip
        raw = args
        args, gates = split_gates(verb, args)
        line, timeout_s = command_line(verb, args, prefix)
        ok, detail = send_gated(role, line, timeout_s, gates)
        note_gate(roles, role, verb, raw, ok, detail)
        return ok, detail
    if verb == "pickup":
        return pickup(role, args)
    if verb == "halt" and not args:
        return halt(role)
    if verb == "mark" and not args:
        role.mark = len(log_lines(role_log(role)))
        return True, f"marked at log line {role.mark}"
    if verb == "stall":
        return stall(role, args, role.bin_dir)
    if verb == "wait" and args and args[0] in WAIT_KINDS:
        rest, timeout_s = split_timeout(args[1:], GOTO_TIMEOUT_S if args[0] == "goto" else WAIT_TIMEOUT_S)
        fn = {"state": wait_state, "log": wait_log, "near": wait_near, "stand": wait_stand, "goto": wait_goto,
              "file": wait_file}[args[0]]
        return fn(role, rest, timeout_s)
    return False, f"unknown step: {' '.join(words)}"


def game_pids(role, bin_dir):
    """The role's game processes in this lane: argv[0] is the exe's Windows path (wine renames comm to GameThread),
    its environment has the role's log tag and its working directory is this lane's game copy (run_pair.sh starts
    the game from there)."""
    game_dir = os.path.realpath(os.path.join(bin_dir, "..", "..", ".."))
    tag = f"UBWCOOP_LOG_TAG={role.name}".encode()
    pids = []
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            with open(f"/proc/{d}/cmdline", "rb") as f:
                if not f.read().split(b"\0")[0].endswith(b"StarshipTroopers_UBW-Win64-Shipping.exe"):
                    continue
            with open(f"/proc/{d}/environ", "rb") as f:
                if tag not in f.read().split(b"\0"):
                    continue
            if os.path.realpath(os.readlink(f"/proc/{d}/cwd")) == game_dir:
                pids.append(int(d))
        except OSError:
            continue
    return pids


def stall(role, args, bin_dir):
    """Card 128: stop the role's game for <seconds>, then let it go on."""
    if len(args) != 1:
        return False, "stall needs <seconds>"
    pids = game_pids(role, bin_dir)
    if len(pids) != 1:
        return False, f"stall: {len(pids)} game processes for {role.name} (want 1): {pids}"
    os.kill(pids[0], signal.SIGSTOP)
    try:
        time.sleep(float(args[0]))
    finally:
        os.kill(pids[0], signal.SIGCONT)
    return True, f"stalled pid {pids[0]} for {args[0]} s"


def wait_up(roles, timeout_s):
    """Both games have opened their command files (their logs say so); writing earlier would be truncated."""
    for r in roles.values():
        if not wait_until(lambda r=r: "cmd: polling" in read_text(role_log(r)), timeout_s):
            return r.name
    return None


def misplaced_warp(words):
    """Why a warp in this step cannot run, or None. `warp client` moves the host's server copy of the client's pawn,
    so only the host has one; sent to the client it always fails with "no server copy of the client's pawn". A
    client's own warp (`client warp actor ...`) is undone by the host's move correction: c323b271a line 57 and
    c502b270a cmd 48, the client's next look was from its old spot, 4687 and 41418 uu out. The step is found after
    any `try`/`&`/`unless <role> log "<re>" [count=N]` prefix."""
    for i in range(len(words) - 2):
        if words[i] not in ("client", "client2", "both") or words[i + 1] != "warp":
            continue
        if words[i + 2].startswith("client"):
            return "warp client moves the host's server copy of the client's pawn: send it to the host"
        return ("a client's own warp is undone by the host's move correction: send `host warp client|client2 ...` "
                "(the server copy, which replicates)")
    return None


# Dead-wait cap (user, 2026-11-14: both players stood still through chained dead waits; b11 run 6: one failed
# objective burned a 20 s wait and three 30 s waits). A group is the steps from a "# ---" header comment or a
# `host objective expect` step (a new objective step) to the next of either. After
# a step of a group fails (not a try step, not `objective expect`), every later wait of that group still runs, with
# its time cut to DEAD_CAP_S: a row already in the log still matches, a wait on what the failed step should have
# caused ends at once. `must` steps keep their time.
GROUP_STARTS = []
STOP_LINES = []  # "# ===" comments: where a STUCK step's skip ends (b215 speed-up)
DEAD_CAP_S = 1


def group_of(n):
    """The line of the last "# ---" header at or before line n (0 before the first)."""
    return max([g for g in GROUP_STARTS if g <= n], default=0)


def cap_wait(words):
    """words with the wait time of a `<role> wait ...` or `<role> objective wait <id> <s>` cut to DEAD_CAP_S, or None."""
    if words[0] == "must" or "wait" not in words:
        return None
    i = words.index("wait")
    if i >= 2 and words[i - 1] == "objective" and words[-1].replace(".", "", 1).isdigit():
        return words[:-1] + [str(DEAD_CAP_S)]
    if i >= 1 and words[i - 1] in LINT_ROLES and i + 1 < len(words) and words[i + 1] in WAIT_KINDS:
        return [w for w in words[:i]] + [w for w in words[i:] if not w.startswith("timeout=")] + [f"timeout={DEAD_CAP_S}"]
    return None


def load(path):
    steps = []
    with open(path, "r", encoding="ascii") as f:
        for n, raw in enumerate(f, 1):
            line = raw.strip()
            if line.startswith("# ---"):
                GROUP_STARTS.append(n)
            if line.startswith("# ==="):
                STOP_LINES.append(n)
            if line and not line.startswith("#"):
                words = shlex.split(line, comments=True)
                why = misplaced_warp(words)
                if why:
                    raise SystemExit(f"{path}:{n}: {why}")
                steps.append((n, words))
    if len(steps) > MAX_STEPS:
        raise SystemExit(f"scenario has {len(steps)} steps, more than {MAX_STEPS}")
    return steps


LINT_ROLES = ("host", "client", "client2")  # client2: card 209, run_pair.sh CLIENT2=1
WAIT_KINDS = ("state", "log", "near", "stand", "goto", "file")


SHOT_NAME = re.compile(r"[A-Za-z0-9_-]{1,40}")  # mod/src/cmdfile.cpp: shot names are 1-40 chars of [A-Za-z0-9_-]
lint_prefix = ""  # b12 item 3: the run tag the drive prepends to shot names, counted by the parse pass


def lint_command(verb, args):
    """Why a command step's words cannot run (card 243 r8 parse pass), or None. Checks the gate words and the
    clear ranges the DLL's cmdfile accepts (radius 100-20000 uu, 10-90 s)."""
    rest, gates = split_gates(verb, args)
    rest, _ = split_timeout(rest, 0.0)
    if verb == "shot" and len(rest) == 1:
        name = f"{lint_prefix}-{rest[0]}" if lint_prefix else rest[0]
        if not SHOT_NAME.fullmatch(name):
            return f"shot name {name} ({len(name)} chars with the run tag) is not 1-40 chars of [A-Za-z0-9_-]"
    if "line" in gates and (verb != "look" or rest[:1] != ["actor"] or "bounds" not in rest or "hit" in gates):
        return "line= takes a look actor <name> ... bounds without hit= (card 310 range)"
    if "walk" in gates and (verb != "look" or "hit" not in gates or not gates["walk"].isdigit()
                            or not 1 <= int(gates["walk"]) <= len(WALK_MOVES)):
        return f"walk= takes 1-{len(WALK_MOVES)} on a look with hit= (card 316)"
    for k in ("within", "max-left", "calm"):
        if k in gates and not re.fullmatch(r"\d+(\.\d+)?", gates[k]):
            return f"{k}={gates[k]} is not a number"
    if verb == "clear" and rest[1:] == ["kill"]:
        if "calm" in gates:
            return "calm= has no use on a clear kill (no fire, no clear row stamp)"
        if not 100 <= float(rest[0]) <= 20000:
            return f"clear kill takes <radius 100-20000>, got {rest}"
    elif verb == "clear" and (len(rest) != 2 or not 100 <= float(rest[0]) <= 20000 or not 10 <= float(rest[1]) <= 90):
        return f"clear takes <radius 100-20000> <seconds 10-90>, got {rest}"
    return None


def lint_step(words):
    """Why one scenario line cannot be parsed by run_step, or None; posts and reads nothing (card 243 r8)."""
    for _ in range(8):  # prefixes nest a few deep at most (try unless ... when ...)
        if not words:
            return "empty step"
        if words[0] in ("try", "must") and len(words) > 1:
            words = words[1:]
        elif words[0] == "unless":
            if len(words) < 6 or words[1] not in LINT_ROLES or words[2] != "log" or not words[4].startswith("count="):
                return "unless takes <role> log \"<regex>\" count=N <step>"
            re.compile(words[3])
            int(words[4].split("=", 1)[1])
            words = words[5:]
        elif words[0] == "when":
            if len(words) < 6 or words[1] not in LINT_ROLES or words[2] != "last":
                return "when takes <role> last \"<regex>\" \"<test>\" <step>"
            re.compile(words[3])
            re.compile(words[4])
            words = words[5:]
        elif words[0] == "retry":
            got = retry_parts(words)
            if isinstance(got, str):
                return got
            return next((f"in retry: {why}" for why in map(lint_step, got[1]) if why), None)
        else:
            break
    if words == ["abort-on", "objective"]:
        return None
    if words[0] == "stuck":
        if len(words) != 5 or words[1] not in LINT_ROLES or words[2] != "log" or not words[4].startswith("count="):
            return "stuck takes <role> log \"<regex>\" count=N"
        re.compile(GROUP_RE.sub("x", words[3]))
        int(words[4].split("=", 1)[1])
        return None
    if words[0] in ("void", "join", "failsoft", "voidsoft") or (words[0] == "sleep" and len(words) == 2):
        return None if words[0] != "void" or len(words) in (5, 9) else "void takes 5 or 9 words"
    if words[0] in ("both", "&"):
        words = words[1:] if words[0] == "both" else words[2:]
        return lint_command(words[0], words[1:]) if words and words[0] in COMMANDS else "both/& need a command"
    if len(words) < 2 or words[0] not in LINT_ROLES:
        return "unknown role or step"
    verb, args = words[1], words[2:]
    if verb in COMMANDS:
        return lint_command(verb, args)
    if verb == "pickup":
        return None if len(args) == 2 and float(args[1]) > 0 else "pickup takes <class> <max uu>"
    if verb == "halt":
        return None if not args else "halt takes no arguments"
    if verb == "wait" and args and args[0] in WAIT_KINDS:
        # c551b376a: an unquoted regex split into words, each compiled, and parse said 0 bad; wait_log needs one.
        rxs = [a for a in args[1:] if not a.startswith(("count=", "timeout=")) and a not in ("new", "since-mark")]
        if args[0] == "log" and len(rxs) != 1:
            return "wait log takes one quoted regex, count >= 1 and an optional new or since-mark"
        if args[0] == "file" and len(rxs) != 1:
            return "wait file takes one path"
        for a in rxs if args[0] == "log" else ():
            re.compile(a)
        return None
    return None if verb in ("mark", "stall") else f"unknown verb {verb}"


# c209b256a: a fire clear ran 45 s while the downed host's 30 s bleedout lapsed. Objective and state tests clear by
# code (`clear <radius> kill`); a scenario that tests weapon fire says so with a `# fire test` comment line.
FIRE_TEST_MARK = re.compile(r"^#\s*fire test\b", re.M | re.I)


def fire_clear(words):
    """True when a step (any prefix, any retry part) holds a `clear <radius>` without `kill`."""
    return any(w == "clear" and i + 1 < len(words) and words[i + 1][:1].isdigit() and
               (i + 2 >= len(words) or words[i + 2] != "kill") for i, w in enumerate(words))


def lint(path, prefix):
    """Parse-only replay of a scenario: every line through lint_step; returns the number of bad lines. Shot names
    are checked with `prefix` (the run tag; run_pair.sh passes TAG) prepended."""
    global lint_prefix
    lint_prefix = prefix
    bad = 0
    with open(path, encoding="utf-8") as f:
        fire_test = FIRE_TEST_MARK.search(f.read()) is not None
    for n, words in load(path):
        try:
            why = lint_step(words)
        except (re.error, ValueError) as e:
            why = f"{type(e).__name__}: {e}"
        if not why and not fire_test and fire_clear(words):
            why = "clear by weapon fire in a scenario not marked `# fire test` (user rule 2026-11-13: clear <radius> kill)"
        if why:
            bad += 1
            print(f"{path}:{n}: {why}: {' '.join(words)[:200]}")
    print(f"{path}: {len(load(path))} steps, {bad} bad")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("scenario")
    ap.add_argument("--bin", default=DEFAULT_BIN)
    ap.add_argument("--up-timeout", type=float, default=120.0)
    ap.add_argument("--roles", default="host,client")
    ap.add_argument("--shot-prefix", default="", help="prepended to shot names: <role>-<prefix>-<name>.png")
    ap.add_argument("--parse", action="store_true", help="parse-only replay of the scenario; posts nothing")
    a = ap.parse_args()
    if a.parse:  # b12 item 3: without --shot-prefix, the scenario's file stem stands for the run tag
        return lint(a.scenario, a.shot_prefix or os.path.splitext(os.path.basename(a.scenario))[0])
    steps = load(a.scenario)
    roles = {n: Role(a.bin, n) for n in a.roles.split(",")}
    t0 = time.monotonic()

    def say(text):
        print(f"{time.strftime('%H:%M:%S')} +{time.monotonic() - t0:6.1f}s {text}", flush=True)

    missing = wait_up(roles, a.up_timeout)
    if missing:
        say(f"FAIL {missing} never logged 'cmd: polling' in {a.up_timeout:.0f} s")
        return 1
    say("both games polling")
    # Dead time (user, after hb56): seconds in sleep steps and in try waits that timed out, printed at the end so a
    # stale wait pattern or a long sleep shows in every run's .drive log.
    slept = 0.0
    timed_out = []
    failsoft = False
    voidsoft = False
    abort_objective = False
    failed = []
    voided = []
    skipped = []
    not_proved = []  # card 504: (line, step, gate detail)
    capped = []
    dead_group = (0, False)
    skip_to = 0  # b215: a STUCK step skips the steps before this line
    stuck_skipped = 0
    for n, words in steps:
        if n < skip_to:
            stuck_skipped += 1
            continue
        if words == ["failsoft"]:
            failsoft = True
            say(f"ok   line {n}: failsoft -> on")
            continue
        if words == ["voidsoft"]:
            voidsoft = True
            say(f"ok   line {n}: voidsoft -> on")
            continue
        if words == ["abort-on", "objective"]:
            abort_objective = True
            say(f"ok   line {n}: abort-on objective -> on")
            continue
        if words[1:3] == ["objective", "expect"] or group_of(n) != dead_group[0]:
            dead_group = (group_of(n), False)  # a new header or a new objective step: a live group
        capped_words = cap_wait(words) if dead_group[1] else None
        if capped_words:
            capped.append(n)
            words = capped_words
        t_step = time.monotonic()
        ok, detail = run_step(roles, words, a.shot_prefix)
        if not ok and "expect" not in words and not dead_group[1]:
            dead_group = (group_of(n), True)
            say(f"scenario: group of line {group_of(n)} failed at line {n}: its later waits run {DEAD_CAP_S} s")
        took = time.monotonic() - t_step
        if words[0] == "sleep":
            slept += took
        elif detail.startswith("TRY-FAIL timeout"):
            timed_out.append((took, n))
        say(f"{'ok  ' if ok else 'FAIL'} line {n}: {' '.join(words)} -> {detail}")
        if ok and detail.startswith("STUCK"):
            skip_to = next((s for s in STOP_LINES if s > n), 0)  # no stop comment after it: nothing is skipped
            say(f"scenario: line {n} STUCK: " + (f"steps skipped up to line {skip_to}" if skip_to else
                                                 "no \"# ===\" stop comment after it, nothing skipped"))
        if "SKIPPED " in detail:
            skipped.append(n)
        if ok and (detail.startswith(NOT_HELD) or "SKIPPED " in detail):
            not_proved.append(n)
            say(f"not proved: line {n}: step {' '.join(words)}: gate {detail}")
        gone = [r.name for r in roles.values() if not ok and (failsoft or voidsoft) and not game_pids(r, r.bin_dir)]
        void = not ok and detail.startswith("VOID")
        if not ok and words[0] == "must":
            say(f"scenario: must step FAILED line {n} ({' '.join(words)}): run ends")
            failsoft = voidsoft = False  # card 310 range: one result, no chained voids
        if not ok and abort_objective and words[1:3] == ["objective", "wait"]:
            say(f"scenario: objective wait FAILED line {n} ({' '.join(words)}): run ends (abort-on objective)")
            failsoft = voidsoft = False  # card 316: the steps after a failed objective prove nothing
        if not ok and failsoft and not void and not gone:
            kind = "wait" if "wait" in words else "step"
            say(f"scenario: {kind} FAILED line {n} ({' '.join(words)}), continuing")
            failed.append(n)
            continue
        if void and voidsoft and not gone and not TERMINAL_VOID.match(detail):
            for r in roles.values():
                send(r, f"shot {a.shot_prefix + '-' if a.shot_prefix else ''}fail-line{n}", 5.0)
            say(f"scenario: VOID line {n} ({' '.join(words)}), continuing (voidsoft)")
            voided.append(n)
            continue
        if not ok:
            if gone:
                say(f"scenario: line {n} failed with the game gone ({', '.join(gone)}): run ends")
            for r in roles.values():
                send(r, f"shot {a.shot_prefix + '-' if a.shot_prefix else ''}fail-line{n}", 5.0)
            return 1
    lost = sum(t for t, _ in timed_out)
    worst = ", ".join(f"line {n} {t:.0f} s" for t, n in sorted(timed_out, reverse=True)[:8])
    say(f"dead time: sleeps {slept:.1f} s, timed-out try waits {lost:.1f} s in {len(timed_out)} ({worst or 'none'})")
    say(f"stuck skips: {stuck_skipped} step(s) skipped after a STUCK step")
    say(f"dead-wait cap: {len(capped)} wait(s) cut to {DEAD_CAP_S} s after a failed step in their group")
    say(f"gate skips: {len(skipped)} action step(s) after a voided look gate"
        + (f": {', '.join(str(n) for n in skipped)}" if skipped else ""))
    say(f"not proved: {len(not_proved)}" + (f": lines {', '.join(str(n) for n in not_proved)}" if not_proved else ""))
    if voided:
        say(f"scenario finished VOID with {len(voided)} void line(s): {', '.join(str(n) for n in voided)}"
            + (f"; {len(failed)} failed line(s): {', '.join(str(n) for n in failed)}" if failed else ""))
        return 3
    if failed:
        say(f"scenario finished with {len(failed)} failed line(s): {', '.join(str(n) for n in failed)}")
        return 2
    say("scenario passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
