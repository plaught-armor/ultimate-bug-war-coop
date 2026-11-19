#!/usr/bin/env python3
"""Card 245: writes tools/scenarios/c245-c4m2.txt, the FED_C4M2 full objective soak (discovery form, voidsoft).

Objective-driven (card 284): every step reads the live objective (expect / go / wait); no coordinate table. The only
table is tools/c245_eggs.txt, the 81 egg actor names by count area, taken from the level script's EggDestroyed bound
events (card 245 step 1 part B). Usage: python3 -I tools/gen_c245.py <dll hash16> > tools/scenarios/c245-c4m2.txt
"""
import sys

DLL = sys.argv[1] if len(sys.argv) > 1 else "0000000000000000"
out = []


def sent(oid):
    return r'card049: host sent Complete\w*Objective\((\w+, )?%s\)' % oid


def replayed(oid):
    return r'card049: client t=\d+ replayed Complete\w*Objective\((\w+, )?%s\)' % oid


def unless(oid):
    return 'unless host log "%s" count=1 ' % sent(oid)


def done(oid, seconds=60):
    out.append('host objective wait %s %d' % (oid, seconds))
    out.append('try client wait log "%s" timeout=8' % replayed(oid))


def presence(oid, completes=None):
    """Both players into the step's volume; `completes`: the id the trigger completes when it is not the step."""
    end = completes or oid
    # b213: a presence card 301 enables with the players already inside completes on the enable frame (c245b213a:
    # BrainBug_SO1 completed at the C_176 enable, 3 s before its expect, which voided); the expect is skipped then.
    out.extend(['host objective now', unless(end) + 'host objective expect %s' % oid, 'host clear 1500 kill',
                'host objective go all', 'try host objective wait %s 10' % end])
    for _ in range(3):
        out.append(unless(end) + 'host objective go all')
    done(end)


def use(oid, switch, completes=None, seconds=30, after=()):
    """The client presses the step's switch with E from the card 284 use stand; `after`: steps run after the E."""
    base = switch.rsplit('_', 1)[0]
    out.extend(['host objective expect %s' % oid, 'host clear 1500 kill', 'host mark', 'host objective go all',
                # A switch row names the actor and "winner <dir>"; a pickup row (b188) names the class and "from its
                # bounds", and its winner is "+x", which \w+ does not match. Both print object_name without the
                # number (b193: "use stand BP_Switch_C:" for BP_Switch_C_3), so since-mark keeps an earlier
                # switch's row (BP_Switch_C_0) from matching.
                r'try host wait log "c284: use stand (?:%s: .* winner \S+|%s from its bounds: )" since-mark timeout=5'
                % (base, base),
                'client look actor %s 0 bounds focus=%s' % (switch, switch),
                'client shot c245-%s' % oid.lower(), 'client key E'])
    out.extend(after)
    done(completes or oid, seconds)


# b196 order: a clear inside a wave hold runs while the level script still spawns waves, so bugs left after it are
# expected (c245b194b: Plaza_SO2b 4 left, Plaza_O1 1 to 2 left voided the run). Those clears allow bugs left; every
# other clear keeps the default max-left=0.
WAVE_LEFT = 'max-left=99'


def hold(oid, rounds=30, radius=4000, go=False):
    """An encounter-end step: the host kills the waves by code until the level script completes the id. `go`: both
    players first move to the live current objective (a child encounter far from the last stand: b194b BrainBug_SO2_R2,
    encounter BP_EM_1FactionSetWaves_C_6, never ended under 4000 uu clears at the arena). Card 489: only the first
    `go` must stand; each later one walks the next presence near the marker (c284_marker_next_presence) and may find
    none, as BrainBug_SO2_R2 did in c245b215a-b217a. Those subs are kill steps (an archer bug at each marker, in waves
    of two), so the clears complete them and a later `go` with no presence left is a `try`."""
    out.append('host objective now')
    for i in range(rounds):
        if go:
            out.append(unless(oid) + ('host ' if i == 0 else 'try host ') + 'objective go all')
        out.append(unless(oid) + 'host clear %d kill %s' % (radius, WAVE_LEFT))
        out.append(unless(oid) + 'try host objective wait %s 3' % oid)
    done(oid, 30)


def digest(tag):
    out.extend(['# digest round: %s' % tag, 'host mark', 'client mark', 'host objectives',
                r'host wait log "card132: host t=\d+ digest round \d+ \((command|timer)\) sent: " since-mark timeout=15',
                r'client wait log "card132: client round \d+: actors \d+ host, \d+ client, mismatches " since-mark timeout=30'])


# Card 323 done-when (join while the host is in a seat): the client disconnects and rejoins (card 045 follow, the c447
# steps) while the host sits on a plaza turret, then while it pilots a mech. Placed after the BrainBug_SO2 hold so the
# card 491 award rows at SO2 come from the client's first pawn (a rejoin makes a new pawn, ResupplyLevel 0).
# Join while the host is downed is not in this run: with two players the client's leave makes the host's Downed entry
# all down (card 366, death.cpp c43_host_step), so the host goes Dead; join while Dead is c375. A downed-host join
# needs a third player (Up) and rides the card 209 three-instance run.
REJOIN_SEATS = (
    # (tag, enter steps, seat actor for the client's look, leave steps)
    ('turret', ['host warp actor point -19569 13346 3502', 'host warp actor BP_MountableTurret_C 90 behind',
                'host look actor BP_MountableTurret_C', 'sleep 1', 'host key E', 'host wait state pawn=other timeout=10'],
     'BP_MountableTurret_C', ['host key E', 'try host wait state pawn=host-own timeout=10']),
    ('mech', ['try host warp actor BP_BaseMech_C 300', 'try host look actor BP_BaseMech_C 150', 'sleep 1', 'try host key E',
              'host wait state pawn=other timeout=10'],
     'BP_BaseMech_C', ['try host key E 2500 timeout=15', 'try host wait state pawn=host-own timeout=10']),
)


def rejoin_rounds():
    for tag, enter, seat, leave in REJOIN_SEATS:
        out.append('# --- Card 323 rejoin round: the host on the %s, the client leaves and rejoins.' % tag)
        out.append('host clear 4000 kill ' + WAVE_LEFT)
        out.extend(enter)
        out.extend(['host shot c245-323-%s-host' % tag, 'host mark', 'client mark', 'client disconnect',
                    r'client wait log "reconnected \(" since-mark timeout=120',
                    r'client wait log "possess: AcknowledgePossession\(.*Pawn \S+ BP_PlayerCharacterMI_C" since-mark timeout=120',
                    r'client wait log "card135: client t=\d+ screen prompt ready" since-mark timeout=60',
                    'try client ui continue', 'client wait state pawn=client-own timeout=120',
                    r'try host wait log "card323: host t=\d+ seat join replay to \S+: %s mech pilot row\(s\)" '
                    r'since-mark timeout=30' % (r'[1-9]\d*' if tag == 'mech' else r'\d+'),
                    'try client warp actor %s 600' % seat, 'try client look actor %s 150' % seat, 'sleep 1',
                    'client shot c245-323-%s-client' % tag])
        if tag == 'mech':
            # Card 452: the rejoined client's E on the piloted mech is refused (no second pilot).
            out.extend(['try client key E',
                        r'try client wait log "card452: client CanInteract on \S+ refused: piloted" since-mark timeout=5'])
        digest('card 323 rejoin, host on the %s' % tag)
        out.extend(leave)


def discriminator(egg):
    """Card 480: one client round, then one host round, on the same 500-Health egg. Kill rows print only on the lethal
    hit (c98_host_kill), so a row "Health 250.0 -> 0.0" on the host round proves the client round did 250 damage."""
    out.extend(['# --- Card 480 discriminator on %s: client round, then host round, same egg.' % egg,
                'client ammo fill', 'host warp actor %s 900' % egg, 'host clear 2500 kill',
                'host warp client %s 500' % egg, 'host mark',
                'retry 1 client look actor %s 0 bounds hit=%s ;; client shot c245-c480-client ;; '
                'client key LeftMouseButton 2000' % (egg, egg),
                r'try host wait log "card157: host/standalone t=\d+ OnDamageCaused on BP_PlayerCharacterMI_C '
                r'\(server copy\) damage " since-mark timeout=5',
                'host warp client %s 1500' % egg, 'host warp actor %s 500' % egg, 'host mark',
                'retry 1 host look actor %s 0 bounds hit=%s ;; host shot c245-c480-host ;; '
                'host key LeftMouseButton 2000' % (egg, egg),
                r'try host wait log "card098: host t=\d+ \w+ %s Health [-0-9.]+ -> [-0-9.]+: kill relayed" '
                r'since-mark timeout=5' % egg])


def eggs(area, oid, names):
    """Clear-bugs-first rule: each egg dies by code (`kill actor`: InstaKillTarget, the game's DealDamage path), so the
    health component's OnDeath and the level's EggDestroyed count fire as for a real round."""
    out.extend(['# --- %s: %d eggs in area %s; each egg dies by code through DealDamage.' % (oid, len(names), area),
                'host objective now'])
    for egg in names:
        out.append(unless(oid) + 'host kill actor %s' % egg)
    done(oid, 20)
    out.extend(['host objectives', 'client inventory'])


def read_eggs(path):
    areas, cur = {}, None
    for line in open(path):
        if line.startswith('Eggs'):
            cur = line.split()[0][-1]
            areas[cur] = []
        elif line.strip():
            areas[cur].append(line.strip())
    assert [len(areas[a]) for a in 'ABC'] == [17, 34, 30], areas
    return areas


HEAD = r'''# Card 245: FED_C4M2 full objective soak, discovery run (voidsoft). Generated by tools/gen_c245.py; edit that.
# Play order and triggers: docs/cards/245-full-objective-soak-c4m2.md, step 1 part B. Rules: docs/STATE.md
# "Gameplay test scenario rules". Needs a build whose `clear <radius> kill` spares the Brain Bug (b185 order).
# Predictions (written before the run):
#  1. 35 of 35 objective ids complete on the host by their real triggers, each replayed on the client. Count:
#     grep -a -o -E 'card049: host sent Complete\w*Objective\([^)]*\)' logs/<run>-host.log | sort -u | wc -l
#  2. Tunnel_SO1, Plaza_SO5 and BrainBug_SO7 complete by cascade from their parents (no own trigger).
#  3. Sewers_SO1 completes at egg 13 of 17, SO2 at 22 of 34, SO3 at 22 of 30 (eggs die by code: `kill actor`, card 480 order b195).
#  4. BrainBug_SO1 + SO2 are added 5 s after the third island; the arena shield (StaticMeshActor_52) turns on at
#     BP_PlayerPresence_C_176 and the digest round after it has 0 StaticMeshActor_52 mismatches (card 237, on branch).
#  5. Every digest round: mismatches 0. 0 EXCEPTION on both sides. No `clear kill` row kills the Brain Bug.
# Open in this discovery run: the damage one egg needs (2 s Morita burst assumed), whether the Brain Bug takes damage
# before BrainBug_SO3, and the stage thresholds of `Brainbug Damaged`.
# Run: systemd-run --user --scope -q -p MemoryMax=12G taskset -c 4-7,12-15 nice -n 10 env LANE=2
#      SCENARIO=tools/scenarios/c245-c4m2.txt FAST=1
#      HOST_EXTRA_ENV="UBWCOOP_DIAG=1 UBWCOOP_AUTODROPSHIP=1 UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C4M2?listen UBWCOOP_GOD=1"
#      CLIENT_EXTRA_ENV="UBWCOOP_DIAG=1" tools/run_pair.sh c245a 3600
# Expected wall: about 40 min.
voidsoft
void unless host dll %(dll)s
void unless client dll %(dll)s
host wait state map=FED_C4M2 timeout=240 void
client wait state map=FED_C4M2 pawn=client-own timeout=180
try host ui continue
try client wait log "card135: client t=\d+ screen prompt ready" timeout=30
try client ui continue
host wait log "card320: host t=\d+ hold off after \d+ ms \(all confirmed\)" timeout=60
failsoft
host wait state pawn=host-own timeout=90
client shot c245-hud-start
host clear 4000 kill'''


def main():
    areas = read_eggs('tools/c245_eggs.txt')
    out.append(HEAD % {'dll': DLL})
    digest('map start (shield off branch)')
    out.append('# --- Tunnel: BP_PlayerPresence_C_6 completes Tunnel_O1; Tunnel_SO1 cascades; the three islands are added.')
    presence('Tunnel_SO1', completes='Tunnel_O1')
    out.append(r'host wait log "card049: host t=\d+ Add\w*Objectives?\((\w+, )?Sewers_SO0(, \w+)?\)" timeout=20')
    out.append('# --- Plaza: presence C_24, switch C_0, three encounter ends, presence C_54 (completes Plaza_O1).')
    presence('Plaza_SO1')
    # Card 492 fix 2 on FED_C4M2: the client's press of BP_Switch_C_0 sets the card 288 trigger (the Gigantic C_2
    # close @5339 runs in the same call as SwitchPressedDelegate @26080).
    use('Plaza_SO2a', 'BP_Switch_C_0', after=[r'try host wait log "card492: host t=\d+ switch press BP_Switch_C_0 '
                                              r'\(\S+\) by \S+: card 288 trigger set" since-mark timeout=5'])
    hold('Plaza_SO2b', rounds=60)  # c245a: completed about 120 s after its add, past 30 rounds
    # c245a: Plaza_SO3 is the flamethrower pickup (ObjectiveActors[0] BP_WeaponPickup_FlameThrower_C_0), not an
    # encounter end. Needs the b187 kind row (WeaponPickup -> use).
    # b196 order (c245b196a line 202, same in b194d): the E pickup works (client and host DisablePickup at once, the
    # look's focus the pickup). The E starts the Plaza_SO3 wave: c245b199a, DisablePickup 603556424, then 3.8 s later
    # StartEncounter on BP_EM_1FactionSetWaves_C spawns two hostile BP_AI_ControllerTrooper_C beside the pickup, and
    # Plaza_SO3 completes 150 ms after both die (friendly AI killed them, 32 to 47 s after the E across b194d..b199a).
    # The b198 clear ran before the spawn and killed 0. Wait for the StartEncounter row, then clear (ControllerTrooper
    # is a clear target, b191).
    use('Plaza_SO3', 'BP_WeaponPickup_FlameThrower_C_0', after=(
        r'try host wait log "card245: host t=\d+ StartEncounter on BP_EM_1FactionSetWaves_C" since-mark timeout=10',
        'sleep 1', 'host clear 4000 kill ' + WAVE_LEFT))
    hold('Plaza_SO4')
    # b192: in b191 `objective go all` stood both players inside the nearest presence and Plaza_O1 never completed.
    # Card 245 prediction: C_54 is disabled until the SO5 dropship encounter's survivors die; the card245 overlap
    # rows name the failing gate. Clear the survivors, then both players into BP_PlayerPresence_C_54 by name.
    out.extend(['host objective now', 'host objective expect Plaza_SO5'])
    for _ in range(10):
        out.append(unless('Plaza_O1') + 'host clear 4000 kill ' + WAVE_LEFT)
        out.append(unless('Plaza_O1') + 'try host objective wait Plaza_O1 3')
    out.extend([unless('Plaza_O1') + 'host objective now',
                unless('Plaza_O1') + 'host warp actor BP_PlayerPresence_C_54 10',
                unless('Plaza_O1') + 'host warp client BP_PlayerPresence_C_54 60',
                unless('Plaza_O1') + 'try host objective wait Plaza_O1 10'])
    for _ in range(2):
        out.append(unless('Plaza_O1') + 'host clear 4000 kill ' + WAVE_LEFT)
        out.append(unless('Plaza_O1') + 'host warp actor BP_PlayerPresence_C_54 10')
        out.append(unless('Plaza_O1') + 'host warp client BP_PlayerPresence_C_54 60')
        out.append(unless('Plaza_O1') + 'try host objective wait Plaza_O1 10')
    done('Plaza_O1')
    digest('Plaza done')
    out.append('# --- Bridge: presence C_23, satchel pickup, four sites, switch C_3 (BridgeExplode, +3 s).')
    presence('Bridge_SO1')
    # b193: the level script (ExecuteUbergraph_FED_C4M2_Scripting 0x4cb9) adds 1 to its Satchels count on every
    # BP_SatchelItemPickup_C OnPickup and completes Bridge_SO2 only when Satchels == 4. Card 482 Done-when: the client
    # takes all four pickups and places all four sites (each placement uses one of the placer's own satchels). The
    # card245 pickup row prints the host level's Satchels after each take.
    out.extend(['host objective expect Bridge_SO2', 'host clear 3000 kill'])
    for count, (role, n) in enumerate((('client', 3), ('client', 5), ('client', 7), ('client', 9)), 1):
        pick = 'BP_SatchelItemPickup_C_%d' % n
        if role == 'client':
            out.extend(['host warp actor %s 600' % pick, 'host warp client %s 150' % pick])
        else:
            out.extend(['host warp client %s 600' % pick, 'host warp actor %s 300' % pick,
                        'host goto actor %s 100' % pick, 'host wait goto timeout=60'])
        out.extend(['%s look actor %s 0 bounds focus=BP_SatchelItemPickup_C' % (role, pick),
                    '%s shot c245-bridge-satchel-%d' % (role, n), '%s key E' % role,
                    r'try host wait log "card245: host t=\d+ pickup BP_SatchelItemPickup_C .* after the base call: '
                    r'.* Satchels %d" timeout=10' % count])
    done('Bridge_SO2', 20)
    for i, s in enumerate('abcd'):
        oid = 'Bridge_SO3%s' % s
        site = 'BP_LevelObjectiveSatchelSIte_C_%d' % (i + 1)
        role = 'client'  # card 482: the client holds all four satchels
        out.extend(['host objective expect %s' % oid, 'host clear 1500 kill'])
        if role == 'client':
            out.extend(['host warp actor %s 600' % site, 'host warp client %s 150' % site])
        else:
            out.extend(['host warp client %s 600' % site, 'host warp actor %s 150' % site])
        out.extend(['%s look actor %s 0 bounds focus=BP_LevelObjectiveSatchelSIte_C' % (role, site),
                    '%s shot c245-bridge-site-%s' % (role, s), '%s key E 3500' % role])
        done(oid, 20)
    done('Bridge_SO3', 15)
    use('Bridge_SO4', 'BP_Switch_C_3')
    done('Bridge_O1', 15)
    digest('Bridge done')
    out.append('# --- Sewers: presence C_19, then the egg counts.')
    presence('Sewers_SO0')
    discriminator('BP_DestructibleEggs_GlowingEmissive_Base_C_0')
    for area, oid in (('A', 'Sewers_SO1'), ('B', 'Sewers_SO2'), ('C', 'Sewers_SO3')):
        if area == 'C':
            # b212 card 301 gap G: the third island (Sewers_O1) adds BrainBug_O1 and enables BP_PlayerPresence_C_176
            # (@20514), then tests only player 0 inside (@20547-20646). The client waits alone inside C_176 while the
            # host kills the last eggs by code from the sewers; detour_c301_exec_enable sends the client in.
            out.extend(['host warp client BP_PlayerPresence_C_176 60', 'client shot c245-c301-client-inside'])
        eggs(area, oid, areas[area])
    done('Sewers_O1', 15)
    digest('Sewers done')
    out.append('# --- Brain Bug: added 5 s after the third island; presence C_176 turns the arena shield on.')
    out.append(r'host wait log "card049: host t=\d+ Add\w*Objectives?\((\w+, )?BrainBug_SO1(, \w+)?\)" timeout=30')
    out.append(r'try host wait log "card301: host t=\d+ presence BP_PlayerPresence_C_176 enabled with \d+ other player\(s\) '
               r'inside: \d+ sent" timeout=15')
    out.append('client shot c245-c301-after-enable')
    presence('BrainBug_SO1')
    digest('after C_176 (card 237: shield on branch, StaticMeshActor_52)')
    out.append('host shot c245-shield-on')
    out.append('client shot c245-shield-on')
    hold('BrainBug_SO2', radius=20000, go=True)
    rejoin_rounds()
    out.extend(['host objectives',
                '# Boss rounds: the client fires on the Brain Bug; the host kills each stage encounter by code.'])
    # b213: the clear is centred on the host pawn, and the host stands wherever the last hold left it. In c245b212a
    # that was the SO2 marker (5254,-41821), about 15500 uu off the boss arena (20544,-44432); clear 3000 killed 0-4
    # per round, the four archer encounters (SO4: Archers Completed reaches 4) ended slowly, and SO4 took 165 s, against
    # 17 s in c245b204a with the host about 5000 uu off. The radius matches the hold's 20000; the Brain Bug is spared.
    u = unless('BrainBug_O1')
    # b223 (senior, c245b223a line 659): the Brain Bug spawns after SO2 at a varying delay (card098 spawn row 0.48 s
    # after SO2 in c245b222a, 2.9 s in c245b223a), and round 00 warped at 0.9 s ("no such actor"), which cut the
    # group's later stage waits to 1 s. Wait for the host spawn row before the first boss step.
    out.append(u + r'host wait log "card098: host spawn BP_AI_BrainBug_C " timeout=30')
    # b228 (senior, c245b227a line 660): the client's own copy arrived 428 ms after the host spawn row (host 669143345,
    # client 669143773) and the warp ran before it ("no such actor"). Wait for the client's row for the spawned
    # instance; the load-time rows name Default__BP_AI_BrainBug_C, so this regex does not match them.
    # b257 (c245b257a line 720): after the card 323 rejoins the client's pawn stood at its start, 67000 uu from the
    # Brain Bug (net cull 15000), so its copy never arrived until the round 00 warp (client spawn row 41 ms after it).
    # The host warps the client's pawn next to the host's actor first; the client's copy then arrives in relevance.
    out.append(u + 'host warp client BP_AI_BrainBug_C 900')
    out.append(u + r'client wait log "card098: client collision BP_AI_BrainBug_C BP_AI_BrainBug_C " timeout=30')
    # b215 speed-up (senior; user rule: objective tests kill by code): c245b213a ran 27+ fire rounds at 5.9 s each.
    # Round 00 is the real fire proof; then the Brain Bug is killed by code (InstaKillTarget on the nearest,
    # `host kill actor`) after the stage encounters are cleared, and BrainBug_O1 is waited for. The fire rounds after
    # it stay as the fallback, skipped once BrainBug_O1 is sent. Open: whether the stage steps SO3..SO6 (driven by
    # `Brainbug Damaged`) still complete when the kill is one hit.
    for i in range(40):
        if i == 1:
            # b216 (senior order after c245b215a: the one-hit kill completed SO5 and O1 but never SO3, SO4, SO6):
            # one damage step per stage, each waiting on its stage's Complete row, before the final kill. c245b213a
            # boss health (MaxHealth 6000): SO3 completed at the hit that took it 4634 -> 4384 (under 75%), SO5 at
            # 2102 -> 1852 (under about 33%); SO4 and SO6 complete by their encounter ends, and the boss took no
            # damage during SO4 (4384 -> 4384). So: 30% (6000 -> 4200), clear for SO4, 40% (-> 1800), clear for SO6.
            # c245b216a: the failed SO2_R2 marker step (line 570) shares the boss group, so the dead-wait cap cut these
            # stage waits to 1 s; the stages get their own group.
            out.append('# --- Brain Bug stages: one damage step per stage, each waiting on its Complete row.')
            stage = r'host wait log "card049: host t=\d+ CompleteSubObjective\(BrainBug_O1, BrainBug_SO%d\)" timeout=%d'
            out.append(u + 'host clear 20000 kill ' + WAVE_LEFT)
            out.append(u + 'try host kill actor BP_AI_BrainBug_C 30')
            out.append(u + 'try ' + stage % (3, 15))
            out.append(u + 'host clear 20000 kill ' + WAVE_LEFT)
            out.append(u + 'try ' + stage % (4, 20))
            out.append(u + 'host clear 20000 kill ' + WAVE_LEFT)
            out.append(u + 'try host kill actor BP_AI_BrainBug_C 40')
            out.append(u + 'try ' + stage % (5, 15))
            out.append(u + 'host clear 20000 kill ' + WAVE_LEFT)
            out.append(u + 'try ' + stage % (6, 20))
            out.append(u + 'host clear 20000 kill ' + WAVE_LEFT)
            out.append(u + 'try host kill actor BP_AI_BrainBug_C')
            out.append(u + 'try host objective wait BrainBug_O1 20')
        out.append(u + 'host clear 20000 kill ' + WAVE_LEFT)
        out.append(u + 'client ammo fill')
        # b196 order (c245b196a line 732): the look voided twice from one landing (StaticMeshActor_2787, then Actor_7
        # at 3227 uu): the Brain Bug moves between stage spots, so a retry warps again before it looks.
        # c245b202a line 674: round 05 still voided after both retries (Actor_7 at 1007 uu, StaticMeshActor_2787),
        # so the senior order for b203 is more retries at a closer stand: retry 3 (drive.py MAX_RETRY) at 900 uu.
        out.append(u + 'retry 3 host warp client BP_AI_BrainBug_C 900 ;; '
                       'client look actor BP_AI_BrainBug_C 0 bounds hit=BP_AI_BrainBug ;; '
                       'client shot c245-boss-%02d ;; client key LeftMouseButton 5000' % i)
        # c245b201a line 720: "objective now" failed "no current step" in the gap between BrainBug_SO7 and the
        # BrainBug_O1 complete; the "host objectives" rows before and after the rounds show the state instead.
    done('BrainBug_O1', 30)
    digest('map end')
    out.extend(['host objectives', 'client inventory', 'host shot c245-end', 'client shot c245-end', 'client quit', 'host quit'])
    print('\n'.join(out))


main()
