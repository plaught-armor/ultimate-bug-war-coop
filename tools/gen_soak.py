#!/usr/bin/env python3
"""Pre-playtest soak, any Trooper map: the gen_c244 routine loop with the map as an argument (senior, 2026-11-20).

Writes the full objective soak for one FED map (routine form, voidsoft). Same loop, digest rounds, death round and
level-end tail as tools/gen_c244.py (the FED_C4M1 original). The loop ends on the map's last objective Complete row
when END_IDS knows it, and always on the host WB_TrooperLevelEnd row (card 513: a level can end with no O1 Complete
row). The tail follows the level end to the frontend and on to the next map in NEXT_MAP.

Card 504 (senior order 2026-11-19): no per-step lines. Each loop round asks the live current step (`objective now`)
and runs `host objective do` with no id, which reads that step's kind and actor[0] and runs the proven routine for it
(docs/reference/objective-routines.md). A step with no routine VOIDs with "routine not built"; under voidsoft that is
logged and the run goes on (soak-all-objectives: every step driven, each gap a finding). The loop ends when the map
reports Metro_O1 complete; then the gen's evac and level-end waits run.
Usage: python3 -I tools/gen_soak.py <FED map, e.g. C1M1> <dll hash16> [--death] > tools/scenarios/soak-c1m1.txt
"""
import sys

ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
if len(ARGS) != 2:
    sys.exit('usage: gen_soak.py <FED map, e.g. C1M1> <dll hash16> [--death]')
MAP = 'FED_' + ARGS[0].upper()
DLL = ARGS[1]
TAG = 'soak-' + ARGS[0].lower()
# Campaign order (Browse rows of c241-c2m1-b1: C2M1 -> C3M1). None = the last map, no next-map tail.
NEXT_MAP = {'FED_C1M1': 'FED_C2M1', 'FED_C2M1': 'FED_C3M1', 'FED_C3M1': 'FED_C3M2', 'FED_C3M2': 'FED_C4M1',
            'FED_C4M1': 'FED_C4M2', 'FED_C4M2': 'FED_C5M1', 'FED_C5M1': 'FED_C5M2', 'FED_C5M2': None}
# The last objective Complete row before the level end, from runs that reached it (c241-c2m1-b1 C2M1, c244b237a
# C4M1). Maps not listed end on the level-end row only. C1M1 is not listed: soak1m1b365a ended its loop on Assault_O1
# with TacSupport_01 and ShootingRange_O1 still open and no level-end widget; c335-range-load drives both after
# Assault, so the level goes on past Assault_O1.
END_IDS = {'FED_C2M1': 'LOS_O05', 'FED_C4M1': 'Metro_O1'}
if MAP not in NEXT_MAP:
    sys.exit('unknown map %s' % MAP)
# Rounds of the loop. C4M1 has 22 doable steps in the c244b237a doable lists; the rest is room for a step that
# VOIDs once and comes back current after `objective next` wraps (each round with no step open waits 15 s).
ROUNDS = 60
WAVE_LEFT = 'max-left=99'  # a clear inside a wave hold runs while the level script still spawns waves (c245 b196)
# The map's last objective: once its Complete row is in the host log, every loop line is skipped.
END_ID = END_IDS.get(MAP)
# Card 513 (c504k2, lesson 16): Metro_SO2's presence opened the level-end widget with no Metro_O1 Complete row; the
# widget row ends the loop too.
LEVEL_END = r'card018: host SetInputMode_UIOnlyEx called by BP function ExecuteUbergraph_WB_TrooperLevelEnd'
LEVEL_END_C = r'card018: client SetInputMode_UIOnlyEx called by BP function ExecuteUbergraph_WB_TrooperLevelEnd'
END_RX = LEVEL_END if END_ID is None else (
    r'(card049: host t=\d+ CompleteObjective\(%s\)|card097: host t=\d+ native CompleteObjective (\w+/)?%s |%s)'
    % (END_ID, END_ID, LEVEL_END))
UNTIL_END = 'unless host log "%s" count=1 ' % END_RX

# The summary row of `objective now` (or its failure row) is printed by every call, `objective do`'s own too.
# "current none" and "-> failed" mean no step is open (the next one is added later).
SUM_RX = r'cmd: \d+ objective now(: \d+ row\(s\) under \S+, current \w+| -> failed)'
HAS = r'"current (?!none;)\w+;"'
NONE = '"current none;|-> failed"'
# Lesson 4: the last of (an `objective now` row, a Complete row). "objective now" last means nothing completed since
# the do's own now row, so the step the do ran is still open (VOID or failed routine): move on with `objective next`
# (sparse order, wrap; none doable keeps the step). A Complete row last means the do (or a late fuse) closed a step.
AFTER_DO = (r'(cmd: \d+ objective now: \d+ row\(s\) under \S+, current \w+|card049: host t=\d+ Complete\w*Objective'
            r'|card097: host t=\d+ native CompleteObjective)')

out = []


def when(test, step, rx=SUM_RX):
    out.append(UNTIL_END + 'when host last "%s" %s %s' % (rx, test, step))


def round_(n):
    out.extend(['# === round %02d' % n, '# --- round %02d: the live current step' % n,
                UNTIL_END + 'try host objective now'])
    when(HAS, 'host objective do')
    when(HAS, 'when host last "%s" "objective now" try host objective next' % AFTER_DO)
    # c244b214a: with no current step (no open step with its marker on) the next step comes on a wave end; clear,
    # then wait for the next Add or Complete.
    when(NONE, 'host clear 4000 kill ' + WAVE_LEFT)
    when(NONE, r'try host wait log "card049: host t=\d+ (Add|Complete)" new timeout=15')


def digest(tag):
    out.extend(['# === digest round: %s' % tag, '# --- digest round: %s' % tag, 'host mark', 'client mark', 'host objectives',
                r'host wait log "card132: host t=\d+ digest round \d+ \((command|timer)\) sent: " since-mark timeout=15',
                r'client wait log "card132: client round \d+: actors \d+ host, \d+ client, mismatches " since-mark timeout=30'])


HEAD = r'''# Pre-playtest soak: %(map)s full objective soak, routine form (voidsoft). Generated by tools/gen_soak.py; edit that.
# Rules: docs/STATE.md "Gameplay test scenario rules", docs/reference/objective-routines.md (lessons 1-18).
# Every round reads the live current step and runs `host objective do` (no id): the kind comes from the live row.
# "routine not built" VOIDs are findings, logged under voidsoft; the run goes on.
# Run: systemd-run --user --scope -q -p MemoryMax=12G taskset -c 4-7,12-15 nice -n 10 env LANE=2
#      SCENARIO=tools/scenarios/%(tag)s.txt FAST=1
#      HOST_EXTRA_ENV="UBWCOOP_DIAG=1 UBWCOOP_TRAVEL=/Game/Maps/FED/%(map)s?listen UBWCOOP_GOD=1"
#      CLIENT_EXTRA_ENV="UBWCOOP_DIAG=1" tools/run_pair.sh %(tag)s<build> 3600
voidsoft
void unless host dll %(dll)s
void unless client dll %(dll)s
host wait state map=%(map)s timeout=240 void
client wait state map=%(map)s pawn=client-own timeout=180
try client wait log "card120: client t=\d+ minimap origin \S+ \(BP_PlayerCharacterMI_C\) set" timeout=60
try host ui continue
try client wait log "card135: client t=\d+ screen prompt ready" timeout=30
client mark
try client ui continue
try client wait log "card067: dump \d+ in %(map)s \(WB_Stack_C RequestGameUnpause\)" since-mark timeout=30
try host wait log "card320: host t=\d+ hold off after \d+ ms \(all confirmed\)" timeout=60
failsoft
host wait state pawn=host-own timeout=90
client shot %(tag)s-hud-start
host clear 4000 kill'''


FRONT = r'card052: t=\d+ world 3D_Frontend \(package /Game/Maps/3D_Frontend/3D_Frontend, net mode 2\)'
NEXT = NEXT_MAP[MAP]
BROWSE = r'card045: host t=\d+ Browse URL: .* map ./Game/Maps/FED/%s.' % NEXT
NEXT_WORLD = r'card052: t=\d+ world %s \(package /Game/Maps/FED/%s' % (NEXT, NEXT)


def tail():
    """Card 513: level end -> Next until the frontend -> continue until Browse NEXT -> both in NEXT (the
    gen_c243 END path). Every line runs only after the host level-end row, so a run that never ends the level does
    not idle here (c504k2 idled 150 s on the old FED_C4M2 waits)."""
    gate = 'when host last "%s" "." ' % LEVEL_END
    out.append('# --- level end: Next to the frontend, continue to %s (card 513, gen_c243 END path)' % NEXT)
    out.append(gate + 'try client wait log "%s" timeout=30' % LEVEL_END_C)
    for _ in range(8):
        out.append(gate + 'unless host log "%s" count=1 host ui click BndEvt__TrooperLevelEnd_WB_Button_Next' % FRONT)
        out.append(gate + 'try host wait log "%s" timeout=3' % FRONT)
    out.append(gate + 'host wait log "%s" timeout=120' % FRONT)
    # c513a: the client follows the host to the frontend (persistent lobby, card 345); no reconnect row is written.
    out.append(gate + r'client wait log "card052: t=\d+ world 3D_Frontend \(package /Game/Maps/3D_Frontend/3D_Frontend, net mode 3\)" timeout=60')
    for s in ('ui continue', 'key Enter', 'winkey Enter', 'winkey Enter', 'winkey Enter'):
        out.append(gate + 'unless host log "%s" count=1 try host %s' % (BROWSE, s))
        out.append(gate + 'try host wait log "%s" timeout=3' % BROWSE)
    out.append(gate + 'host wait log "%s" timeout=10' % BROWSE)
    out.append(gate + 'host wait log "%s" timeout=300' % NEXT_WORLD)
    out.append(gate + 'client wait log "%s" timeout=300' % NEXT_WORLD)


# Pre-playtest soak (senior order 2026-11-20, b364): one all-down death and RESTART CHECKPOINT respawn mid-map, the
# card 371 path (death.cpp: a lone Downed or Out player waits for a revive, so a respawn needs the all-down). God does
# not block `host down` (c210-r2). The loop reads the live step after the reload, so restored progress is driven again.
# Round 2, not 9: in soak1m1b365a the map's END row landed at about round 6 and UNTIL_END skipped the whole death
# block, so short maps had no respawn proof.
DEATH_AFTER_ROUND = 2
DEATH = [
    '# --- death and respawn: client down, host down (all down), RESTART CHECKPOINT (card 371 path)',
    'client mark',
    'host mark',
    'host down client',
    r'client wait log "card043: client t=\d+ own pawn guid 0x[0-9a-f]+ \(host slot \d\) Up -> Downed" since-mark timeout=10',
    'host down host',
    r'host wait log "card043: host t=\d+ BP_PlayerCharacterMI_C \(host-own, slot 0\) -> Dead \(all down: nobody Up\)" since-mark timeout=10',
    r'host wait log "card044: host death screen: 5 widgets; .*LoadLastSave click function" since-mark timeout=20',
    r'client wait log "card045: client death screen: 3 buttons disabled" since-mark timeout=20',
    'host shot %s-death-host' % TAG,
    'client shot %s-death-client' % TAG,
    'host mark',
    'client mark',
    'host ui click BndEvt__WB_PlayerDeathScreen_LoadLastSave',
    r'host wait log "card045: host t=\d+ respawn in place \(new pawn" since-mark timeout=30',
    r'client wait log "card045: client t=\d+ reconnected" since-mark timeout=60',
    r'client wait log "card294: client t=\d+ restart done" since-mark timeout=60',
    'host wait state pawn=host-own timeout=60',
    'client wait state pawn=client-own timeout=60',
    'host shot %s-respawn-host' % TAG,
    'client shot %s-respawn-client' % TAG,
]


def main():
    death = '--death' in sys.argv
    out.append(HEAD % {'dll': DLL, 'map': MAP, 'tag': TAG})
    digest('map start')
    for n in range(ROUNDS):
        round_(n)
        if n % 10 == 9:
            digest('after round %02d' % n)
        if death and n == DEATH_AFTER_ROUND:
            out.extend(UNTIL_END + line if not line.startswith('#') else line for line in DEATH)
            digest('after respawn')
    digest('map end')
    out.extend(['host objectives', 'client objectives', 'host shot %s-end' % TAG, 'client shot %s-end' % TAG])
    if NEXT is not None:
        tail()
    out.extend(['client quit', 'host quit'])
    print('\n'.join(out))


main()
