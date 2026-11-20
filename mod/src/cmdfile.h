#pragma once

// Card 063: the test runner's files, UBWCoop-<tag>.cmd and UBWCoop-<tag>.state next to the exe (UBWCOOP_CMD=1).
//
// The runner (tools/drive.py) appends one command per line to the .cmd file; poll() returns the complete lines
// appended since the last poll, parsed. write_state() replaces the .state file through a temporary file and a
// rename, so a reader never sees half a line. This module does file I/O and parsing only: the ubw/ files run the
// commands and builds the state text. Game thread only (nothing here locks).

#include <stddef.h>
#include <stdint.h>

namespace ubw::cmdfile {

inline constexpr size_t kKeyChars = 32;  // FKey name, NUL included
inline constexpr size_t kNameChars = 41; // shot name, NUL included
inline constexpr size_t kLineChars = 128; // one command line, NUL included; longer lines are malformed
inline constexpr size_t kActorChars = 64; // card 064: placed actor or class name, NUL included
inline constexpr float kDefaultRadius = 150.0f; // card 064: `goto` without a radius

// Card 064 adds Goto (`goto <x> <y> <z> [radius]`, `goto actor <name> [radius]`) and Look (`look <yaw> <pitch>`,
// `look actor <name>`). An actor name is a placed actor's object name or, failing that, a class name (the nearest
// actor of that class is taken); ubw/testrun.cpp resolves it in the current world.
// Card 056 adds Disconnect (`disconnect`): the console command, so card 045's follow reconnects the client.
// Card 066 adds Objectives (`objectives`): one dump of the objective list panel and the objective map's counts.
// Card 075 adds Warp (`warp actor <name> [dist]`, `warp client <name> [dist]`): a test teleport of the local pawn (or,
// on the host, the client's server copy) to dist uu (default 300) short of the nearest such actor.
// Card 107 adds the Key chord (`key <name> <hold_ms> <name2> <at_ms>`): name2 is tapped (down, then up 100 ms
// later or at the hold's end) at_ms into name's hold, so both keys are down together (a run-and-jump).
// Card 095 adds Door (`door list`, `door open <name>`, `door close <name>`): list the doors and the blocker-like
// actors near the pawn, or call OpenDoor/CloseDoor on a placed door as the level script does (host).
// Card 514: ObjectiveGo's copy bit for `objective go <who> home` (the step's own stand, no go counted).
constexpr uint8_t kGoHome = 0x80;
enum class Op : uint8_t { Key, Axis, Shot, Quit, Goto, Look, Ui, Disconnect, Objectives, Warp, Door, Complete, Kill, Charge, Down, Travel, Crash, Spawn, Autoplay, Speed, Inventory, OffShot, God, WinKey, Clear, AmmoFill, ClearKill, ObjectiveNow, ObjectiveExpect, ObjectiveGo, ObjectiveWait, LevelCall, Discard, Stamina, GrenadeFill, ObjectiveNext }; // ObjectiveNext: `objective next` (b238, test only, host: the next doable step after the current one becomes current, sparse order with wrap; a satchel site no player can interact with is skipped) // GrenadeFill: `grenade fill <n>` (b234, test only, host: every player's frag grenade count raised to n, 1 to 20) // Stamina: `stamina on|off` (card 490, host: the infinite stamina option for this session, not saved) // Discard: `discard <n>` (card 281 b187, test only, host: the client copy's next n rounds become misses, their claims take the made-round path) // LevelCall: `levelcall <function name, spaces kept>` (b184, test only, host: a no-parameter function of a level script actor run by ProcessEvent) // ObjectiveNow/Expect/Go/Wait: `objective now`, `objective expect <id>`, `objective go [host|client|client2|all] [home]`, `objective wait <id> [seconds]` (card 284, host: the live active objective, the players placed there) // ClearKill: `clear <radius> kill` (user rule 2026-11-13, test only, host: InstaKillTarget on every live bug within radius, no fire) // AmmoFill: `ammo fill` (card 243, test only: the own soldier pawn's equipped weapon gets its starting ammunition, c21_grant_ammo) // Clear: `clear <radius>` (card 265: kill the live bugs within radius by weapon fire) // WinKey: `winkey <K> [hold_ms]` (card 222: WM_KEYDOWN/WM_KEYUP posted to the game window) // God: `god all|host` (card 132 hb53 note 3: host-only god, client pawn Health logged) // OffShot: `offshot` (card 132, host: side-on shots of a still sprite-drawn NPC) // Inventory: `inventory` (card 132, own pawn and the host's server copy) // Speed: `speed <x>` (card 130, host: world TimeDilation) // Autoplay: `autoplay on|off` (card 126) // Spawn: `spawn bug <x> <y> <z> <yaw>` (card 125, host) // Crash: `crash av` (card 118, test only) // Travel: `travel next|restart` (card 113) // Down: `down host|client` (card 109) // Charge: `charge <slot> <value> <ready>` (card 106) // Kill: `kill actor <name>` (card 098) // Complete: `objective complete <id>|all` (card 095) // Ui: `ui continue` (card 067), `ui click <prefix>` (card 121)

struct Cmd {
    uint32_t seq;            // 1-based line number in the .cmd file (malformed lines count too)
    Op op;
    uint32_t ms;             // Key: hold time; Axis: duration; Clear: the cap (card 265); ObjectiveWait: the wait, ObjectiveExpect: the poll, ObjectiveGo: the settle (284)
    float value;             // Axis: the per-tick amount
    wchar_t key[kKeyChars];  // Key, Axis: the FKey name
    wchar_t key2[kKeyChars]; // Key chord (card 107): a second FKey tapped while key is held; empty for a plain key
    uint32_t at_ms;          // Key chord: key2 goes down this many ms after key (inside the hold)
    char name[kNameChars];   // Shot: [A-Za-z0-9_-]
    char actor[kActorChars]; // Goto, Look: the actor or class name; empty for a point or an angle; Ui: the click
                             // function's name prefix (card 121), empty for `ui continue`
    double at[3];            // Goto: x y z (uu); Look: yaw pitch (degrees), or a point; Charge: slot value ready;
                             // Spawn: x y z, its yaw in radius
    float radius;            // Goto: done within this many uu; Warp: the distance short of the actor
    bool point;              // Look: at[] is an x y z point to face (card 065); Warp: at[] is the exact target (106)
    uint8_t copy;            // Warp: 0 the local pawn; 1 or 2 the host's first or second server copy of a client's
                             // pawn in PlayerController order (host only; 2 is `warp client2`, card 209);
                             // Charge: 1 the host's server copy of the client's pawn (card 324);
                             // ObjectiveGo (card 284): 0 host, 1 client, 2 client2, 3 all
    bool behind;             // Warp: radius uu behind the actor along its forward axis, not short of it (card 123)
    bool front;              // Warp: radius uu in front of the actor along its forward axis (card 264 p3: switch faces)
    bool sprint;             // Goto: LeftShift held for the walk (card 124)
    bool smooth;             // Look: turn through mouse axis events, not a control rotation write (card 124)
    bool bounds;             // Look actor: face the actor's bounds centre (AActor::GetActorBounds), not its origin (131)
    bool hole;               // Look actor (b232, user: "grenades ... need to be aimed at yellow center"): face the
                             // nearest owned grenade hole component's world location (a nest dies only to a blast there)
    bool live;               // Look/Goto/Warp actor (warp: b210): the nearest live one (Health over 0), corpses skipped (card 281, b190);
                             // a pickup taken (bEnabled 0) is skipped too (card 244 b208)
    char want[kActorChars];  // Look actor (b14): `focus=<prefix>`, scan the actor's bounds for an interactor focus
                             // whose name starts with it; empty for a plain look
    uint8_t door;            // Door: 0 list, 'O' open, 'C' close
    char text[kLineChars];   // the line as read, for the log
};

// Builds both paths for `tag` (null or empty: "UBWCoop.cmd") and creates or truncates the .cmd file, so commands
// left from an earlier run never execute. False (logged) on failure; poll() then returns 0.
bool open(const wchar_t* tag);

// Up to `cap` commands from the complete lines appended since the last call. A malformed line is logged as
// "WARN cmd:" and skipped (its seq is used up). Lines beyond `cap` stay unread for the next call.
int32_t poll(Cmd* out, int32_t cap);

// Replaces the .state file with `len` bytes of `text`. False (logged, capped) on failure.
bool write_state(const char* text, size_t len);

} // namespace ubw::cmdfile
