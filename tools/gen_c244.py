#!/usr/bin/env python3
"""Card 244: writes tools/scenarios/c244-c4m1.txt, the FED_C4M1 full objective soak (discovery form, voidsoft).

Objective-driven (card 284): no id or coordinate table. Each block reads the live current step (`objective now`
summary row), makes it current (`objective expect`), moves both players there (`objective go all`), runs the action
its kind needs (use: client E at the card 284 use stand's class; nest: client grenade at the nearest bug nest), then
waits for the level script to complete it, killing the waves by code in between. The step-1 table in
docs/cards/244-full-objective-soak-c4m1.md lists the triggers this should reach; the discovery run says which do.
Usage: python3 -I tools/gen_c244.py <dll hash16> > tools/scenarios/c244-c4m1.txt
"""
import os
import sys

DLL = sys.argv[1] if len(sys.argv) > 1 else "0000000000000000"
BLOCKS = 36  # b241: 36 (the once-only second satchel hold adds a step per block); b235: 37 (the nest kill wait after each G adds 4 steps per block); b223: 39 still fits (the satchel E retries add 4 steps per block: 2490 of the 2600 cap); b215: 39 (the satchel warps and the none-branch clear add 2 steps per block); b214: 41 (the use look front/behind fallback adds 4 steps per block); b212: 44 (the marker 5 s wait adds a step per block); 45 was under the 2500 step cap with the b209 satchel site warps, the b210 pickup warp and card 485 shot steps; 22 ids plus their count rows (Park_SO1_A..C, Park_SO2_A..E, Stadium_SO1_A..D) and markers
HOLD_ROUNDS = 4
WAVE_LEFT = 'max-left=99'  # a clear inside a wave hold runs while the level script still spawns waves (c245 b196)
NEST = 'BP_DestructibleSpawner_Bugnest_HumanGameplay_C'  # the class `objective now` actor[0] rows print

# The summary row of `objective now` (or its failure row) is printed last by every call; its "current <id>" is the
# block's step. "current none" and "-> failed" mean no step is open (the next one is added later).
SUM_RX = r'cmd: \d+ objective now(: \d+ row\(s\) under \S+, current \w+| -> failed)'
SUM_CAPTURE = r'cmd: \d+ objective now: \d+ row\(s\) under \S+, current (\w+);'
# b241: the current step's stand row (printed before the summary row), x y z as {g1} {g2} {g3}
# b242: the satchel site's focus name prefix (the look gate and the miss retry).
# C4M1's site focus is its child actor SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_<n>.
SITE_FOCUS = 'SatchelSiteCAC_'
STAND_CAPTURE = r'cmd: \d+ objective now:   stand \((-?\d+), (-?\d+), (-?\d+)\) from actor '
HAS = r'"current (?!none;)\w+;"'
NONE = '"current none;|-> failed"'
# `objective go` prints the host's row with the kind it moved for, or fails.
GO_RX = r'cmd: \d+ objective go(: host kind \w+| all -> failed)'

# User rule 2026-11-18: an objective that passed with no issues is skipped, not played again. Skip ledger:
# tools/scenarios/c244-passed.txt, one id per line (# comments), each completed on the host and replayed on the client
# in the named run with 0 failed block waits. A block whose current step is in it completes the step by command
# (`objective complete`, card 095) and runs nothing else. Empty ledger or SKIP=0 in the env: every step is played.
# The full soak (SKIP=0) runs before each playtest build and when a card touches that objective's path.
def read_passed():
    import os
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scenarios', 'c244-passed.txt')
    if os.environ.get('SKIP') == '0' or not os.path.exists(path):
        return []
    ids = [l.split('#')[0].strip() for l in open(path)]
    return [i for i in ids if i]


PASSED = read_passed()
if PASSED:
    SKIP_ALT = '(?:%s)' % '|'.join(PASSED)
    SKIP_HAS = '"current %s;"' % SKIP_ALT
    HAS = r'"current (?!none;)(?!%s;)\w+;"' % SKIP_ALT

out = []

# Card 492 fix 6 proof (b248, senior): C492=1 in the env writes tools/scenarios/c492-c4m1.txt, the soak blocks up to
# the stadium (c244b237a: the 23rd block completed Stadium_SO1) with the stadium steps below, then quits. The full
# soak has no room for them (2565 of the 2600 step cap). At the last open Stadium_SO1_<x> panel (the 4th, whose
# CompleteSubObjective adds Stadium_SO2 and closes Medium C_2, C_1 and Gigantic C_1) the host stands outside the
# stadium, south of the Medium doors (y -29720; the panels and BP_PlayerPresence_C_5 are north of it), while the
# client presses E inside. Predictions: one "card492: host ... CompleteSub Stadium_SO1 trigger from
# BP_PlayerPresence_C_5: 1 of 2 inside, card 288 trigger set" row; "card288: ... close held: 1 of 2 players inside"
# rows for the three doors; no "closing: all" row in the 5 s the host stays out; after the host walks in, "closing:
# all 2 players inside" per door, and host and client door states equal in the digest.
C492 = os.environ.get('C492') == '1'
C492_BLOCKS = 28  # 23 blocks reached the stadium end in c244b237a; 5 spare
# The summary row of the last open panel: no other Stadium_SO1_<x> in the doable list.
C492_LAST = r'"current Stadium_SO1_(\w);(?!.* Stadium_SO1_(?!\1\b)\w)"'
# The host's own warp row lands south of the doors (y under -30000) or not.
C492_DOOR = 'BP_Door_Sliding_Medium_C_1'  # (-2600, -29720, -1550), c244b237a card161 tracked row


def c492_outside():
    """Before the client's E at the last stadium panel: the host warps 600 uu off the Medium C_1 door, on the
    south side, and clears the bugs there. c492b248a: `front` lands at y -29120, inside the door line (y -29720),
    so `behind` (y about -30320) is the outside; the warp row must show y under -30000 or the proof is void."""
    out.append('# --- card 492 fix 6: the host outside the stadium for the last panel')
    when(C492_LAST, 'host mark')
    when(C492_LAST, 'host warp actor %s 600 behind' % C492_DOOR)
    when(C492_LAST, r'try host wait log "cmd: \d+ warp: .* -> \(-?[\d.]+, -3\d{4}\." since-mark timeout=5')
    when(C492_LAST, 'host shot c492-outside')
    when(C492_LAST, 'host clear 1500 kill')


def c492_walk_in():
    """After the E: the trigger row, 5 s held with the host outside, then the host walks in and the doors close."""
    out.append('# --- card 492 fix 6: held 5 s, then the host walks in')
    when(C492_LAST, r'try host wait log "card492: host t=\d+ objective \w+ Stadium_SO1 trigger from " since-mark '
         'timeout=10')
    when(C492_LAST, r'try host wait log "card288: host t=\d+ door \S+ closing: all" since-mark timeout=5')
    when(C492_LAST, 'try host goto actor BP_PlayerPresence_C_5 1500 sprint timeout=40')
    # c492b248a: the goto found no path; the warp puts the host inside if the walk did not.
    when(C492_LAST, 'try host warp actor BP_PlayerPresence_C_5 300')
    when(C492_LAST, r'try host wait log "card288: host t=\d+ door \S+ closing: all \d+ players inside" since-mark '
         'timeout=20')
    when(C492_LAST, 'host shot c492-inside')
    when(C492_LAST, 'client shot c492-inside')


def when(test, step, rx=SUM_RX):
    out.append('when host last "%s" %s %s' % (rx, test, step))


def kind(k, step):
    when('"host kind %s "' % k, step, GO_RX)


def block(n):
    tag = 'c244-%02d' % n
    out.extend(['# === block %02d' % n, '# --- block %02d: the live current step' % n, 'host mark',
                'try host objective now', 'try host wait log "%s" since-mark timeout=5' % SUM_CAPTURE])
    if PASSED:  # skip ledger: complete a passed step by command, then wait for the client replay
        when(SKIP_HAS, 'try host objective complete {g1}')
        when(SKIP_HAS, r'try client wait log "card049: client t=\d+ replayed Complete\w*Objective\((\w+, )?{g1}\)" '
                       'timeout=5')
    # b215 speed-up (senior): c244b213a retried Stadium_SO1_B about 50 times and Bridge_Marker 35, each block ending in
    # the same failed wait. After 2 failed block waits (15 s, marker 5 s) for the step, its blocks skip to the next
    # "# ===" stop (drive.py stuck) and the drive log names it stuck.
    # c244b215a with count=2: Bridge_Marker went stuck at 446 s and every later block skipped (the step stays current,
    # so no next one comes). b214 steps that failed then completed: Bridge_Marker after 4 failed block waits,
    # Stadium_SO1_A after 6 (c244b214a) and 13 (c244b214b). count=10 keeps those, caps the 35-50 retry loops.
    when(HAS, r'stuck host log "WARN cmd: \d+ objective wait {g1} (15|5) -> failed" count=10')
    when(HAS, 'try host objective expect {g1}')
    when(HAS, 'host clear 1500 kill')
    when(HAS, 'try host objective go all')
    when(HAS, 'client shot %s-go' % tag)
    when(HAS, 'host shot %s-go' % tag)
    # use (Switch, PanelSwitch, WeaponPickup): the card 284 use stand row names the class; the client faces the
    # nearest one and presses E.
    kind('use', r'try host wait log "c284: use stand (\w+)[: ]" since-mark timeout=5')
    # b214 (card 244 Stadium_SO1_B): c244b213a's look at BP_PanelSwitch_WithoutScreen_C hit Actor_39 at 2761 uu, focus
    # none, and the plain look voided every block. The satchel site fallback: a look with focus none warps the client
    # 200 uu in front of the use target and looks again, then 200 uu behind it (its facing is not known statically).
    # b223 (senior order): c244b222b voided 30 of these looks (Stadium_SO1_A, PanelSwitch C_0 bounds centre 4031
    # -15990, extent 134): from the stand, the 200 front warp (eye 4142 -15881) and the 200 behind one (eye 3887 -16131)
    # the scan hit 1 of 26 points, all on the y -15882 face beside the eye, and the aim passed it to Actor_2/Actor_39 at
    # 2700-3000 uu. Each gated look voided, so the run was VOID even when a later one could have hit. Now one retry
    # step: an ungated look (no void), and while focus is none a 350 uu front warp + ungated look, then a 350 uu behind
    # warp; the one gated look comes last, and a void starts the list again from where the client stands (1 retry).
    # b224 (senior): c244b223a looped on Stadium_SO1_D (PanelSwitch, bounds centre -10794 -15933 -1174) from line 1252
    # to 1653. The 350 behind warp put the client at z -2087, 913 uu below the switch on the floor under it (aim on
    # Actor_2, the slab); the 350 front warp put it on the right floor at (-11009, -15717, -1093), but the host pawn on
    # the stand (-10944, -15872) was in the line and the aim hit BP_PlayerCharacterMI_C at 103 uu. The list ended on
    # behind, so the gated look always voided. Now behind comes first; if it still has focus none, the host moves 350
    # uu behind the switch (out of the front line) and the client warps 350 front; the gated look runs from there.
    # A behind look that finds the switch skips both front steps.
    # b226 (senior): c244b225a still voided SO1_D. From the stand, the bounds look hit C_3 at 124 uu with a clear line
    # but pitch -26.6 (aim at the bounds centre z -1174) and focus none. c244b220a focused the same switch from the
    # same stand with the origin look (cmd 121: hit at z -1104, pitch -3.7, 168 uu) and completed Stadium_SO1_A..D. The
    # origin look now comes first at each spot, and the bounds look is its fallback (Stadium_SO1_A in c244b213a). Every
    # step after the first runs only while focus is none, so a later look never re-aims off a switch already focused;
    # the last one is the gated look that voids when no spot found it.
    # Correction (senior, b229): cmd 121 in c244b220a was the gated look, and its DLL focus sweep found pitch -3.7; the
    # origin look did not focus C_3. The origin-first order stays, but the stand fix is the sweep below.
    # b229: c244b228a looped SO1_D from line 1188 to the end: the only DLL focus sweep (the gated look) ran after the
    # 350 front warp, eye -11012 -15715, 340 uu from the switch and past the 250 interactor range; the stand bounds look
    # (126 uu) has no sweep, so its pitch -26.2 aim stayed focus none. The stand bounds look now sweeps (`sweep`).
    nf = r'when client last "cmd: \d+ aim: focus" "focus none" '
    # b237 (user 2026-11-18, c244b235b Stadium_SO1_B: "client isn't close enough to terminal to press interact, then
    # at the last terminal it spazzes out"): the 350 uu warp fallbacks jerked both pawns around and never reached the
    # 250 uu interactor range. The DLL now steps the presser in to 60 uu of the target (objective go, c284_step_in),
    # so the looks run from there and no warp follows.
    kind('use', 'retry 1 try client look actor {g1} 0 ;; ' + nf + 'try client look actor {g1} 0 bounds sweep ;; ' + nf +
         'try client look actor {g1} 0 bounds focus={g1}')
    kind('use', 'client shot %s-use' % tag)
    if C492:
        c492_outside()
    kind('use', 'client key E')
    if C492:
        c492_walk_in()
    # nest (Bugnest): each player faces the nearest nest's grenade hole within 1600 uu, gets grenades (b234
    # `grenade fill`), then throws a grenade; twice. A client grenade alone left
    # Bugnest C_11 alive after four throws (c244b205b).
    for i in range(2):
        kind('nest', 'host clear 1500 kill')
        # b236: client first. c244b235a's host G killed every nest first, so each client G was skipped and the
        # card 494 after-throw rows (the client's own throw) never ran.
        for role in ('client', 'host'):
            # b223 (senior order): c244b222b voided 8 host looks at lines 93-357 (aim on Landscape_2 at 1766 uu,
            # terrain between). One retry step: an ungated look (no void); when its hit is not the nest, warp the
            # player 300 uu from the nest; then the gated look. A void runs the list once more. The aim stays at the
            # origin + 200: c244b222c's bounds looks aimed 26-28 deg up at Actor_18 (Bugnest bounds extent 2785 2785
            # 2000, the spawn area), every nest look voided.
            # b223: c244b222d voided 4 looks (lines 163, 217, 225, 279) at a nest already killed after the block's go
            # (C_11 died at t=661928859, the client look came at 661933000; the ray then passes to Landscape). Each
            # nest step now runs only while no Bugnest kill row follows the block's objective go.
            alive = 'when host last "(card098: host t=\\d+ %s|cmd: \\d+ objective go)" "objective go" ' % NEST
            if role == 'client':
                # b229: c244b228a line 219 voided: a WarriorBug at 0 uu blocked the client's nest look; it spawned in
                # the 4 s of host nest steps after line 214's clear. A second clear right before the client look (2
                # steps per block: 2568 of the 2600 cap).
                kind('nest', alive + 'host clear 1500 kill')
            warp = 'host warp actor' if role == 'host' else 'host warp client'
            # b232 (user: "grenades to destroy bug hole need to be aimed at yellow center"): a nest dies only to a
            # blast in its hole, so every nest look faces the nearest grenade hole component (`hole`), not origin +
            # 200 (the grenade hit the mound side; c244b231a Park_SO2_* objective waits never saw a kill).
            # b233: c244b233a line 92 voided: the client's first look hit the nest at 4787 uu, so the warp (gated on a
            # non-nest hit only) was skipped and the within=1600 look failed both tries. The warp now also runs on a
            # miss or a nest hit past 1599 uu.
            kind('nest', alive + 'retry 1 try %s look actor %s 0 hole ;; when %s last "cmd: \\d+ aim: (no )?hit " '
                 '"hit (?!%s)|no hit|, (1[6-9]\\d\\d|[2-9]\\d{3}|\\d{5,}) uu" %s %s 300 ;; '
                 'try %s look actor %s 0 hole within=1600 hit=%s' %
                 (role, NEST, role, NEST, warp, NEST, role, NEST, NEST))
            if i == 0:  # one aim proof shot per player and block (the 2500 step cap)
                kind('nest', alive + '%s shot %s-nest' % (role, tag))
            # b234 (user: "they are trying to shoot the bug nests... just give them enough grenades to kill nests"):
            # no fire at the nest; the host raises every player's frag count to 5 before each throw.
            kind('nest', alive + 'host grenade fill 5')
            kind('nest', alive + 'try %s key G 200' % role)
            # b235: c244b234a line 295 voided: the host's look ran 2 s after the client's G (line 293), while the nest
            # was dying (no hit within 10000 uu both tries). The next look waits for this throw's kill row (the fuse
            # lands it about 2 s after the press, so `new` from here sees it); no kill in 5 s goes on (try).
            kind('nest', alive + 'try host wait log "card098: host t=\\d+ %s" new timeout=5' % NEST)
    # other at a satchel building (Park_SO1 Flakbug C_0..2, Rubble C_2): `objective now` has no kind row for the class
    # ("needs: no kind row for these classes: void", c244b205c), so the go is "other" at the building. The client faces
    # the nearest placement site and holds E, as the C1M1 CTF satchel step does (b14 soak: key E 3200).
    # The go stand is the building's; walk to the nearest site first (b13 CTF: goto, then look).
    # The site takes focus only once the player carries a satchel: c244b206b looks at the site from 161..223 uu all
    # ended "focus none", as C1M1 CTF needs its SO3 satchel pickup before the SO4 site (b13 soak). Pick up the nearest
    # BP_SatchelItemPickup_C first (11 placed in C4M1, one at (-5992, -72581) near Flakbug C_2).
    # Every block picks up, held or not: the Park pickups come in pairs (C_7/C_8, C_3/C_10) and the spares are the LZ
    # satchels (no level script spawns satchels; LZ_SO1 has no pickup near it, the nearest untaken is C_1, 15 km off).
    # c244b209a skipped the pickup with a satchel held and reached LZ_SO1 with 0 (c244b208b, always picking up, had 2).
    # b215 speed-up (senior): c244b213a's pickup and site walks took about 25 s each and two timed out at 38 s past
    # the navmesh end; long walks belong to the movement tests. The client is warped 300 uu short of the nearest
    # untaken pickup (warp live, the same c64_find_actor test as the goto; from the LZ it resolved C_1 in c244b209b),
    # then walks the rest. The site goto after it fails at the LZ and the site's front warp brings the client back.
    # b236 (user 2026-11-18: "you could just teleport... why are you running"): no walks. The client is warped
    # 100 uu from the nearest untaken pickup and 200 uu in front of the nearest site; the site look list below
    # covers facing (front, then behind).
    kind('other', 'try host warp client BP_SatchelItemPickup_C 100 live')
    kind('other', 'try client look actor BP_SatchelItemPickup_C 0 bounds live focus=BP_SatchelItemPickup_C')
    kind('other', 'client key E')
    # b241 (user 2026-11-18, c247b240a: "client keeps spinning ... looking at a satchel placement point"): the site
    # front warp took the site nearest the client copy, which after the pickup warp (C_4 at -54051 58019) was C_5
    # (-49501 56120), not the current step's (Research_SO3_B/C); C_5's charge went off and every later look hit 0 of
    # 26. The client now warps to the current step's stand point first (the `objective now` stand row, its groups
    # replace {g1}; the wait section reads the id again), so the front warp resolves the step's own site.
    kind('other', 'try host wait log "%s" since-mark timeout=2' % STAND_CAPTURE)
    kind('other', 'try host warp client point {g1} {g2} {g3}')
    kind('other', 'host warp client BP_SatchelPlacementSite_C 200 front')
    # No within=: the site's bounds trace hits 0 of 26 points (the building mesh covers it), and a
    # focus-swept look prints no distance row, so within= voided all 18 looks and every E (c244b206a).
    # Origin look, not bounds: c244b207a's bounds scan hit 0 of 26 points and its focus sweep ended at pitch +29.6,
    # focus none, while card456 rows at the goto end (617829735) name the site as the interactor's hold target, so
    # the site took focus on the walk in, facing the actor origin the goto steered to.
    # focus= is a name prefix and the site is a child actor named SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_<n>:
    # c244b207b look 81 had focus SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_2 and voided on focus=BP_Satchel...
    # b209: LZ_SO1's site (CAT_4, Rubble C_2) is off the navmesh: c244b208b's 37 site gotos ended "750 uu past the
    # navmesh end" at (23276, -84646), with the Rubble building between that spot and the site, so the look hit the
    # Rubble at 93 uu (aim range 250), focus none. A failed site goto warps the client 200 uu in front of the site
    # along its forward axis; a look with focus none then tries 200 uu behind it (the site's facing is not known
    # statically).
    # b223 (senior order): c244b222e voided this gated look at line 471 (Park_SO1_A). The site look is now the use
    # kind's retry list: an ungated look (no void), and while focus is none a 200 uu front warp + ungated look, then a
    # 200 uu behind warp; the one gated look comes last, and a void starts the list again from where the client stands.
    site = 'BP_SatchelPlacementSite_C'
    nf = r'when client last "cmd: \d+ aim: focus" "focus none" '
    kind('other', 'retry 1 try client look actor %s 0 ;; ' % site + nf + 'host warp client %s 200 front ;; ' % site +
         nf + 'try client look actor %s 0 ;; ' % site + nf + 'host warp client %s 200 behind ;; ' % site +
         'try client look actor %s 0 focus=%s' % (site, SITE_FOCUS))
    kind('other', 'client shot %s-satchel' % tag)
    # b236 (senior, c244b234a Park_SO1_C STUCK at line 1149): live bugs interrupted the 3.2 s holds; clear before each.
    kind('other', 'host clear 1500 kill ' + WAVE_LEFT)
    kind('other', 'client key E 3200')
    # b223 (senior order): in c244b222e the focus= gate passed while the press named no target ("Use press:
    # interactor target null", card456 started 0) about 380 uu from the site's bounds centre. While the last press
    # names anything but a SatchelSiteCAC_ child, the client is warped 150 uu in front of the site (then 150 behind,
    # the site's facing is not known statically), looks, and holds E again: 2 retries. A null target with the client
    # within 150 uu facing the site is a mod bug, not a test miss.
    # b237 (user 2026-11-18, c244b236a Park_SO1 CAT_1: "client standing still not planting satchel"): the press named
    # the site, then 136 ms in the hold broke ("card456: hold interrupted ... started 1 completed 0 progress 0.13,
    # trace now null"; Flakbug C_1 turned 50 ms before the press) and no retry ran, so the client stood through the
    # step waits. The last press or hold-end row decides: a press at a non-site target, or a hold that ended with
    # completed 0, retries.
    miss = (r'when client last "(card050: client t=\d+ Use press: interactor target |card456: client t=\d+ hold '
            r'(released|interrupted) )" "(target (?!%s)|completed 0 )" ' % SITE_FOCUS)
    for side in ('front', 'behind'):
        kind('other', miss + 'host warp client %s 150 %s' % (site, side))
        kind('other', miss + 'try client look actor %s 0' % site)
        kind('other', miss + 'host clear 1500 kill ' + WAVE_LEFT)
        kind('other', miss + 'client key E 3200')
    # b241 (senior): one second hold on a site this client just placed, for the CanInteract-false HoldDone row. It
    # runs once per run (no "key E 3201" row since attach) and only when the client's last hold event is its own
    # completed hold (not a later press). One step: nested whens.
    once = r'when client last "(UBWCoop attach: proxy module|cmd: \d+ key E 3201 )" "UBWCoop attach" '
    placed = (r'when client last "(card133: client t=\d+ own hold completed on |card050: client t=\d+ Use press: '
              r'interactor target )" "own hold completed" ')
    kind('other', once + placed + 'client key E 3201')
    # The id again (the use row replaced the groups), then the wait with wave holds. Its own "# ---" group, so a
    # failed action step above does not cut these waits to the dead-wait cap (drive.py group_of, c244b205a).
    out.append('# --- block %02d: wait for the step' % n)
    out.append('try host wait log "%s" since-mark timeout=2' % SUM_CAPTURE)
    # b212 (card 244 LZ_SO2): a marker step waits 5 s, so the next block's `objective go` for the same open step
    # (c284_marker_next_presence) moves on to the next nearest switched-on presence sooner.
    # A failed go prints no kind row: that block keeps the 15 s wait.
    when(HAS, 'when host last "%s" "host kind marker " try host objective wait {g1} 5' % GO_RX)
    when(HAS, 'when host last "%s" "^(?!.*host kind marker )" try host objective wait {g1} 15' % GO_RX)
    for _ in range(HOLD_ROUNDS):
        when(HAS, 'host clear 4000 kill ' + WAVE_LEFT)
        when(HAS, 'try host objective wait {g1} 3')
    when(HAS, r'try client wait log "card049: client t=\d+ replayed Complete\w*Objective\((\w+, )?{g1}\)" timeout=8')
    # b238 (c247b237a: the current step Research_SO3_C, a satchel site with no charge carried, held six blocks while the
    # Research_SO1 PanelSwitches were never tried): when the block's last wait for the step failed, `objective next`
    # makes the next doable step current (sparse order, wrap; a satchel site no player can interact with is skipped),
    # and the next block's `objective now` names it. None doable: the step stays.
    # b239: \w+, not {g1}: drive.py's when does not substitute {gN} in its regex (c247b238a: never matched)
    when(HAS, r'when host last "cmd: \d+ objective wait \w+ (3|5|15) -> " "failed" try host objective next')
    # b215 (c244b214a: 9 blocks of 31 s idle after Stadium_SO2, "current none; no open step with its marker on"):
    # the open Stadium_Marker step completed only at the digest round's clear, so a step with no current marker
    # waits on a wave kill. Clear first, then wait for the next Add.
    when(NONE, 'host clear 4000 kill ' + WAVE_LEFT)
    when(NONE, r'try host wait log "card049: host t=\d+ (Add|Complete)" new timeout=15')


C485_RX = r'card485: client guid (0x\w+) first rendered .* body 0 at \((-?\d+), (-?\d+), (-?\d+)\)'


def c485_shot(tag):
    # Card 485 b209: the first-rendered row (pose_c132 c485_render_note) of the first body reposed since the last
    # digest's client mark: the client drew that copy for the first time after its freeze repose, with the bone and
    # body offsets in the row. Both players stand 250 uu off its body 0 on different axes, 150 uu up, face it, and
    # shoot. c244b209a: a 12 s wait for a fresh repose after a kill found none (1 repose in the first 8 min), so the
    # step takes the row from the digest window instead. Every step is try: no row leaves no groups.
    out.extend(['# === card 485 shot: %s' % tag, '# --- card 485 shot: %s' % tag,
                'try client wait log "%s" since-mark timeout=1' % C485_RX,
                'try host clear 4000 kill',
                'try host warp client point {g2+250} {g3} {g4+150}',
                'try host warp actor point {g2} {g3+250} {g4+150}',
                'try client look point {g2} {g3} {g4}',
                'try host look point {g2} {g3} {g4}',
                'try client shot c485-%s' % tag,
                'try host shot c485-%s' % tag])


def digest(tag):
    if tag != 'map start':
        c485_shot(tag.replace(' ', '-'))
    out.extend(['# === digest round: %s' % tag, '# --- digest round: %s' % tag, 'host mark', 'client mark', 'host objectives',
                r'host wait log "card132: host t=\d+ digest round \d+ \((command|timer)\) sent: " since-mark timeout=15',
                r'client wait log "card132: client round \d+: actors \d+ host, \d+ client, mismatches " since-mark timeout=30'])


HEAD = r'''# Card 244: FED_C4M1 full objective soak, discovery run (voidsoft). Generated by tools/gen_c244.py; edit that.
# Triggers: docs/cards/244-full-objective-soak-c4m1.md, step 1 table. Rules: docs/STATE.md "Gameplay test scenario
# rules". Every block reads the live current step; no id or coordinate table, no "objective complete".
# Skip ledger tools/scenarios/c244-passed.txt (user rule 2026-11-18): its ids complete by `objective complete`, not
# played. SKIP=0 python3 -I tools/gen_c244.py <dll> writes the full soak.
# Predictions (written before the run):
#  1. N of 22 ids complete on the host by their real triggers, each replayed on the client. Count:
#     grep -a -o -E 'card049: host sent Complete\w*Objective\([^)]*\)' logs/<run>-host.log | sort -u
#  2. Every digest round: mismatches 0. 0 EXCEPTION on both sides.
#  3. Open in this discovery run: the satchel buildings (Flakbug C_0..2, Rubble C_2), the Bridge_SO3 bombing run,
#     the four Metro panel switches, and whether the mission end travels to FED_C4M2.
# Run: systemd-run --user --scope -q -p MemoryMax=12G taskset -c 4-7,12-15 nice -n 10 env LANE=2
#      SCENARIO=tools/scenarios/c244-c4m1.txt FAST=1
#      HOST_EXTRA_ENV="UBWCOOP_DIAG=1 UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C4M1?listen UBWCOOP_GOD=1 UBWCOOP_C132_FX=1
#      UBWCOOP_C136_LOG=1" CLIENT_EXTRA_ENV="UBWCOOP_DIAG=1 UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1"
#      tools/run_pair.sh c244<build> 3600
voidsoft
void unless host dll %(dll)s
void unless client dll %(dll)s
host wait state map=FED_C4M1 timeout=240 void
client wait state map=FED_C4M1 pawn=client-own timeout=180
try client wait log "card120: client t=\d+ minimap origin \S+ \(BP_PlayerCharacterMI_C\) set" timeout=60
try host ui continue
try client wait log "card135: client t=\d+ screen prompt ready" timeout=30
client mark
try client ui continue
try client wait log "card067: dump \d+ in FED_C4M1 \(WB_Stack_C RequestGameUnpause\)" since-mark timeout=30
try host wait log "card320: host t=\d+ hold off after \d+ ms \(all confirmed\)" timeout=60
failsoft
host wait state pawn=host-own timeout=90
client shot c244-hud-start
host clear 4000 kill'''


C490_SITE = 'SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_4'  # b218: 21 km of navmesh from the start, 16 s at 1312
C490_FAR = 'BP_SatchelItemPickup_C_1'  # 27234 -99508 216, about 15 km from C490_SITE
# Card 490 close (b219): the host runs its own route west so the two pawns never share a path. c244b219a had both
# players on the same gotos and pawn-pawn contact gave 16-55 corrections per second for 12 s.
C490_HOST_A = 'SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_2'  # -6748 -75580 -480, about 33 km from the start
C490_HOST_B = 'SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_1'  # -22083 -87774 -492, about 20 km from C490_HOST_A
C490_HOST_OFF = 'BP_SatchelItemPickup_C_7'  # -29765 -83997 -43, about 8 km from C490_HOST_B


def c490_hold():
    """Card 490 proof: the host's infinite stamina option. On: both players sprint about 60 s (two sprint gotos each)
    and jump, on separate routes: the client start -> C490_SITE -> C490_FAR, the host start -> C490_HOST_A ->
    C490_HOST_B. The card490 rows must keep Stamina 100 with MaxWalkSpeed about 1312 on the host pc 0, its copy pc 1
    (role 3) and the client pc 0 (role 2). Reopen condition: more than 4 srv[] corrections per second with the hold on.
    Off: one more sprint goto each (client -> C490_SITE, host -> C490_HOST_OFF); the drain must match b218 (about 1.2
    per 100 ms, jump 5)."""
    out.append('# --- card 490: infinite stamina on (host option), 60 s sprint and jumps on separate routes, then off')
    out.extend(['client mark', 'host stamina on',
                r'client wait log "card490: client host stamina 1" since-mark timeout=10'])
    for host_target, client_target, secs in ((C490_HOST_A, C490_SITE, 35), (C490_HOST_B, C490_FAR, 35)):
        out.extend(['& host goto actor %s 300 sprint timeout=%d' % (host_target, secs),
                    'try client goto actor %s 300 sprint timeout=%d' % (client_target, secs), 'try join'])
        for _ in range(2):
            out.extend(['& host key W 1500 SpaceBar 300', 'client key W 1500 SpaceBar 300', 'try join'])
    out.extend(['host stamina off', 'host clear 4000 kill',
                '& host goto actor %s 300 sprint timeout=15' % C490_HOST_OFF,
                'try client goto actor %s 300 sprint timeout=15' % C490_SITE, 'try join',
                '& host key W 1500 SpaceBar 300', 'client key W 1500 SpaceBar 300', 'try join',
                'host clear 4000 kill'])


def main():
    out.append(HEAD % {'dll': DLL})
    # b236: c490_hold() dropped (card 490 closed at c244b219a; its 60 s of sprint walks proved nothing new).
    digest('map start')
    if C492:
        out.append('# Card 492 fix 6 proof run (C492=1): soak blocks up to the stadium end, then quit.')
    for n in range(C492_BLOCKS if C492 else BLOCKS):
        block(n)
        if n % 10 == 9:
            digest('after block %02d' % n)
    digest('map end')
    if C492:
        out.extend(['host objectives', 'client objectives', 'client quit', 'host quit'])
        print('\n'.join(out))
        return
    # b215: the mission end was never reached in 2 runs; 240 s waits idled 480 s in c244b214b (90 s covers an outro).
    out.extend(['host objectives', 'client objectives', 'host shot c244-end', 'client shot c244-end',
                'try host wait state map=FED_C4M2 timeout=90', 'try client wait state map=FED_C4M2 timeout=60',
                'client quit', 'host quit'])
    print('\n'.join(out))


main()
