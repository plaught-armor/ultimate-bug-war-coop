#!/usr/bin/env python3
"""Card 247: writes tools/scenarios/c247-c5m2.txt, the FED_C5M2 full objective soak (discovery form, voidsoft).

Derived from tools/gen_c244.py (2026-11-18, senior): same objective-driven blocks; the b2xx history comments are
C4M1 runs and are kept as the reason for each step.

Objective-driven (card 284): no id or coordinate table. Each block reads the live current step (`objective now`
summary row), makes it current (`objective expect`), moves both players there (`objective go all`), runs the action
its kind needs (use: client E at the card 284 use stand's class; nest: client grenade at the nearest bug nest), then
waits for the level script to complete it, killing the waves by code in between. The step-1 table in
docs/cards/247-full-objective-soak-c5m2.md lists the triggers this should reach; the discovery run says which do.
Usage: python3 -I tools/gen_c247.py <dll hash16> > tools/scenarios/c247-c5m2.txt
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
# C5M2's site focus is the site actor itself (c247b242a: 16 focus rows BP_SatchelPlacementSite_C_0/1/2, 0 SatchelSiteCAC_; each gated look voided).
SITE_FOCUS = 'BP_SatchelPlacementSite_C'
STAND_CAPTURE = r'cmd: \d+ objective now:   stand \((-?\d+), (-?\d+), (-?\d+)\) from actor '
HAS = r'"current (?!none;)\w+;"'
NONE = '"current none;|-> failed"'
# `objective go` prints the host's row with the kind it moved for, or fails.
GO_RX = r'cmd: \d+ objective go(: host kind \w+| all -> failed)'

# User rule 2026-11-18: an objective that passed with no issues is skipped, not played again. Skip ledger:
# tools/scenarios/c247-passed.txt, one id per line (# comments), each completed on the host and replayed on the client
# in the named run with 0 failed block waits. A block whose current step is in it completes the step by command
# (`objective complete`, card 095) and runs nothing else. Empty ledger or SKIP=0 in the env: every step is played.
# The full soak (SKIP=0) runs before each playtest build and when a card touches that objective's path.
def read_passed():
    import os
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scenarios', 'c247-passed.txt')
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


def when(test, step, rx=SUM_RX):
    out.append('when host last "%s" %s %s' % (rx, test, step))


def kind(k, step):
    when('"host kind %s "' % k, step, GO_RX)


def block(n):
    tag = 'c247-%02d' % n
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
    kind('use', 'client key E')
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


HEAD = r'''# Card 247: FED_C5M2 full objective soak, discovery run (voidsoft). Generated by tools/gen_c247.py; edit that.
# Triggers: docs/cards/247-full-objective-soak-c5m2.md, step 1 table. Rules: docs/STATE.md "Gameplay test scenario
# rules". Every block reads the live current step; no id or coordinate table, no "objective complete".
# Skip ledger tools/scenarios/c247-passed.txt (empty: no C5M2 step has passed yet).
# Predictions (written before the run):
#  1. N of the step-1 table ids complete on the host by their real triggers, each replayed on the client. Count:
#     grep -a -o -E 'card049: host sent Complete\w*Objective\([^)]*\)' logs/<run>-host.log | sort -u
#  2. Every digest round: mismatches 0. 0 EXCEPTION on both sides.
#  3. Open in this discovery run: Research_SO3 and Finale_SO4 satchel sites, the Array_SO3 door close (card 492, both
#     players inside first), Outpost_SO4 atWhiskey (C_402 presence), Finale_SO5 (no add in the script), and the
#     Finale_O1 dropship end.
# Run: systemd-run --user --scope -q -p MemoryMax=12G taskset -c 4-7,12-15 nice -n 10 env LANE=2
#      SCENARIO=tools/scenarios/c247-c5m2.txt FAST=1
#      HOST_EXTRA_ENV="UBWCOOP_DIAG=1 UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C5M2?listen UBWCOOP_GOD=1 UBWCOOP_C132_FX=1
#      UBWCOOP_C136_LOG=1" CLIENT_EXTRA_ENV="UBWCOOP_DIAG=1 UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1"
#      tools/run_pair.sh c247<build> 3600
voidsoft
void unless host dll %(dll)s
void unless client dll %(dll)s
host wait state map=FED_C5M2 timeout=240 void
client wait state map=FED_C5M2 pawn=client-own timeout=180
try client wait log "card120: client t=\d+ minimap origin \S+ \(BP_PlayerCharacterMI_C\) set" timeout=60
try host ui continue
try client wait log "card135: client t=\d+ screen prompt ready" timeout=30
client mark
try client ui continue
try client wait log "card067: dump \d+ in FED_C5M2 \(WB_Stack_C RequestGameUnpause\)" since-mark timeout=30
try host wait log "card320: host t=\d+ hold off after \d+ ms \(all confirmed\)" timeout=60
failsoft
host wait state pawn=host-own timeout=90
client shot c247-hud-start
host clear 4000 kill'''


# Card 493 proof (b248, senior): C493=1 in the env writes tools/scenarios/c493-c5m2.txt, a short run straight to the
# tunnel: the host warps to BP_PlayerPresence_C_408 (enabled at BeginPlay, not card 227 gated; its enter @30571
# arms `Cave in`), the client is warped 4000 uu short of it, and the run waits on the log for 2 card493 rerun rows.
# The soak never got there (c247b246a ended at Outpost_O1). Predictions: one "card493: host t=... armed:
# CallFunctionWithDelay 30.0 s 'Cave in'" row; then every 30 s one "cave in rerun for <client copy> at x y z, N uu
# from the host" row with N over 3000 (2 rows in about 65 s); "re-arms skipped" in the map tally equals the reruns;
# 0 EXCEPTION/FATAL on both sides.
C493 = os.environ.get('C493') == '1'
C493_PRESENCE = 'BP_PlayerPresence_C_408'  # -55226 70730 -2823 (c247b246a card244 presence row)
C493_RERUN = r'card493: host t=\d+ cave in rerun for '


def c493_tunnel():
    # b251: no client warp. `warp client C_408 4000` snapped the server copy to 2238 uu from the presence
    # (c493b250a cmd 5); the client stays at the map start, about 40000 uu from the tunnel. The rerun waits are try
    # steps: b251's probe (every CallFunctionWithDelay name and `Cave in` ProcessEvent, both sides) must log the
    # whole window even when nothing arms the loop (c493b250a: never armed).
    out.append('# --- card 493: straight to the tunnel, the client left at the map start, probe window')
    out.extend(['host mark', 'client mark', 'host warp actor %s 100' % C493_PRESENCE,
                'host clear 4000 kill max-left=99',
                r'try host wait log "card493: host t=\d+ armed: " since-mark timeout=30',
                'host shot c493-tunnel', 'client shot c493-tunnel',
                'try host wait log "%s" since-mark timeout=45' % C493_RERUN, 'host clear 4000 kill max-left=99',
                'try host wait log "%s" count=2 since-mark timeout=45' % C493_RERUN,
                'host shot c493-rerun2', 'client shot c493-rerun2'])
    digest('c493 end')
    out.extend(['host objectives', 'client objectives', 'client quit', 'host quit'])


# Card 497 proof (b253, senior): C497=1 in the env writes tools/scenarios/c497-c5m2.txt, the soak's first blocks
# only, to the Research_O1 completion (c247b245a: blocks 0 to 3 played Research_SO3_C, _B, _A and Research_SO4, then
# the level script ran CompleteObjective(Research_O1)). No b247 or later run reached it. Predictions: one "card497:
# host t=... CompleteObjective(Research_O1) sent before <n> nested rows (<k> non-complete)" row at the completion;
# Research_SO2 and Research_Marker2 complete 0 on both sides in the end `objectives` rows; 0 "card132: mismatch ...
# Research_" rows on either side; the client card491 score after Research_O1 equals the host's; 0 EXCEPTION/FATAL.
C497 = os.environ.get('C497') == '1'
C497_BLOCKS = 6  # 4 Research steps, 2 spare for a retried step
C497_DONE = r'card497: host t=\d+ CompleteObjective\(Research_O1\) sent before '


def c497_research():
    digest('map start')
    for n in range(C497_BLOCKS):
        block(n)
    out.append('# --- card 497: the Research_O1 completion row, then one digest round past it')
    out.append('try host wait log "%s" timeout=30' % C497_DONE)
    digest('c497 end')
    out.extend(['host objectives', 'client objectives', 'host shot c497-end', 'client shot c497-end',
                'client quit', 'host quit'])


def main():
    out.append(HEAD % {'dll': DLL})
    if C493 or C497:
        c493_tunnel() if C493 else c497_research()
        print('\n'.join(out))
        return
    digest('map start')
    for n in range(BLOCKS):
        block(n)
        if n % 10 == 9:
            digest('after block %02d' % n)
    digest('map end')
    # C5M2 is the last FED mission: Finale_O1 (dropship C_2 boarding) runs End The Level +27 s, then EndLevel @9540.
    # No next map is known statically; the end waits on the EndLevel row and both sides' final shots.
    out.extend(['host objectives', 'client objectives', 'host shot c247-end', 'client shot c247-end',
                r'try host wait log "card049: host t=\d+ (sent )?Complete\w*Objective\((\w+, )?Finale_O1\)" timeout=60',
                'client quit', 'host quit'])
    print('\n'.join(out))


main()
