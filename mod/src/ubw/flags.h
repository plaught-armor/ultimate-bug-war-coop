#pragma once

// Per-instance settings from environment variables. Two game instances share one exe
// directory, so nothing per-instance can live in a file next to the exe.
//   UBWCOOP_CLIENT_GAMEONLY "1" = once per client map load, after the client's local PC
//                           acknowledges its pawn, set FInputModeGameOnly on it (card 009
//                           fallback); unset or anything else = off
//   UBWCOOP_AUTOINPUT       "1" = card 014 scripted input: a delay after the local PC acknowledges
//                           its pawn, a fixed key/axis script runs through the PC's InputKey;
//                           a net client then exits the game 8 s after the script ends;
//                           unset or anything else = off
//   UBWCOOP_AUTOINPUT_DELAY_MS script start delay after the acknowledge (default 5000);
//                           lets the script run after the dropship intro
//   UBWCOOP_AUTODROPSHIP    "1" = card 015: host/standalone only, once per map, drive the local
//                           player's transport dropship to land through the game's own functions;
//                           on maps that start on foot it releases the WB_Stack_C mission-brief pause
//                           ("PRESS ANY KEY") instead, and every later WB_Stack_C pause (card 032);
//                           unset or anything else = off
//   UBWCOOP_SKIP_FLIGHT     "1" = card 090 test switch: after the autodropship unpause, the host calls
//                           SkipFlight(dropship, true) once, so the transport dropship skips its spline;
//                           needs UBWCOOP_AUTODROPSHIP=1 (else WARN, off); unset or anything else = off
//   UBWCOOP_HOST_SPRINT     "1" = card 019 host baseline: the host's own scripted Shift+W (instead of
//                           the card 014 script), armed when the host's character is possessed after
//                           the dropship landing; also starts the host's "own" rows. Uses
//                           UBWCOOP_AUTOINPUT_DELAY_MS; unset or anything else = off
//   UBWCOOP_GRANT_WEAPON    card 021 test loadout: a weapon DataTable row name (e.g. MoritaMk1, 1-32
//                           chars of [A-Za-z0-9_-]); each machine gives that weapon to its player
//                           pawns that lack it (host: own pawn and server copies; client: own pawn);
//                           unset or invalid = off
//   UBWCOOP_KILL_TROOPER    "1" = card 025 test: host/standalone only, once the local character is on
//                           foot, log the NPCs every 5 s for 60 s and kill the nearest NPC trooper once
//                           through the game's InstaKillTarget; unset or anything else = off
//   UBWCOOP_ENEMYAUDIT      "1" = card 023 enemy replication audit (see read_enemy_audit)
//   UBWCOOP_SPAWN_BUG       "1" = card 023 host debug bug spawn and test kill (see read_spawn_bug)
//   UBWCOOP_NO_ENEMY_HEALTH "1" = card 037 diagnosis: no client enemy health adoption (see read_no_enemy_health)
//   UBWCOOP_NO_CLIENT_GORE "1" = card 039 diagnosis: no client gib or corpse box call (see read_no_client_gore)
//   UBWCOOP_C39_OVERKILL "1" = card 039 test: card 023's test kill is an overkill (see read_c39_overkill)
//   UBWCOOP_C40_DAMAGE_TRACE "1" = card 040 damage pipeline trace (see read_c40_damage_trace)
//   UBWCOOP_C41_ANIM_TRACE "1" = card 041 NPC animation sequence trace (see read_c41_anim_trace)
//   UBWCOOP_C42_TEST "1" = card 042 corpse walk and close-overkill gib test (see read_c42_test)
//   UBWCOOP_C43_TEST "1" = card 043 downed and revive test (see read_c43_test)
//   UBWCOOP_C50_TEST "1" = card 050 weapon sync test, "054" = card 054 interaction relay test (see read_c50_test)
//   UBWCOOP_C57_SOLO "1" = card 057 solo weapon switch test, with UBWCOOP_C50_TEST (see read_c57_solo)
//   UBWCOOP_C65_LOG "1" = card 065 step 1 level-trigger log (see read_c65_log)
//   UBWCOOP_C68_LOG "1" = card 068 minimap chain log (see read_c68_log)
//   UBWCOOP_C69_LOG "1" = card 069 ammo and reload log (see read_c69_log)
//   UBWCOOP_C73_LOG "1" = card 073 grenade item and ability log (see read_c73_log)
//   UBWCOOP_C74_LOG "1" = card 074 grenade spawn log (see read_c74_log)
//   UBWCOOP_C76_LOG "1" = card 076 mech fire log (see read_c76_log)
//   UBWCOOP_C79_LOG "1" = card 079 mech destruction log (see read_c79_log)
//   UBWCOOP_C79_KILL "1" = card 079 test: lethal hits on a piloted mech, host (see read_c79_kill)
//   UBWCOOP_C79_DOWN "host"|"client"|"zipline" = card 079 test: that player goes down next to a mech, or (card 081)
//     the client next to a zipline point (see read_c79_down)
//   UBWCOOP_C66_CAP <32-512> = card 066 objective journal test cap (see read_c66_cap)
//   UBWCOOP_DIAG "1" = card 047 diagnostic hooks: installed only when on (see read_diag)
//   UBWCOOP_C60_HOST_OWN "1" = card 060 host-own switch test, with UBWCOOP_C50_TEST and a client (see read_c60_host_own)
//   UBWCOOP_CMD "1" = card 063 test runner: poll UBWCoop-<tag>.cmd, write UBWCoop-<tag>.state (see read_cmd)
//   UBWCOOP_C49_TEST <objective id> = card 049 objectives test (see read_c49_test)
//   UBWCOOP_KILL_PLAYER     "host" or "client" = card 030 player death audit (see read_kill_player)
//   UBWCOOP_OVERLAY_TEST    "1" or "script" = card 031 lobby overlay render test (see read_overlay_test)
//   UBWCOOP_SPAWN_MECH      "pickup", "mech" or "pad" = card 032 mech spawn and enter audit (see read_spawn_mech)
//   UBWCOOP_DUMP_SPRITE_ANIMS  "1" = card 027 test: host/standalone only, once the local character is on
//                           foot, dump the Morita trooper sprite animation assets (male, female) to
//                           card027-<asset>.txt next to the exe; unset or anything else = off

#include "ue/config.h"

#include <stddef.h>
#include <stdint.h>

namespace ubw::config {

inline constexpr size_t kMaxWeaponLen = 32;

// Reads UBWCOOP_CLIENT_GAMEONLY; true only for the exact value "1". Logs the result.
bool read_client_game_only();

// Reads UBWCOOP_AUTODROPSHIP; true only for the exact value "1". Logs the result.
bool read_auto_dropship();

// Reads UBWCOOP_SKIP_FLIGHT; true only for the exact value "1" with `auto_dropship` set. Logs the result.
bool read_skip_flight(bool auto_dropship);

// Reads UBWCOOP_KILL_TROOPER; true only for the exact value "1". Logs the result.
bool read_kill_trooper();

// Reads UBWCOOP_DUMP_SPRITE_ANIMS; true only for the exact value "1". Logs the result.
bool read_dump_sprite_anims();

// Reads UBWCOOP_AUDIT_029; true only for the exact value "1". Logs the result.
bool read_audit_029();

// Card 135: UBWCOOP_TEST_SKIP=1 (set only by tools/run_pair.sh): tests skip every cinematic (startup movies through
// movieskip.h, card 130 cine_skip.cpp) and close the brief's loading screen. Unset or anything else: off, the play
// build skips nothing and each player skips by the game's own button. Logs the result; test_skip() returns it after.
bool read_test_skip();
bool test_skip();
// Card 135: UBWCOOP_C135_KEEP_SCREEN=1 (tests only): cine_skip_step does not close the press-to-start screen after a
// skipped brief. No effect with UBWCOOP_AUTODROPSHIP=1 (its close stays; one WARN when both are set). Logs the result.
bool read_c135_keep_screen(bool auto_dropship);
bool c135_keep_screen();
// UBWCOOP_C28_SHOTS=1: card 028 screenshots of the remote player body (key changes, host orbit). Off by default.
bool read_c28_shots();
// UBWCOOP_C92_LOG=1: card 092 client per-frame log of the camera Z against the host's actor and sprite Z, for 10 s
// from the first frame the host is within 600 uu of the camera, plus 5 client shots (capped at 1200 lines). Client only.
bool read_c92_log();
bool read_c94_log();

// UBWCOOP_ENEMYAUDIT=1: card 023 enemy replication audit on this side (log rows, client facing, screenshots).
bool read_enemy_audit();
// UBWCOOP_SPAWN_BUG=1: card 023 test driver, host only: one BP_AI_WarriorBug per map 1500 units ahead after
// landing, then a test kill of it after the client's relayed fire. Off by default.
bool read_spawn_bug();
// UBWCOOP_NO_ENEMY_HEALTH=1: card 037 diagnosis run, client: no enemy health set adoption and no notify (the
// audit's set dumps still run). Off by default, so the client adopts and notifies.
bool read_no_enemy_health();
// UBWCOOP_GOD=1: card 132 test god mode. Host: every player pawn's Health loss is put back and a death event on a
// player pawn is skipped (damage is host-authoritative, so this covers the client's server copy). Client: a local
// death event on its own pawn is skipped. Off by default; the play build never sets it.
bool read_god();
// UBWCOOP_DECAL_CAP=<8-500>: card 132 test only, both sides: the decal pool cap (pool +0x34, 500 in the game) is set
// to this at the first decal spawn of each map, so a short run reaches the pool's FIFO eviction. Unset: 0, the
// game's cap is kept. The play build never sets it.
int32_t read_decal_cap();
bool read_no_client_gore();
bool read_c39_overkill();
// UBWCOOP_C40_DAMAGE_TRACE=1: card 040, host: log each stage of the damage pipeline for hits on non-trooper
// enemies, and (with UBWCOOP_SPAWN_BUG) a second bug and a long host burst after card 023's kill; the client aims
// its scripted fire at three heights. Log only. Off by default.
bool read_c40_damage_trace();
// UBWCOOP_C41_ANIM_TRACE=1: card 041, both sides: log each NPC animation sequence play, the callback delegate
// listeners and the callbacks, with 30 s sequence totals. Log only. Off by default.
bool read_c41_anim_trace();
// UBWCOOP_C42_TEST=1: card 042, both sides: the host kills the AI troopers, walks over a corpse, has the client walk
// over it too, then spawns two WarriorBugs close together and overkills them; the client takes screenshots of the
// gibs. Off by default.
bool read_c42_test();
// UBWCOOP_C43_TEST=1: card 043, both sides: the host downs the server copy and revives it, is downed and revived by
// the client, then both go down for game over; both sides take the card's screenshots. Off by default.
bool read_c43_test();
// UBWCOOP_C43_START_MS: the card 043 test's first hit, in ms after both pawns are on foot (20000-600000, default
// 20000; card 074 holds it until the client has taken a grenade).
uint32_t read_c43_start_ms();
// UBWCOOP_C138_HOLD_MS: card 138 test, client: the world BeginPlay dispatch is held at least this long, pawn or not
// (0-30000, default 0: held only while the local MI pawn is missing).
uint32_t read_c138_hold_ms();
// UBWCOOP_C45_TEST=1: card 045, with UBWCOOP_C43_TEST=1: the game over restarts with RESTART MISSON, the client
// reconnects, a down and revive runs, then RESTART CHECKPOINT; solo runs RESTART MISSON only. Off by default.
bool read_c45_test();
// UBWCOOP_C46_TEST=1: card 046, both sides: the host drops player pawns below the chapter kill height (the copy,
// the host, the copy at low Health, then the host with the copy Out); both sides take the card's screenshots. Solo:
// one host drop, a vanilla death. Off by default.
bool read_c46_test();
// UBWCOOP_C50_TEST=1: card 050, both sides: once per process, after both players are on foot, the host moves its own
// pawn and the server copy next to two placed weapon pickups; both players interact with them; the card's
// screenshots follow. Off by default.
int read_c50_test(); // 0 off, 1 card 050, 54 card 054
bool read_c57_solo();
bool read_c65_log();
bool read_c68_log(); // UBWCOOP_C68_LOG=1: card 068 minimap chain log
bool read_c69_log(); // UBWCOOP_C69_LOG=1: card 069 ammo and reload log
bool read_c73_log(); // UBWCOOP_C73_LOG=1: card 073 grenade item and ability log
bool read_c74_log(); // UBWCOOP_C74_LOG=1: card 074 grenade spawn log
bool read_c99_log(); // UBWCOOP_C99_LOG=1: card 099 cue log
bool read_c103_log(); // UBWCOOP_C103_LOG=1: card 103 enemy target log
bool read_c122_log(); // UBWCOOP_C122_LOG=1: card 122 attack pacing log
bool read_c125_log(); // UBWCOOP_C125_LOG=1: card 125 turret input and state log
bool read_c76_log(); // UBWCOOP_C76_LOG=1: card 076 mech fire log
bool read_c132_fx();
bool read_c136_log(); // UBWCOOP_C136_LOG=1: card 136 host effect event and skip lines
bool read_god_host_only(); // UBWCOOP_GOD_HOST_ONLY=1: card 132 god for the host's own pawn only // UBWCOOP_C132_FX=1: card 132 step 2e effect audit
bool read_c79_log(); // UBWCOOP_C79_LOG=1: card 079 mech destruction log
bool read_c79_kill(); // UBWCOOP_C79_KILL=1: card 079 test kill of a piloted mech (host)
int read_c79_down();  // UBWCOOP_C79_DOWN: 0 off, 1 "host" (Downed, then Out), 2 "client" (Downed), 3 "zipline"
                      // (the client, Downed, at a zipline point)
int32_t read_c66_cap(); // UBWCOOP_C66_CAP=32-512: card 066 objective journal test cap (default 512)
bool read_diag();    // UBWCOOP_DIAG=1: card 047 diagnostic hooks installed
bool read_c60_host_own();
// Card 063: "1" = each game polls UBWCoop-<tag>.cmd next to its log every 100 ms for appended commands (key, axis,
// shot, quit) and rewrites UBWCoop-<tag>.state every 250 ms; unset or anything else = off, no file touched.
bool read_cmd();
// UBWCOOP_C52_TEST: card 052 lobby panel test driver. "host" = turn host mode on through the panel, then start the
// first campaign mission through the game's own TransitionToLevel; "client" = type 127.0.0.1 into the panel and join;
// "fail" = the same join with no host running; "solo" = host mode off, then the same mission start; "play" / "join" =
// the host start / client join only, then the driver stops in the first mission (card 319 played-path scenarios).
// Off by default.
enum class C52Test : uint8_t { Off, Host, Client, Fail, Solo, Play, Join };
C52Test read_c52_test();

// UBWCOOP_KILL_PLAYER: card 030 death audit. Set on both machines: both log the death flow; only the host
// damages the named player's pawn (its own pawn, or the client's server copy) until it dies. Off by default.
enum class KillPlayer : uint8_t { Off, Host, Client };
KillPlayer read_kill_player();

// UBWCOOP_OVERLAY_TEST: card 031 lobby overlay render test, single player only. "1" = F8 toggles the
// overlay; "script" = the same plus a timed SendInput script (F8, typing, Enter, shots, a main menu ->
// mission -> main menu round trip). Off by default.
enum class OverlayTest : uint8_t { Off, Manual, Script };
OverlayTest read_overlay_test();

// UBWCOOP_SPAWN_MECH: card 032 mech audit. Set on both machines: both log the mech actors, the mech enter and
// leave detours and the mech-related UFunctions; the client tries an interact on the mech. Only the host spawns:
// "pickup" = a MechSupport tactical support pickup it collects and throws, "mech" = BP_BaseMech directly,
// "pad" = the nearest placed BP_MechDropPlatform (or a spawned one when none is loaded), then activates it.
// Off by default.
enum class SpawnMech : uint8_t { Off, Pickup, Mech, Pad };
SpawnMech read_spawn_mech();

struct AutoInput {
    uint32_t delay_ms; // UBWCOOP_AUTOINPUT_DELAY_MS, clamped to kMaxDelayMs
    bool enabled;      // UBWCOOP_AUTOINPUT or UBWCOOP_HOST_SPRINT is exactly "1"
    bool host_sprint;  // UBWCOOP_HOST_SPRINT is exactly "1": the parity script is armed on the host after landing
};

struct GrantWeapon {
    char row[kMaxWeaponLen + 1]; // NUL-terminated ASCII row name; empty when off
};

// Reads and validates UBWCOOP_GRANT_WEAPON. Logs the result.
GrantWeapon read_grant_weapon();

// UBWCOOP_C49_TEST=<objective id> (1-32 chars of [A-Za-z0-9_-]): card 049 objectives test, both sides: panel
// screenshots after the map load and after a completion; the host completes this objective once in card 045's
// restart phase 1. Off by default.
struct C49Test {
    wchar_t id[kMaxWeaponLen + 1]; // NUL-terminated; empty when off
    bool enabled;
};
C49Test read_c49_test();

// Reads UBWCOOP_AUTOINPUT, UBWCOOP_HOST_SPRINT and UBWCOOP_AUTOINPUT_DELAY_MS. Logs the result.
AutoInput read_autoinput();

} // namespace ubw::config
