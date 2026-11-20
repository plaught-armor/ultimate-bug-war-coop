#include "cmdfile.h"

#include "ue/log.h"

#include <windows.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace ubw::cmdfile {
namespace log = ue::log;
namespace {

constexpr size_t kPathChars = MAX_PATH;
constexpr DWORD kReadBytes = 4096;        // one poll reads at most this much of the file
constexpr uint32_t kMaxMs = 60000;        // key hold and axis duration ceiling
constexpr uint32_t kDefaultHoldMs = 100;  // `key <K>` without a hold time
constexpr float kDefaultWarpDist = 300.0f; // card 075: `warp` without a distance
constexpr float kMaxAxis = 1000.0f;
// 6 since card 106: warp actor|client point x y z; 7 since card 132: spawn bug x y z yaw kind
constexpr uint32_t kMaxTokens = 7;
constexpr double kMaxCoord = 1.0e7;   // goto: |x|, |y|, |z| ceiling (uu)
constexpr double kMaxChargeSlot = 3.0; // charge: highest slot index (card 106)
constexpr double kMaxCharge = 1.0e6;   // charge: value ceiling (card 106)
constexpr float kMinRadius = 10.0f;   // goto radius floor and ceiling (uu)
constexpr float kMaxRadius = 5000.0f;
constexpr double kMaxYaw = 720.0;     // look: |yaw| ceiling (degrees)
constexpr double kMaxPitch = 89.0;    // look: |pitch| ceiling (degrees)
constexpr uint32_t kStateErrorLines = 4;

struct Files {
    wchar_t cmd[kPathChars];
    wchar_t state[kPathChars];
    wchar_t tmp[kPathChars];
    HANDLE in;
    uint64_t offset;   // bytes of the .cmd file consumed
    uint32_t seq;      // lines consumed
    uint32_t state_errors;
};
Files g_files{{}, {}, {}, INVALID_HANDLE_VALUE, 0, 0, 0};

struct Tokens {
    char* t[kMaxTokens];
    uint32_t n;
};

// Splits `line` in place on spaces; more than kMaxTokens tokens sets n past kMaxTokens.
Tokens split(char* line) {
    Tokens k{};
    char* p = line;
    for (uint32_t guard = 0; guard < kLineChars && *p != '\0'; ++guard) {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }
        if (k.n < kMaxTokens) {
            k.t[k.n] = p;
        }
        ++k.n;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            ++p;
        }
    }
    return k;
}

bool name_char(char c, bool dash) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
           (dash && c == '-');
}

// A token of 1..cap-1 name chars.
bool valid_name(const char* s, size_t cap, bool dash) {
    const size_t len = strnlen(s, cap);
    for (size_t i = 0; i < len; ++i) {
        if (!name_char(s[i], dash)) {
            return false;
        }
    }
    return len > 0 && len < cap;
}

bool parse_ms(const char* s, uint32_t* out) {
    char* end = nullptr;
    const unsigned long v = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || v > kMaxMs) {
        return false;
    }
    *out = static_cast<uint32_t>(v);
    return true;
}

bool parse_axis(const char* s, float* out) {
    char* end = nullptr;
    const float v = strtof(s, &end);
    if (end == s || *end != '\0' || !isfinite(v) || fabsf(v) > kMaxAxis) {
        return false;
    }
    *out = v;
    return true;
}

bool parse_double(const char* s, double limit, double* out) {
    char* end = nullptr;
    const double v = strtod(s, &end);
    if (end == s || *end != '\0' || !isfinite(v) || fabs(v) > limit) {
        return false;
    }
    *out = v;
    return true;
}

bool parse_radius(const char* s, float* out) {
    double v = 0.0;
    if (!parse_double(s, kMaxRadius, &v) || v < kMinRadius) {
        return false;
    }
    *out = static_cast<float>(v);
    return true;
}

bool set_actor(const char* s, Cmd* c) {
    if (!valid_name(s, kActorChars, false)) {
        return false;
    }
    memcpy(c->actor, s, strlen(s) + 1);
    return true;
}

// A trailing `flag` token (card 124: goto `sprint`, look `smooth`): true and dropped from the copy in *out.
bool strip_flag(const Tokens& k, const char* flag, Tokens* out) {
    *out = k;
    if (k.n < 2 || strcmp(k.t[k.n - 1], flag) != 0) {
        return false;
    }
    --out->n;
    return true;
}

// `goto <x> <y> <z> [radius] [sprint]` or `goto actor <name> [radius] [sprint]` (card 124: sprint holds LeftShift).
// Card 284 objective verbs (host): `objective complete <id>|all` (card 095), `objective now`, `objective expect <id>` (3 s poll),
// `objective go [host|client|client2|all]` (default all, 1 s settle), `objective wait <id> [seconds]` (default 60, 1 to 900),
// `objective next` (b238).
bool parse_objective(const Tokens& k, Cmd* c, const char** why) {
    *why = "objective takes complete <id>|all, now, next, expect <id>, go [host|client|client2|all] or wait <id> "
           "[seconds]";
    const char* verb = k.n >= 2 ? k.t[1] : "";
    if (strcmp(verb, "complete") == 0) {
        c->op = Op::Complete;
        return k.n == 3 && set_actor(k.t[2], c);
    }
    if (strcmp(verb, "now") == 0) {
        c->op = Op::ObjectiveNow;
        return k.n == 2;
    }
    if (strcmp(verb, "next") == 0) {
        c->op = Op::ObjectiveNext;
        return k.n == 2;
    }
    if (strcmp(verb, "expect") == 0) {
        c->op = Op::ObjectiveExpect;
        c->ms = 3000; // polled: longer than the 1.5 s marker blink (card 284 review 1)
        return k.n == 3 && set_actor(k.t[2], c);
    }
    if (strcmp(verb, "go") == 0) {
        static constexpr const char* kWho[] = {"host", "client", "client2", "all"};
        c->op = Op::ObjectiveGo;
        c->ms = 1000; // the settle before the rows (card 284 review 3)
        c->copy = 3;
        if (k.n == 2) {
            return true;
        }
        // Card 514: `objective go <who> home` stands at the step's own stand (its first go's, the presence nearest its
        // marker) without counting a go, so the long stay after the presence tries waits in the step's own volume.
        const bool home = k.n == 4 && strcmp(k.t[3], "home") == 0;
        for (uint8_t i = 0; i < 4 && (k.n == 3 || home); ++i) {
            if (strcmp(k.t[2], kWho[i]) == 0) {
                c->copy = static_cast<uint8_t>(i | (home ? kGoHome : 0u));
                return true;
            }
        }
        return false;
    }
    if (strcmp(verb, "wait") == 0) {
        c->op = Op::ObjectiveWait;
        c->ms = 60000;
        uint32_t s = 60;
        if (k.n == 4 && (!parse_ms(k.t[3], &s) || s < 1 || s > 900)) {
            return false;
        }
        c->ms = s * 1000;
        return (k.n == 3 || k.n == 4) && set_actor(k.t[2], c);
    }
    return false;
}

// Card 244 b208: a trailing `live` (after any `sprint`) skips corpses and taken pickups, as look's does: c244b207b's
// `goto actor BP_SatchelItemPickup_C` walked back to the taken BP_SatchelItemPickup_C_0 in every block after the first.
bool parse_goto(const Tokens& all, Cmd* c, const char** why) {
    Tokens s{};
    Tokens k{};
    c->live = strip_flag(all, "live", &s);
    c->sprint = strip_flag(s, "sprint", &k);
    c->op = Op::Goto;
    c->radius = kDefaultRadius;
    if (strcmp(k.t[1], "actor") == 0 && (k.n == 3 || k.n == 4)) {
        *why = "bad actor name (1-63 chars of [A-Za-z0-9_]) or radius (10-5000)";
        return set_actor(k.t[2], c) && (k.n == 3 || parse_radius(k.t[3], &c->radius));
    }
    *why = "goto takes <x> <y> <z> [radius] or actor <name> [radius]";
    if (k.n != 4 && k.n != 5) {
        return false;
    }
    *why = "bad coordinate or radius (10-5000)";
    for (uint32_t i = 0; i < 3; ++i) {
        if (!parse_double(k.t[1 + i], kMaxCoord, &c->at[i])) {
            return false;
        }
    }
    return k.n == 4 || parse_radius(k.t[4], &c->radius);
}

// `look <yaw> <pitch>`, `look actor <name> [dz]` (dz: uu added to the actor's z) or `look point <x> <y> <z>`
// (both card 065). Card 124: a trailing `smooth` turns there through MouseX/MouseY axis events over several ticks.
// Card 131: a `bounds` before `smooth` faces the actor's bounds centre instead of its origin.
// Card 281 (b190): a `live` with `bounds` skips corpses (c64_find_actor's live test), so a shot aimed at a bug class
// after a kill clear hits the code-spawned live bug.
// b232: a `hole` (with or without `bounds`, before `smooth`) faces the actor's nearest grenade hole component.
// b14: a trailing `focus=<prefix>` (the drive's card 264 gate word, passed through) makes `look actor` a focus scan.
bool parse_look(const Tokens& raw, Cmd* c, const char** why) {
    Tokens all = raw;
    const char* const last = raw.n >= 2 ? raw.t[raw.n - 1] : "";
    if (strncmp(last, "focus=", 6) == 0) {
        if (!valid_name(last + 6, kActorChars, false)) {
            *why = "look focus= takes a name prefix";
            return false;
        }
        memcpy(c->want, last + 6, strlen(last + 6) + 1);
        --all.n;
    }
    Tokens s{};
    Tokens l{};
    Tokens h{};
    Tokens k{};
    c->smooth = strip_flag(all, "smooth", &s);
    c->live = strip_flag(s, "live", &l);
    c->hole = strip_flag(l, "hole", &h);
    c->bounds = strip_flag(h, "bounds", &k);
    c->op = Op::Look;
    if (c->want[0] != '\0' && c->smooth) {
        *why = "look focus= scans by snap looks: no smooth";
        return false;
    }
    *why = "look takes <yaw> <pitch> (|pitch| <= 89), actor <name> [dz] or point <x> <y> <z>";
    if (k.n == 4 && strcmp(k.t[1], "actor") == 0) {
        return set_actor(k.t[2], c) && parse_double(k.t[3], kMaxCoord, &c->at[2]);
    }
    if (k.n == 5 && strcmp(k.t[1], "point") == 0) {
        if (c->want[0] != '\0') {
            *why = "look focus= needs look actor <name> [dz] [bounds]";
            return false;
        }
        c->point = true;
        return parse_double(k.t[2], kMaxCoord, &c->at[0]) && parse_double(k.t[3], kMaxCoord, &c->at[1]) &&
               parse_double(k.t[4], kMaxCoord, &c->at[2]);
    }
    if (k.n != 3) {
        return false;
    }
    if (strcmp(k.t[1], "actor") == 0) {
        return set_actor(k.t[2], c);
    }
    if (c->want[0] != '\0') {
        *why = "look focus= needs look actor <name> [dz] [bounds]";
        return false;
    }
    return parse_double(k.t[1], kMaxYaw, &c->at[0]) && parse_double(k.t[2], kMaxPitch, &c->at[1]);
}

// `warp actor <name> [dist]` or `warp client <name> [dist]` (card 075; dist 10-5000, default 300), or
// `warp actor|client point <x> <y> <z>`: exactly that location, no lift (card 106). Card 123: `warp actor|client
// <name> <dist> behind` puts the pawn dist uu behind the actor along its forward axis (a turret's seat side); card
// 264: `front` puts it dist uu in front, along the forward axis (a switch's face side). Card 244 b210: a trailing
// `live` skips corpses and taken pickups (bEnabled 0), as goto's does: the LZ_SO1 block warps the client to the nearest
// untaken satchel pickup when the goto to it fails.
bool parse_warp(const Tokens& all, Cmd* c, const char** why) {
    Tokens k{};
    c->live = strip_flag(all, "live", &k);
    c->op = Op::Warp;
    c->radius = kDefaultWarpDist;
    *why = "warp takes actor|client|client2 <name> [dist [behind|front]] [live] (dist 10-5000) or actor|client|client2 "
           "point <x> <y> <z>";
    c->copy = strcmp(k.t[1], "client") == 0 ? 1 : strcmp(k.t[1], "client2") == 0 ? 2 : 0; // card 209: client2
    if (k.n < 3 || (c->copy == 0 && strcmp(k.t[1], "actor") != 0)) {
        return false;
    }
    if (strcmp(k.t[2], "point") == 0) {
        c->point = true;
        return k.n == 6 && parse_double(k.t[3], kMaxCoord, &c->at[0]) && parse_double(k.t[4], kMaxCoord, &c->at[1]) &&
               parse_double(k.t[5], kMaxCoord, &c->at[2]);
    }
    c->behind = k.n == 5 && strcmp(k.t[4], "behind") == 0;
    c->front = k.n == 5 && strcmp(k.t[4], "front") == 0;
    return (k.n == 3 || k.n == 4 || c->behind || c->front) && set_actor(k.t[2], c) &&
           (k.n == 3 || parse_radius(k.t[3], &c->radius));
}

bool set_key(const char* s, wchar_t* out) {
    if (!valid_name(s, kKeyChars, false)) {
        return false;
    }
    const size_t len = strlen(s);
    for (size_t i = 0; i <= len; ++i) {
        out[i] = static_cast<wchar_t>(s[i]);
    }
    return true;
}

// Fills c from the tokens; false (with *why) when the line is malformed.
bool parse_tokens(const Tokens& k, Cmd* c, const char** why) {
    *why = "wrong argument count";
    if (k.n == 0 || k.n > kMaxTokens) {
        return false;
    }
    const char* const op = k.t[0];
    const bool win = strcmp(op, "winkey") == 0; // card 222: `winkey <K> [hold_ms]`, the key without a chord
    if ((strcmp(op, "key") == 0 && (k.n == 2 || k.n == 3 || k.n == 5)) || (win && (k.n == 2 || k.n == 3))) {
        c->op = win ? Op::WinKey : Op::Key;
        c->ms = kDefaultHoldMs;
        *why = "bad key name or hold_ms (chord: at_ms must be below hold_ms)";
        if (!set_key(k.t[1], c->key) || (k.n > 2 && !parse_ms(k.t[2], &c->ms))) {
            return false;
        }
        return k.n != 5 || (set_key(k.t[3], c->key2) && parse_ms(k.t[4], &c->at_ms) && c->at_ms < c->ms);
    }
    if (strcmp(op, "axis") == 0 && k.n == 4) {
        c->op = Op::Axis;
        *why = "bad key name, value or ms";
        return set_key(k.t[1], c->key) && parse_axis(k.t[2], &c->value) && parse_ms(k.t[3], &c->ms) && c->ms > 0;
    }
    if (strcmp(op, "shot") == 0 && k.n == 2) {
        c->op = Op::Shot;
        *why = "bad shot name (1-40 chars of [A-Za-z0-9_-])";
        if (!valid_name(k.t[1], kNameChars, true)) {
            return false;
        }
        memcpy(c->name, k.t[1], strlen(k.t[1]) + 1);
        return true;
    }
    if (strcmp(op, "goto") == 0 && k.n >= 3) { // checked again after a trailing sprint is dropped
        return parse_goto(k, c, why);
    }
    if (strcmp(op, "look") == 0) {
        return parse_look(k, c, why);
    }
    if (strcmp(op, "warp") == 0) {
        return parse_warp(k, c, why);
    }
    if (strcmp(op, "clear") == 0) { // card 265: `clear <radius> [seconds]`, the own pawn kills the live bugs within
        // radius; the clear ends after seconds (10-90, default 90). `clear <radius> kill` (user rule 2026-11-13,
        // objective tests): the host kills them by code instead, no fire (Op::ClearKill)
        c->op = Op::Clear;
        *why = "clear takes <radius> (100-20000) [seconds] (10-90) or <radius> kill";
        double r = 0.0;
        double s = 90.0;
        if (k.n == 3 && strcmp(k.t[2], "kill") == 0) {
            c->op = Op::ClearKill;
            if (!parse_double(k.t[1], 20000.0, &r) || r < 100.0) {
                return false;
            }
            c->radius = static_cast<float>(r);
            return true;
        }
        if (k.n < 2 || k.n > 3 || !parse_double(k.t[1], 20000.0, &r) || r < 100.0 ||
            (k.n == 3 && (!parse_double(k.t[2], 90.0, &s) || s < 10.0))) {
            return false;
        }
        c->radius = static_cast<float>(r);
        c->ms = static_cast<uint32_t>(s * 1000.0);
        return true;
    }
    // card 098: `kill actor <name>`, InstaKillTarget on the nearest such actor. b216 (card 245 boss stages):
    // `kill actor <name> <percent>`, one DealDamage hit of percent (1-100) of its MaxHealth instead (value)
    if (strcmp(op, "kill") == 0) {
        c->op = Op::Kill;
        c->value = 0.0f;
        *why = "kill takes actor <name> [percent of MaxHealth, 1-100]";
        double pct = 0.0;
        const bool pct_ok = k.n == 3 || (k.n == 4 && parse_double(k.t[3], 100.0, &pct) && pct >= 1.0);
        c->value = static_cast<float>(pct);
        return pct_ok && strcmp(k.t[1], "actor") == 0 && set_actor(k.t[2], c);
    }
    // card 125: `spawn bug <x> <y> <z> <yaw> [<kind>]`, one bug there (host): BP_AI_<kind>Bug, WarriorBug when left out
    // (card 132: ArcherBug, the Plasmabug round's shooter)
    if (strcmp(op, "spawn") == 0) {
        c->op = Op::Spawn;
        *why = "spawn takes bug <x> <y> <z> <yaw> [<kind>]";
        double yaw = 0.0;
        c->actor[0] = '\0';
        const bool ok = (k.n == 6 || (k.n == 7 && set_actor(k.t[6], c))) && strcmp(k.t[1], "bug") == 0 &&
                        parse_double(k.t[2], kMaxCoord, &c->at[0]) &&
                        parse_double(k.t[3], kMaxCoord, &c->at[1]) && parse_double(k.t[4], kMaxCoord, &c->at[2]) &&
                        parse_double(k.t[5], 360.0, &yaw);
        c->radius = static_cast<float>(yaw);
        return ok;
    }
    if (strcmp(op, "speed") == 0) { // card 130: `speed <x>`, the host sets AWorldSettings::TimeDilation (replicated)
        c->op = Op::Speed;
        *why = "speed takes a factor from 0.1 to 4";
        c->at[0] = k.n == 2 ? atof(k.t[1]) : 0.0;
        return k.n == 2 && c->at[0] >= 0.1 && c->at[0] <= 4.0;
    }
    if (strcmp(op, "ammo") == 0) { // card 243 run A decision, test only: `ammo fill`, both roles, on the caller's own pawn
        c->op = Op::AmmoFill;
        *why = "ammo takes fill";
        return k.n == 2 && strcmp(k.t[1], "fill") == 0;
    }
    if (strcmp(op, "inventory") == 0) { // card 132: `inventory`, the weapon inventory of the own pawn (and the
        c->op = Op::Inventory;            // host's server copy of the client's pawn) to the log
        *why = "inventory takes no arguments";
        return k.n == 1;
    }
    if (strcmp(op, "god") == 0) { // card 132 hb53 note 3: `god host` ends the client pawn's god (UBWCOOP_GOD), `god all`
        c->op = Op::God;            // gives it back; set on both sides (the client logs its pawn's Health while host)
        *why = "god takes all or host";
        c->at[0] = k.n == 2 && strcmp(k.t[1], "host") == 0 ? 1.0 : 0.0;
        return k.n == 2 && (strcmp(k.t[1], "host") == 0 || strcmp(k.t[1], "all") == 0);
    }
    if (strcmp(op, "stamina") == 0) { // card 490: `stamina on|off`, the host's infinite stamina option for this
        c->op = Op::Stamina;            // session (the saved panel setting is not changed)
        *why = "stamina takes on or off";
        c->at[0] = k.n == 2 && strcmp(k.t[1], "on") == 0 ? 1.0 : 0.0;
        return k.n == 2 && (strcmp(k.t[1], "on") == 0 || strcmp(k.t[1], "off") == 0);
    }
    if (strcmp(op, "grenade") == 0) { // b234 (user: "just give them enough grenades to kill nests"), test only:
        c->op = Op::GrenadeFill;      // `grenade fill <n>`, the host raises every player's frag count to n
        *why = "grenade takes fill and a count 1 to 20";
        const int n = k.n == 3 && strcmp(k.t[1], "fill") == 0 ? atoi(k.t[2]) : 0;
        c->at[0] = static_cast<double>(n);
        return n >= 1 && n <= 20;
    }
    if (strcmp(op, "discard") == 0) { // card 281 (b187), test only: `discard <n>`, the host discards the client copy's
        c->op = Op::Discard;            // next n rounds (1 to 10), so their claims take the made-round path
        *why = "discard takes a count 1 to 10";
        const int n = k.n == 2 ? atoi(k.t[1]) : 0;
        c->at[0] = static_cast<double>(n);
        return n >= 1 && n <= 10;
    }
    if (strcmp(op, "offshot") == 0) { // card 132: `offshot`, the host shoots the next still sprite-drawn NPC side-on
        c->op = Op::OffShot;
        *why = "offshot takes no arguments";
        return k.n == 1;
    }
    if (strcmp(op, "autoplay") == 0) { // card 126: `autoplay on|off`, the local player follows the objectives
        c->op = Op::Autoplay;
        *why = "autoplay takes on or off";
        const bool on = k.n == 2 && strcmp(k.t[1], "on") == 0;
        c->at[0] = on ? 1.0 : 0.0;
        return k.n == 2 && (on || strcmp(k.t[1], "off") == 0);
    }
    if (strcmp(op, "down") == 0) { // card 109: `down host|client`, the host puts that player into card 043's Downed;
        c->op = Op::Down;            // card 209: `down client2`, roster slot 2
        *why = "down takes host, client or client2";
        const bool client = k.n == 2 && strcmp(k.t[1], "client") == 0;
        const bool client2 = k.n == 2 && strcmp(k.t[1], "client2") == 0;
        c->at[0] = client2 ? 2.0 : client ? 1.0 : 0.0;
        return k.n == 2 && (client || client2 || strcmp(k.t[1], "host") == 0);
    }
    if (strcmp(op, "travel") == 0) { // card 113: `travel next|restart`, the host's TransitionToLevel (next or same);
        c->op = Op::Travel;            // card 132: `travel checkpoint`, the host's LoadLatestSave in place
        *why = "travel takes next, restart, checkpoint, save, end or join"; // card 143: `travel save`, the host's Save_Quicksave
        const bool next = k.n == 2 && strcmp(k.t[1], "next") == 0;
        const bool checkpoint = k.n == 2 && strcmp(k.t[1], "checkpoint") == 0;
        const bool save = k.n == 2 && strcmp(k.t[1], "save") == 0;
        const bool end = k.n == 2 && strcmp(k.t[1], "end") == 0; // card 345: `travel end`, the host's EndLevel
        const bool join = k.n == 2 && strcmp(k.t[1], "join") == 0; // card 374: `travel join`, a client's lobby join
        c->at[0] = next ? 1.0 : checkpoint ? 2.0 : save ? 3.0 : end ? 4.0 : join ? 5.0 : 0.0;
        return k.n == 2 && (next || checkpoint || save || end || join || strcmp(k.t[1], "restart") == 0);
    }
    if (strcmp(op, "charge") == 0) { // card 106: `charge <slot> <value> <ready>`, own pawn's tactical support slot
        c->op = Op::Charge;
        *why = "charge takes [client] <slot 0-3> <value 0-1000000> <ready 0|1>";
        const uint32_t o = k.n == 5 && strcmp(k.t[1], "client") == 0 ? 1u : 0u; // card 324: host, the server copy's slot
        c->copy = static_cast<uint8_t>(o);
        if (k.n != 4 + o || (strcmp(k.t[3 + o], "0") != 0 && strcmp(k.t[3 + o], "1") != 0)) {
            return false;
        }
        c->at[2] = k.t[3 + o][0] == '1' ? 1.0 : 0.0;
        return parse_double(k.t[1 + o], kMaxChargeSlot, &c->at[0]) && c->at[0] >= 0.0 &&
               parse_double(k.t[2 + o], kMaxCharge, &c->at[1]) && c->at[1] >= 0.0;
    }
    if (strcmp(op, "ui") == 0) {
        c->op = Op::Ui;
        *why = "ui takes continue or click <function name prefix>";
        if (k.n == 3 && strcmp(k.t[1], "click") == 0) {
            return set_actor(k.t[2], c); // card 121: the widget function's name prefix
        }
        return k.n == 2 && strcmp(k.t[1], "continue") == 0;
    }
    if (strcmp(op, "quit") == 0 && k.n == 1) {
        c->op = Op::Quit;
        return true;
    }
    if (strcmp(op, "crash") == 0) {
        // Card 118, test only: a deliberate access violation that shows a crash trace block.
        c->op = Op::Crash;
        *why = "crash takes av";
        return k.n == 2 && strcmp(k.t[1], "av") == 0;
    }
    if (strcmp(op, "disconnect") == 0 && k.n == 1) {
        c->op = Op::Disconnect;
        return true;
    }
    if (strcmp(op, "door") == 0) {
        c->op = Op::Door;
        *why = "door takes list, open <name> or close <name>";
        if (k.n == 2 && strcmp(k.t[1], "list") == 0) {
            c->door = 0;
            return true;
        }
        c->door = k.n == 3 && strcmp(k.t[1], "open") == 0 ? 'O' : (k.n == 3 && strcmp(k.t[1], "close") == 0 ? 'C' : 0);
        return c->door != 0 && set_actor(k.t[2], c);
    }
    if (strcmp(op, "objective") == 0) {
        return parse_objective(k, c, why);
    }
    if (strcmp(op, "levelcall") == 0) { // b184, test only: `levelcall <word> [<word> ...]`, joined by single spaces
        c->op = Op::LevelCall;
        *why = "levelcall takes a function name of 1-6 words of [A-Za-z0-9_], 63 chars at most";
        size_t at = 0;
        for (uint32_t i = 1; i < k.n; ++i) {
            const size_t len = strnlen(k.t[i], kActorChars);
            if (!valid_name(k.t[i], kActorChars, false) || at + len + (i > 1 ? 1 : 0) >= kActorChars) {
                return false;
            }
            if (i > 1) {
                c->actor[at++] = ' ';
            }
            memcpy(c->actor + at, k.t[i], len);
            at += len;
        }
        c->actor[at] = '\0';
        return at > 0;
    }
    if (strcmp(op, "objectives") == 0 && k.n == 1) {
        c->op = Op::Objectives;
        return true;
    }
    *why = "unknown command or wrong argument count";
    return false;
}

// One line (no newline, `len` bytes). True when it parsed into *c.
bool parse_line(const char* line, size_t len, Cmd* c) {
    *c = Cmd{};
    c->seq = ++g_files.seq;
    const size_t keep = len < kLineChars ? len : kLineChars - 1;
    memcpy(c->text, line, keep);
    c->text[keep] = '\0';
    if (keep > 0 && c->text[keep - 1] == '\r') {
        c->text[keep - 1] = '\0';
    }
    const char* why = "line too long";
    char work[kLineChars];
    memcpy(work, c->text, sizeof(work));
    if (len < kLineChars && parse_tokens(split(work), c, &why)) {
        return true;
    }
    log::line("WARN cmd: %u \"%s\" skipped: %s", c->seq, c->text, why);
    return false;
}

// Reads up to kReadBytes at the consumed offset; returns the byte count (0 on error, logged).
DWORD read_chunk(char* buf) {
    OVERLAPPED at{};
    at.Offset = static_cast<DWORD>(g_files.offset & 0xFFFFFFFFu);
    at.OffsetHigh = static_cast<DWORD>(g_files.offset >> 32);
    DWORD got = 0;
    if (!ReadFile(g_files.in, buf, kReadBytes, &got, &at)) {
        const DWORD err = GetLastError();
        if (err != ERROR_HANDLE_EOF) {
            log::line("WARN cmd: ReadFile failed, err=%lu", err);
        }
        return 0;
    }
    return got;
}

} // namespace

bool open(const wchar_t* tag) {
    if (!log::path_next_to_exe(tag, L".cmd", g_files.cmd, kPathChars) ||
        !log::path_next_to_exe(tag, L".state", g_files.state, kPathChars) ||
        !log::path_next_to_exe(tag, L".state.tmp", g_files.tmp, kPathChars)) {
        log::line("ERROR cmd: the command or state path does not fit -- command file off");
        return false;
    }
    g_files.in = CreateFileW(g_files.cmd, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_files.in == INVALID_HANDLE_VALUE) {
        log::line("ERROR cmd: CreateFileW(%ls) failed, err=%lu -- command file off", g_files.cmd, GetLastError());
        return false;
    }
    log::line("cmd: polling %ls, state to %ls", g_files.cmd, g_files.state);
    return true;
}

int32_t poll(Cmd* out, int32_t cap) {
    if (g_files.in == INVALID_HANDLE_VALUE || cap <= 0) {
        return 0;
    }
    char buf[kReadBytes];
    const DWORD got = read_chunk(buf);
    int32_t n = 0;
    DWORD start = 0;
    for (DWORD i = 0; i < got && n < cap; ++i) {
        if (buf[i] != '\n') {
            continue;
        }
        if (i > start && parse_line(buf + start, i - start, &out[n])) {
            ++n;
        }
        start = i + 1;
    }
    if (start == 0 && got == kReadBytes) {
        log::line("WARN cmd: %lu bytes with no newline at offset %llu skipped", got,
                  static_cast<unsigned long long>(g_files.offset));
        start = got;
    }
    g_files.offset += start;
    return n;
}

bool write_state(const char* text, size_t len) {
    const HANDLE h = CreateFileW(g_files.tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD put = 0;
    const bool wrote = h != INVALID_HANDLE_VALUE && len <= MAXDWORD &&
                       WriteFile(h, text, static_cast<DWORD>(len), &put, nullptr) && put == len;
    const DWORD err = GetLastError();
    if (h != INVALID_HANDLE_VALUE && !CloseHandle(h) && wrote) {
        log::line("WARN cmd: CloseHandle(state tmp) failed, err=%lu", GetLastError());
    }
    const bool ok = wrote && MoveFileExW(g_files.tmp, g_files.state, MOVEFILE_REPLACE_EXISTING);
    if (!ok && g_files.state_errors < kStateErrorLines) {
        ++g_files.state_errors;
        log::line("WARN cmd: state write failed (%s), err=%lu", wrote ? "rename" : "temp file",
                  wrote ? GetLastError() : err);
    }
    return ok;
}

} // namespace ubw::cmdfile
