#include "ubw/flags.h"

#include "ubw/hooks_internal.h"
#include "ue/hooktable.h"
#include "ue/log.h"

#include <windows.h>
#include <wchar.h>

namespace ubw::config {
namespace log = ue::log;
using ue::config::EnvRead;
using ue::config::EnvStatus;
using ue::config::is_tag_char;
using ue::config::parse_u32;
using ue::config::read_delay_ms;
using ue::config::read_env;
using ue::config::read_switch;

bool read_client_game_only() {
    wchar_t buf[4];
    const EnvRead r = read_env(L"UBWCOOP_CLIENT_GAMEONLY", buf, 4);
    if (r.status == EnvStatus::Unset) {
        log::line("config: UBWCOOP_CLIENT_GAMEONLY unset -- client input mode left to the game");
        return false;
    }
    if (r.status == EnvStatus::Ok && r.len == 1 && buf[0] == L'1') {
        log::line("config: UBWCOOP_CLIENT_GAMEONLY=1 -- client local PC gets FInputModeGameOnly once per map load");
        return true;
    }
    log::line("WARN config: UBWCOOP_CLIENT_GAMEONLY not \"1\" (status=%d, err=%lu) -- off", static_cast<int>(r.status),
              r.error);
    return false;
}

bool read_auto_dropship() {
    wchar_t buf[4];
    const EnvRead r = read_env(L"UBWCOOP_AUTODROPSHIP", buf, 4);
    if (r.status == EnvStatus::Unset) {
        log::line("config: UBWCOOP_AUTODROPSHIP unset -- dropship left to the game");
        return false;
    }
    if (r.status == EnvStatus::Ok && r.len == 1 && buf[0] == L'1') {
        log::line("config: UBWCOOP_AUTODROPSHIP=1 -- host lands its transport dropship once per map");
        return true;
    }
    log::line("WARN config: UBWCOOP_AUTODROPSHIP not \"1\" (status=%d, err=%lu) -- off", static_cast<int>(r.status),
              r.error);
    return false;
}

bool read_skip_flight(bool auto_dropship) {
    wchar_t buf[4];
    const EnvRead r = read_env(L"UBWCOOP_SKIP_FLIGHT", buf, 4);
    if (r.status == EnvStatus::Unset) {
        log::line("config: UBWCOOP_SKIP_FLIGHT unset -- transport dropship flies its spline");
        return false;
    }
    if (r.status != EnvStatus::Ok || r.len != 1 || buf[0] != L'1') {
        log::line("WARN config: UBWCOOP_SKIP_FLIGHT not \"1\" (status=%d, err=%lu) -- off", static_cast<int>(r.status),
                  r.error);
        return false;
    }
    if (!auto_dropship) {
        log::line("WARN config: UBWCOOP_SKIP_FLIGHT=1 needs UBWCOOP_AUTODROPSHIP=1 -- off");
        return false;
    }
    log::line("config: UBWCOOP_SKIP_FLIGHT=1 -- host calls SkipFlight(dropship, true) once after the autodropship unpause");
    return true;
}

bool read_kill_trooper() {
    const bool on = read_switch(L"UBWCOOP_KILL_TROOPER", "card 025 trooper kill");
    if (on) {
        log::line("config: UBWCOOP_KILL_TROOPER=1 -- card 025 NPC list and one test kill of the nearest NPC trooper");
    }
    return on;
}

bool read_audit_029() {
    const bool on = read_switch(L"UBWCOOP_AUDIT_029", "card 029 weapon, mech and turret audit");
    if (on) {
        log::line("config: UBWCOOP_AUDIT_029=1 -- card 029 weapon cycle, weapon actions, vehicle scan and test enter");
    }
    return on;
}

// Card 135 (replaces card 130's always-on skip): off unless exactly "1". Read once, from the worker, before hooks.
namespace {
bool g_test_skip = false;
} // namespace

bool read_test_skip() {
    g_test_skip = read_switch(L"UBWCOOP_TEST_SKIP", "card 135 test cinematic skip");
    log::line("config: UBWCOOP_TEST_SKIP %s -- %s", g_test_skip ? "1" : "off",
              g_test_skip ? "tests skip startup movies, briefs, cutscenes and level sequences by the skip path, and "
                            "close the brief's loading screen"
                          : "play build: no cinematic is skipped automatically");
    return g_test_skip;
}

bool test_skip() {
    return g_test_skip;
}

// Card 135: UBWCOOP_C135_KEEP_SCREEN=1 keeps the press-to-start screen up after the test-skipped brief.
namespace {
bool g_c135_keep_screen = false;
} // namespace

bool read_c135_keep_screen(bool auto_dropship) {
    g_c135_keep_screen = read_switch(L"UBWCOOP_C135_KEEP_SCREEN", "card 135 keep the press-to-start screen");
    log::line("config: UBWCOOP_C135_KEEP_SCREEN %s -- %s", g_c135_keep_screen ? "1" : "off",
              g_c135_keep_screen ? "the test skip leaves the press-to-start screen up after the brief"
                                 : "the test skip closes the press-to-start screen after the brief");
    if (g_c135_keep_screen && auto_dropship) {
        log::line("WARN config: UBWCOOP_C135_KEEP_SCREEN=1 has no effect with UBWCOOP_AUTODROPSHIP=1 (the autodropship "
                  "close still closes the screen)");
    }
    return g_c135_keep_screen;
}

bool c135_keep_screen() {
    return g_c135_keep_screen;
}

bool read_c28_shots() {
    const bool on = read_switch(L"UBWCOOP_C28_SHOTS", "card 028 remote body screenshots");
    if (on) {
        log::line("config: UBWCOOP_C28_SHOTS=1 -- card 028 screenshots of the remote player body");
    }
    return on;
}

bool read_c92_log() {
    const bool on = read_switch(L"UBWCOOP_C92_LOG", "card 092 host body bounce log");
    if (on) {
        log::line("config: UBWCOOP_C92_LOG=1 -- card 092 client per-frame camera and host sprite Z log");
    }
    return on;
}

bool read_c94_log() {
    const bool on = read_switch(L"UBWCOOP_C94_LOG", "card 094 jump and mantle log");
    if (on) {
        log::line("config: UBWCOOP_C94_LOG=1 -- card 094 jump and mantle ability log (both sides)");
    }
    return on;
}

bool read_c125_log() {
    const bool on = read_switch(L"UBWCOOP_C125_LOG", "card 125 turret input and state log");
    if (on) {
        log::line("config: UBWCOOP_C125_LOG=1 -- card 125 turret tag edges, look counts and 1 Hz turret state");
    }
    return on;
}

bool read_c122_log() {
    const bool on = read_switch(L"UBWCOOP_C122_LOG", "card 122 attack pacing log");
    if (on) {
        log::line("config: UBWCOOP_C122_LOG=1 -- card 122 attack pacing per-window log (host)");
    }
    return on;
}

bool read_c103_log() {
    const bool on = read_switch(L"UBWCOOP_C103_LOG", "card 103 enemy target log");
    if (on) {
        log::line("config: UBWCOOP_C103_LOG=1 -- card 103 enemy target selection and enemy-context player 0 log (host)");
    }
    return on;
}

bool read_enemy_audit() {
    const bool on = read_switch(L"UBWCOOP_ENEMYAUDIT", "card 023 enemy replication audit");
    if (on) {
        log::line("config: UBWCOOP_ENEMYAUDIT=1 -- card 023 enemy audit (rows every 5 s, events every 1 s, screenshots)");
    }
    return on;
}

bool read_spawn_bug() {
    const bool on = read_switch(L"UBWCOOP_SPAWN_BUG", "card 023 debug bug spawn and test kill");
    if (on) {
        log::line("config: UBWCOOP_SPAWN_BUG=1 -- card 023 host spawns one BP_AI_WarriorBug per map, then kills it");
    }
    return on;
}

bool read_no_enemy_health() {
    const bool on = read_switch(L"UBWCOOP_NO_ENEMY_HEALTH", "card 037 diagnosis-only mode");
    if (on) {
        log::line("config: UBWCOOP_NO_ENEMY_HEALTH=1 -- card 037 client enemy health adoption and notify off");
    }
    return on;
}

// UBWCOOP_GOD_HOST_ONLY=1 (card 132 hb53 note 3): god for the host's own pawn only; the client's pawn takes damage
// and each of its Health changes is logged on both sides (set it on both). Implies UBWCOOP_GOD on the host.
bool read_god_host_only() {
    const bool on = read_switch(L"UBWCOOP_GOD_HOST_ONLY", "card 132 host-only god mode");
    if (on) {
        log::line("config: UBWCOOP_GOD_HOST_ONLY=1 -- card 132 god mode for the host's own pawn only; the client's "
                  "pawn takes damage, its Health changes logged on both sides");
    }
    return on;
}

bool read_god() {
    const bool on = read_switch(L"UBWCOOP_GOD", "card 132 test god mode");
    if (on) {
        log::line("config: UBWCOOP_GOD=1 -- card 132 test god mode: host, no player pawn loses Health or goes down; "
                  "client, its own pawn's local death is skipped");
    }
    return on;
}

int32_t read_decal_cap() {
    constexpr uint32_t kMin = 8;
    constexpr uint32_t kMax = 500; // the game's cap
    wchar_t buf[16];
    const EnvRead r = read_env(L"UBWCOOP_DECAL_CAP", buf, 16);
    if (r.status == EnvStatus::Unset) {
        return 0;
    }
    uint32_t v = 0;
    if (r.status != EnvStatus::Ok || !parse_u32(buf, r.len, &v) || v < kMin || v > kMax) {
        log::line("WARN config: UBWCOOP_DECAL_CAP unparsable or outside %u-%u (status=%d) -- the game's cap is kept",
                  kMin, kMax, static_cast<int>(r.status));
        return 0;
    }
    log::line("config: UBWCOOP_DECAL_CAP=%u -- card 132 test decal pool cap", v);
    return static_cast<int32_t>(v);
}

bool read_no_client_gore() {
    const bool on = read_switch(L"UBWCOOP_NO_CLIENT_GORE", "card 039 diagnosis-only mode");
    if (on) {
        log::line("config: UBWCOOP_NO_CLIENT_GORE=1 -- card 039 client gib and corpse box calls off");
    }
    return on;
}

bool read_c39_overkill() {
    const bool on = read_switch(L"UBWCOOP_C39_OVERKILL", "card 039 overkill test kill");
    if (on) {
        log::line("config: UBWCOOP_C39_OVERKILL=1 -- card 023's test kill uses InstaKillTarget(bIsOverkill = true)");
    }
    return on;
}

bool read_c42_test() {
    const bool on = read_switch(L"UBWCOOP_C42_TEST", "card 042 corpse walk and gib test");
    if (on) {
        log::line("config: UBWCOOP_C42_TEST=1 -- card 042 corpse walk test and two close overkills (host drives)");
    }
    return on;
}

bool read_c43_test() {
    const bool on = read_switch(L"UBWCOOP_C43_TEST", "card 043 downed and revive test");
    if (on) {
        log::line("config: UBWCOOP_C43_TEST=1 -- card 043 downed and revive test (host downs both players, they revive)");
    }
    return on;
}

// Card 138 test: the client holds the world BeginPlay dispatch at least this long, pawn or not (the GameState-first
// order, forced). 0 (unset) = only while the pawn is missing.
uint32_t read_c138_hold_ms() {
    constexpr uint32_t kMax = 30000;
    wchar_t buf[16];
    const EnvRead r = read_env(L"UBWCOOP_C138_HOLD_MS", buf, 16);
    if (r.status == EnvStatus::Unset) {
        return 0;
    }
    uint32_t v = 0;
    if (r.status != EnvStatus::Ok || !parse_u32(buf, r.len, &v) || v > kMax) {
        log::line("WARN config: UBWCOOP_C138_HOLD_MS unparsable or above %u (status=%d) -- using 0", kMax,
                  static_cast<int>(r.status));
        return 0;
    }
    log::line("config: UBWCOOP_C138_HOLD_MS=%u -- card 138 test: the client BeginPlay dispatch is held at least this long", v);
    return v;
}

uint32_t read_c43_start_ms() {
    constexpr uint32_t kDefault = 20000; // after both pawns are on foot (self-hits land nothing ~22 s)
    constexpr uint32_t kMax = 600000;
    wchar_t buf[16];
    const EnvRead r = read_env(L"UBWCOOP_C43_START_MS", buf, 16);
    if (r.status == EnvStatus::Unset) {
        return kDefault;
    }
    uint32_t v = 0;
    if (r.status != EnvStatus::Ok || !parse_u32(buf, r.len, &v) || v < kDefault || v > kMax) {
        log::line("WARN config: UBWCOOP_C43_START_MS unparsable or outside %u-%u (status=%d) -- using %u", kDefault,
                  kMax, static_cast<int>(r.status), kDefault);
        return kDefault;
    }
    log::line("config: UBWCOOP_C43_START_MS=%u -- card 043 test's first hit this long after both are on foot", v);
    return v;
}

bool read_c45_test() {
    const bool on = read_switch(L"UBWCOOP_C45_TEST", "card 045 co-op restart test");
    if (on) {
        log::line("config: UBWCOOP_C45_TEST=1 -- card 045 co-op restart test (with UBWCOOP_C43_TEST: restart, "
                  "reconnect, revive, checkpoint restart)");
    }
    return on;
}

// Card 050: the weapon sync test driver.
int read_c50_test() {
    wchar_t buf[4];
    const EnvRead r = read_env(L"UBWCOOP_C50_TEST", buf, 4);
    if (r.status == EnvStatus::Unset) {
        log::line("config: UBWCOOP_C50_TEST unset -- card 050 and 054 tests off");
        return 0;
    }
    if (r.status == EnvStatus::Ok && r.len == 1 && buf[0] == L'1') {
        log::line("config: UBWCOOP_C50_TEST=1 -- card 050 weapon sync test (both players take a placed weapon pickup)");
        return 1;
    }
    if (r.status == EnvStatus::Ok && r.len == 3 && wcscmp(buf, L"054") == 0) {
        log::line("config: UBWCOOP_C50_TEST=054 -- card 054 interaction relay test (switch, satchel, site, zipline)");
        return 54;
    }
    log::line("WARN config: UBWCOOP_C50_TEST not \"1\" or \"054\" (status=%d, err=%lu) -- card 050 and 054 tests off",
              static_cast<int>(r.status), r.error);
    return 0;
}

// Card 057: the solo weapon switch test (with UBWCOOP_C50_TEST, host without a client).
bool read_c57_solo() {
    const bool on = read_switch(L"UBWCOOP_C57_SOLO", "card 057 solo weapon switch test");
    if (on) {
        log::line("config: UBWCOOP_C57_SOLO=1 -- card 057 solo test (the host's own pawn takes a placed weapon pickup)");
    }
    return on;
}

// Card 065 step 1: log the level triggers (range targets) on each machine; the detours are installed only then.
bool read_c65_log() {
    const bool on = read_switch(L"UBWCOOP_C65_LOG", "card 065 level-trigger log");
    if (on) {
        log::line("config: UBWCOOP_C65_LOG=1 -- card 065 level-trigger log (Reactions/StatefulReactions Trigger, "
                  "execTrigger, RangeTarget Activate)");
    }
    return on;
}

// Card 068: the client minimap chain log (ObjectiveUpdated, ShowOnMinimap, the widget's markers, a 1 s line).
bool read_c69_log() {
    const bool on = read_switch(L"UBWCOOP_C69_LOG", "card 069 ammo log");
    if (on) {
        log::line("config: UBWCOOP_C69_LOG=1 -- card 069 ammo log (ResetInventory, AddAmmo, SetAmmoCount, "
                  "RequestAmmo, PerformReload, DoAction, 1 s ammo line)");
    }
    return on;
}

// Card 099 step 1: who plays 2D sounds, located FName sounds, camera shakes and score popups, and for which pawn.
bool read_c99_log() {
    const bool on = read_switch(L"UBWCOOP_C99_LOG", "card 099 cue log");
    if (on) {
        log::line("config: UBWCOOP_C99_LOG=1 -- card 099 cue log (PlaySound2D, SpawnSound2D, ADAudioManager "
                  "PlaySound2D, PlaySoundAtLocationFNameWithAIAwareness, StartCameraShake, AddFedScore)");
    }
    return on;
}

// Card 074 step 1: where each side's grenade throw goes (animation, OnDoAction, FireProjectile task, pooler spawn,
// OnGrenadeSpawned, InitGrenade, Explode and its damage calls).
bool read_c74_log() {
    const bool on = read_switch(L"UBWCOOP_C74_LOG", "card 074 grenade spawn log");
    if (on) {
        log::line("config: UBWCOOP_C74_LOG=1 -- card 074 grenade spawn log (StartAnimations, StartThrowingAnimation, "
                  "OnDoAction, FireProjectile Activate, SpawnProjectile, InitGrenade, Explode and its damage calls)");
    }
    return on;
}

// Card 076 step 1: where each side's mech fire goes (mech input tags, ability activations, FireProjectile, pooler
// spawns, impact damage, Niagara systems while a mech is piloted, and the health of NPCs near a piloted mech).
bool read_c76_log() {
    const bool on = read_switch(L"UBWCOOP_C76_LOG", "card 076 mech fire log");
    if (on) {
        log::line("config: UBWCOOP_C76_LOG=1 -- card 076 mech fire log (mech input tags, activations, FireProjectile, "
                  "SpawnProjectile, impact damage, Niagara systems, NPC health near a piloted mech)");
    }
    return on;
}

// Card 132 step 2e: every effect spawn on this side (Niagara and Cascade systems, sounds, damage zones), capped per
// asset, for the host-against-client pairing (tools/fx_pair.py).
bool read_c132_fx() {
    const bool on = read_switch(L"UBWCOOP_C132_FX", "card 132 effect audit");
    if (on) {
        log::line("config: UBWCOOP_C132_FX=1 -- card 132 effect audit (every effect spawn chokepoint, both sides)");
    }
    return on;
}

// Card 136: the host's capture event and skip lines (tools/fx_pair.py pairs them). Off in the play build; the per-map
// totals line is logged either way.
bool read_c136_log() {
    const bool on = read_switch(L"UBWCOOP_C136_LOG", "card 136 effect event log");
    if (on) {
        log::line("config: UBWCOOP_C136_LOG=1 -- card 136 effect event log (every captured host effect, inner skips)");
    }
    return on;
}

// Card 079 step 1: what the game does when a mech is destroyed (the mech's StartDeath, self-destruct and
// DeathComplete, and which pawn each PlayerController holds afterwards).
bool read_c79_log() {
    const bool on = read_switch(L"UBWCOOP_C79_LOG", "card 079 mech destruction log");
    if (on) {
        log::line("config: UBWCOOP_C79_LOG=1 -- card 079 mech destruction log (mech StartDeath, StartSelfDestruction, "
                  "self-destruct sequence, DeathComplete, PlayerController pawn changes)");
    }
    return on;
}

// Card 079 test: the host deals lethal hits to a piloted mech 8 s after its EnterMech.
bool read_c79_kill() {
    const bool on = read_switch(L"UBWCOOP_C79_KILL", "card 079 test mech kill");
    if (on) {
        log::line("config: UBWCOOP_C79_KILL=1 -- card 079 test: a piloted mech takes lethal hits 8 s after its "
                  "EnterMech (host)");
    }
    return on;
}

// Card 079 test (host): the first time that player's pawn is near a mech, card 043 downs it, so the next E at
// the mech is refused. "host" also cuts the bleedout short, so the host's pawn goes Out. "zipline" (card 081) downs
// the client's pawn next to a zipline point instead (FED_C1M1), so its next E there is refused.
int read_c79_down() {
    wchar_t buf[8];
    const EnvRead r = read_env(L"UBWCOOP_C79_DOWN", buf, 8);
    if (r.status == EnvStatus::Unset) {
        log::line("config: UBWCOOP_C79_DOWN unset -- card 079 down test off");
        return 0;
    }
    const bool ok = r.status == EnvStatus::Ok;
    const int mode = ok && wcscmp(buf, L"host") == 0     ? 1
                     : ok && wcscmp(buf, L"client") == 0  ? 2
                     : ok && wcscmp(buf, L"zipline") == 0 ? 3
                                                          : 0;
    if (mode == 0) {
        log::line("WARN config: UBWCOOP_C79_DOWN not \"host\", \"client\" or \"zipline\" (status=%d, err=%lu) -- card "
                  "079 down test off", static_cast<int>(r.status), r.error);
        return 0;
    }
    log::line("config: UBWCOOP_C79_DOWN=%ls -- card 079 test: that player goes down next to a %s (host)", buf,
              mode == 3 ? "zipline point" : "mech");
    return mode;
}

// Card 047: the finished movement, input, jump, sprint, grant and audit diagnostics (cards 010-024, 038, 048-050,
// 057). Off, their hooks are not installed at all.
bool read_diag() {
    const bool on = read_switch(L"UBWCOOP_DIAG", "card 047 diagnostic hooks");
    if (on) {
        log::line("config: UBWCOOP_DIAG=1 -- card 047 diagnostic hooks installed (movement, input, jump, sprint, grant, "
                  "card 038 and 048-050 and 057 audit logs)");
    }
    return on;
}

// Card 066: the objective journal's test cap (frames). Unset: 512, the array size (card 308).
int32_t read_c66_cap() {
    constexpr uint32_t kDefault = 512;
    constexpr uint32_t kMin = 32;
    wchar_t buf[16];
    const EnvRead r = read_env(L"UBWCOOP_C66_CAP", buf, 16);
    if (r.status == EnvStatus::Unset) {
        return static_cast<int32_t>(kDefault);
    }
    uint32_t v = 0;
    if (r.status != EnvStatus::Ok || !parse_u32(buf, r.len, &v) || v < kMin || v > kDefault) {
        log::line("WARN config: UBWCOOP_C66_CAP unparsable or outside %u-%u (status=%d) -- using %u", kMin, kDefault,
                  static_cast<int>(r.status), kDefault);
        return static_cast<int32_t>(kDefault);
    }
    log::line("config: UBWCOOP_C66_CAP=%u -- card 066 objective journal test cap", v);
    return static_cast<int32_t>(v);
}

// Card 073: the grenade item inventory and grenade ability log (AddAbilityToActor, item add/remove, ConsumeItem, 1 s line).
bool read_c73_log() {
    const bool on = read_switch(L"UBWCOOP_C73_LOG", "card 073 grenade log");
    if (on) {
        log::line("config: UBWCOOP_C73_LOG=1 -- card 073 grenade log (AddAbilityToActor, AddInventoryItem_Internal, "
                  "RemoveItemByDefinition, ConsumeItem, 1 s item and grenade ability line)");
    }
    return on;
}

bool read_c68_log() {
    const bool on = read_switch(L"UBWCOOP_C68_LOG", "card 068 minimap log");
    if (on) {
        log::line("config: UBWCOOP_C68_LOG=1 -- card 068 minimap log (ObjectiveUpdated, ShowOnMinimap, "
                  "AddNewMarker/RemoveMarker, NativeOnInitialized, 1 s minimap line)");
    }
    return on;
}

// Card 063: the test runner's command and state files.
bool read_cmd() {
    const bool on = read_switch(L"UBWCOOP_CMD", "card 063 command and state files");
    if (on) {
        log::line("config: UBWCOOP_CMD=1 -- card 063 test runner: UBWCoop-<tag>.cmd polled, UBWCoop-<tag>.state written");
    }
    return on;
}

// Card 060: the host's own weapon switch under ServerOnly (with UBWCOOP_C50_TEST and a client).
bool read_c60_host_own() {
    const bool on = read_switch(L"UBWCOOP_C60_HOST_OWN", "card 060 host-own weapon switch test");
    if (on) {
        log::line("config: UBWCOOP_C60_HOST_OWN=1 -- card 060 test (after the client's switch, the host's own pawn "
                  "takes a placed weapon pickup and presses 2)");
    }
    return on;
}

C52Test read_c52_test() {
    struct Row {
        const wchar_t* text;
        C52Test test;
    };
    static constexpr Row kRows[] = {{L"host", C52Test::Host}, {L"client", C52Test::Client}, {L"fail", C52Test::Fail},
                                    {L"solo", C52Test::Solo}, {L"play", C52Test::Play}, {L"join", C52Test::Join}};
    wchar_t buf[8];
    const EnvRead r = read_env(L"UBWCOOP_C52_TEST", buf, 8);
    if (r.status == EnvStatus::Unset) {
        return C52Test::Off;
    }
    for (const Row& row : kRows) {
        if (r.status == EnvStatus::Ok && wcscmp(buf, row.text) == 0) {
            log::line("config: UBWCOOP_C52_TEST=%ls -- card 052 lobby panel test driver", row.text);
            return row.test;
        }
    }
    log::line("WARN config: UBWCOOP_C52_TEST not host, client, fail, solo, play or join (status=%d, err=%lu) -- card 052 test off",
              static_cast<int>(r.status), r.error);
    return C52Test::Off;
}

bool read_c46_test() {
    const bool on = read_switch(L"UBWCOOP_C46_TEST", "card 046 kill floor test");
    if (on) {
        log::line("config: UBWCOOP_C46_TEST=1 -- card 046 kill floor test (copy, host, low-Health copy, all-down drops)");
    }
    return on;
}

bool read_c40_damage_trace() {
    const bool on = read_switch(L"UBWCOOP_C40_DAMAGE_TRACE", "card 040 damage pipeline trace");
    if (on) {
        log::line("config: UBWCOOP_C40_DAMAGE_TRACE=1 -- card 040 host traces each enemy damage hit (log only), "
                  "host and client hold fire on each card 023 bug, more bugs until both have enough traced shots");
    }
    return on;
}

bool read_c41_anim_trace() {
    const bool on = read_switch(L"UBWCOOP_C41_ANIM_TRACE", "card 041 NPC animation trace");
    if (on) {
        log::line("config: UBWCOOP_C41_ANIM_TRACE=1 -- card 041 logs NPC animation sequence plays, delegate "
                  "listeners and callbacks on this side (log only)");
    }
    return on;
}

KillPlayer read_kill_player() {
    wchar_t buf[8];
    const EnvRead r = read_env(L"UBWCOOP_KILL_PLAYER", buf, 8);
    if (r.status == EnvStatus::Unset) {
        log::line("config: UBWCOOP_KILL_PLAYER unset -- card 030 player death audit off");
        return KillPlayer::Off;
    }
    if (r.status == EnvStatus::Ok && wcscmp(buf, L"host") == 0) {
        log::line("config: UBWCOOP_KILL_PLAYER=host -- card 030 death audit; the host kills its own pawn");
        return KillPlayer::Host;
    }
    if (r.status == EnvStatus::Ok && wcscmp(buf, L"client") == 0) {
        log::line("config: UBWCOOP_KILL_PLAYER=client -- card 030 death audit; the host kills the client's pawn");
        return KillPlayer::Client;
    }
    log::line("WARN config: UBWCOOP_KILL_PLAYER not \"host\" or \"client\" (status=%d, err=%lu) -- card 030 off",
              static_cast<int>(r.status), r.error);
    return KillPlayer::Off;
}

OverlayTest read_overlay_test() {
    wchar_t buf[8];
    const EnvRead r = read_env(L"UBWCOOP_OVERLAY_TEST", buf, 8);
    if (r.status == EnvStatus::Unset) {
        log::line("config: UBWCOOP_OVERLAY_TEST unset -- card 031 overlay render test off");
        return OverlayTest::Off;
    }
    if (r.status == EnvStatus::Ok && wcscmp(buf, L"1") == 0) {
        log::line("config: UBWCOOP_OVERLAY_TEST=1 -- card 031 overlay render test; F8 toggles it (single player)");
        return OverlayTest::Manual;
    }
    if (r.status == EnvStatus::Ok && wcscmp(buf, L"script") == 0) {
        log::line("config: UBWCOOP_OVERLAY_TEST=script -- card 031 overlay render test driven by a timed input script");
        return OverlayTest::Script;
    }
    log::line("WARN config: UBWCOOP_OVERLAY_TEST not \"1\" or \"script\" (status=%d, err=%lu) -- card 031 off",
              static_cast<int>(r.status), r.error);
    return OverlayTest::Off;
}

SpawnMech read_spawn_mech() {
    struct Row {
        const wchar_t* text;
        SpawnMech mode;
        const char* what;
    };
    static const Row kRows[] = {
        {L"pickup", SpawnMech::Pickup, "the host collects and throws a spawned MechSupport tactical support pickup"},
        {L"mech", SpawnMech::Mech, "the host spawns BP_BaseMech directly"},
        {L"pad", SpawnMech::Pad, "the host activates a placed (or spawned) BP_MechDropPlatform"},
    };
    wchar_t buf[8];
    const EnvRead r = read_env(L"UBWCOOP_SPAWN_MECH", buf, 8);
    if (r.status == EnvStatus::Unset) {
        return SpawnMech::Off;
    }
    for (const Row& row : kRows) {
        if (r.status == EnvStatus::Ok && wcscmp(buf, row.text) == 0) {
            log::line("config: UBWCOOP_SPAWN_MECH=%ls -- card 032 mech audit; %s", row.text, row.what);
            return row.mode;
        }
    }
    log::line("WARN config: UBWCOOP_SPAWN_MECH not \"pickup\", \"mech\" or \"pad\" (status=%d, err=%lu) -- card 032 off",
              static_cast<int>(r.status), r.error);
    return SpawnMech::Off;
}

bool read_dump_sprite_anims() {
    const bool on = read_switch(L"UBWCOOP_DUMP_SPRITE_ANIMS", "card 027 sprite animation dump");
    if (on) {
        log::line("config: UBWCOOP_DUMP_SPRITE_ANIMS=1 -- card 027 dump of the Morita trooper sprite animation assets");
    }
    return on;
}

GrantWeapon read_grant_weapon() {
    GrantWeapon g{};
    wchar_t buf[kMaxWeaponLen + 1];
    const EnvRead r = read_env(L"UBWCOOP_GRANT_WEAPON", buf, kMaxWeaponLen + 1);
    if (r.status == EnvStatus::Unset) {
        return g;
    }
    bool valid = r.status == EnvStatus::Ok && r.len > 0;
    for (DWORD i = 0; valid && i < r.len; ++i) {
        valid = is_tag_char(buf[i]);
    }
    if (!valid) {
        log::line("WARN config: UBWCOOP_GRANT_WEAPON invalid (status=%d, err=%lu; want 1-%zu chars of "
                  "[A-Za-z0-9_-]) -- off",
                  static_cast<int>(r.status), r.error, kMaxWeaponLen);
        return g;
    }
    for (DWORD i = 0; i < r.len; ++i) {
        g.row[i] = static_cast<char>(buf[i]);
    }
    g.row[r.len] = '\0';
    log::line("config: UBWCOOP_GRANT_WEAPON=%s -- card 021 test loadout for player pawns that lack it", g.row);
    return g;
}

C49Test read_c49_test() {
    C49Test t{};
    wchar_t buf[kMaxWeaponLen + 1];
    const EnvRead r = read_env(L"UBWCOOP_C49_TEST", buf, kMaxWeaponLen + 1);
    if (r.status == EnvStatus::Unset) {
        return t;
    }
    bool valid = r.status == EnvStatus::Ok && r.len > 0;
    for (DWORD i = 0; valid && i < r.len; ++i) {
        valid = is_tag_char(buf[i]);
    }
    if (!valid) {
        log::line("WARN config: UBWCOOP_C49_TEST invalid (status=%d, err=%lu; want 1-%zu chars of [A-Za-z0-9_-]) -- off",
                  static_cast<int>(r.status), r.error, kMaxWeaponLen);
        return t;
    }
    for (DWORD i = 0; i < r.len; ++i) {
        t.id[i] = buf[i];
    }
    t.id[r.len] = L'\0';
    t.enabled = true;
    log::line("config: UBWCOOP_C49_TEST=%ls -- card 049 objectives test (shots; the host completes this objective)",
              t.id);
    return t;
}

AutoInput read_autoinput() {
    AutoInput a{};
    const bool script = read_switch(L"UBWCOOP_AUTOINPUT", "scripted input");
    a.host_sprint = read_switch(L"UBWCOOP_HOST_SPRINT", "host-armed parity script");
    if (!script && !a.host_sprint) {
        return a;
    }
    a.enabled = true;
    a.delay_ms = read_delay_ms(L"UBWCOOP_AUTOINPUT_DELAY_MS");
    if (a.host_sprint) {
        log::line("config: UBWCOOP_HOST_SPRINT=1 -- card 020 parity script on the host %u ms after its landed "
                  "character is possessed%s",
                  a.delay_ms, script ? " (UBWCOOP_AUTOINPUT=1 also set; it adds nothing)" : "");
        return a;
    }
    log::line("config: UBWCOOP_AUTOINPUT=1 -- scripted input %u ms after the local PC acknowledges its pawn",
              a.delay_ms);
    return a;
}

} // namespace ubw::config

namespace ubw::hooks {

// Card 047: the install gates (see g_gate_c40). Runs after every flag it reads is set, before the install loop.
void set_hook_gates() {
    g_gate_c40 = g_c40_trace;
    g_gate_c41 = g_c41_trace;
    g_gate_c23 = g_c23_audit;
    g_gate_c23_c25 = g_c23_audit || g_c25_kill;
    g_gate_c30 = g_c30_mode != config::KillPlayer::Off;
    g_gate_c32 = g_c32_mode != config::SpawnMech::Off;
    g_gate_c29_c32 = g_c29_audit || g_gate_c32;
    g_gate_diag = config::read_diag();
    g_gate_c62 = g_c50_test;
    g_gate_c62_diag = g_c50_test || g_gate_diag;
    g_gate_c191 = g_c63_on || g_gate_c62_diag; // card 191: the end tick rows run in every test run
    g_gate_c94 = g_c94_log;
    g_gate_c94_diag = g_c94_log || g_gate_diag;
}

// The card flags read straight from the environment (no ubw::init argument).
void read_card_flags() {
    g_skip_flight = config::read_skip_flight(g_auto_dropship);
    (void)config::read_c135_keep_screen(g_auto_dropship); // card 135
    g_c28_shots = config::read_c28_shots();
    g_c92_log = config::read_c92_log();
    g_c94_log = config::read_c94_log();
    g_c23_audit = config::read_enemy_audit();
    g_c23_spawn = config::read_spawn_bug();
    g_c37_off = config::read_no_enemy_health();
    g_c132_god = config::read_god();
    g_c132_god_host_only = config::read_god_host_only();
    g_c132_god = g_c132_god || g_c132_god_host_only;
    g_c132_decal_cap = config::read_decal_cap();
    g_c39_off = config::read_no_client_gore();
    g_c39_overkill = config::read_c39_overkill();
    g_c40_trace = config::read_c40_damage_trace();
    g_c41_trace = config::read_c41_anim_trace();
    g_c42_test = config::read_c42_test();
    g_c43_test = config::read_c43_test();
    g_c43_start_ms = g_c43_test ? config::read_c43_start_ms() : 20000;
    g_c45_test = config::read_c45_test();
    const config::C49Test c49 = config::read_c49_test();
    g_c49_test = c49.enabled;
    memcpy(g_c49_test_id, c49.id, sizeof(g_c49_test_id));
    g_c46_test = config::read_c46_test();
    const int c50_mode = config::read_c50_test();
    g_c50_test = c50_mode != 0;
    g_c54_test = c50_mode == 54;
    g_c57_solo = config::read_c57_solo();
    g_c65_log = config::read_c65_log();
    g_c68_log = config::read_c68_log();
    g_c69_log = config::read_c69_log();
    g_c73_log = config::read_c73_log();
    g_c74_log = config::read_c74_log();
    g_c99_log = config::read_c99_log();
    pe_gate_set(kPeGate_c99_log, g_c99_log); // card 573 b463: detour_process_event reads the mirror bit
    g_c138_hold_ms = config::read_c138_hold_ms();
    g_c103_log = config::read_c103_log();
    g_c122_log = config::read_c122_log();
    g_c125_log = config::read_c125_log();
    g_c76_log = config::read_c76_log();
    g_c132_fx_log = config::read_c132_fx();
    g_c535_on = !ue::config::read_switch(L"UBWCOOP_C535_OFF", "card 535 client NPC FindFloor skip off (A/B)");
    g_c569_gate = !ue::config::read_switch(L"UBWCOOP_C569_OFF", "card 569 grenade DoAction gate off (A/B)");
    g_c575_log = ue::config::read_switch(L"UBWCOOP_C575_LOG", "card 575 crowd agent registration rows");
    g_c572_on = ue::config::read_switch(L"UBWCOOP_C571", "card 572 detour body cost rows");
    g_c572_memo_on = !ue::config::read_switch(L"UBWCOOP_C572_MEMO_OFF", "card 572 GetPlayerController memo off (A/B)");
    g_gate_c461_c539 = g_gate_diag || perf_wants_c461_move(); // ubwperf cards 539 and 542 use the card 461 row (card 578)
    g_c136_log = config::read_c136_log();
    g_c79_log = config::read_c79_log();
    g_c79_kill = config::read_c79_kill();
    g_c79_down = config::read_c79_down();
    g_c60_host_own = config::read_c60_host_own();
    g_c63_on = config::read_cmd() && c63_init();
    g_c60_mk1_only = g_c57_solo; // card 062 solo: a placed MoritaMk1 (the case laser has no Use target, card060)
    g_c32_mode = config::read_spawn_mech();
    g_c30_mode = config::read_kill_player();
    g_c31_mode = config::read_overlay_test();
    g_c52_test = config::read_c52_test();
    set_hook_gates();
}

} // namespace ubw::hooks
