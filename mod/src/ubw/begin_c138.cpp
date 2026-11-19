// Card 138: on a client, the world's BeginPlay dispatch waits for the local pawn.
//
// A client runs every placed actor's BeginPlay from AGameStateBase::OnRep_ReplicatedHasBegunPlay (exe+0x3C624D0:
// WorldSettings NotifyBeginPlay, then NotifyMatchStarted). When the GameState's replication arrives before the local
// PlayerController has its pawn, the level actors' BeginPlay reads a null "player": hb59 crashed in
// BP_PickupTracker's BeginPlay at GetWeaponInventory (exe+0x51EB9BA). The single-player code assumes the player
// exists when BeginPlay runs, which is the order a client sees when the pawn arrives first.
//
// So on a client the OnRep is held while the local PC (GetPlayerController(world, 0)) has no possessed player
// pawn (an ASTPlayerCharacter: BP_PlayerCharacterMI on the MI maps, BP_PlayerCharacterBug on the bug maps, card 195),
// and called once that pawn is there: from the engine tick (c138_step) and from the
// card 120 Pong gate, which also answers no Ping while the hold is on, so every host join replay (journal, world ops,
// doors, pause lift, card 132 rows) and every live send, which goes to ponged clients only, lands after the dispatch.
// The hold never times out: running the dispatch without the pawn is the crash. A WARN every kC138WarnMs says it is
// still on. Host and standalone (ROLE_Authority) call through unchanged.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

C138OnRepFn g_orig_c138_on_rep = nullptr;
uint32_t g_c138_hold_ms = 0;

namespace {

constexpr size_t kGsReplicatedHasBegunPlay = 0x2D0; // AGameStateBase::bReplicatedHasBegunPlay (the OnRep's test)
constexpr ULONGLONG kC138WarnMs = 10000;
constexpr char kC154MenuMap[] = "3D_Frontend"; // the main menu and hangar map's package name part

struct C138Hold {
    uint64_t gs_weak = 0;   // FWeakObjectPtr to the held GameState; 0 = no hold
    ULONGLONG start_ms = 0;
    ULONGLONG next_warn_ms = 0;
    bool pawn_logged = false; // "pawn possessed" written for this hold
    uint32_t pings_held = 0;  // host Pings left unanswered while held (card 120 gate)
};
C138Hold g_c138;

// The local PC's pawn when it is a possessed player character (ASTPlayerCharacter, the base of the MI and the bug
// pawn, card 195), else null; *pc_out gets the PC (or null). Without the resolved class any possessed pawn counts, so
// the hold still releases at possession: a hold that never releases is a dead client.
void* c138_local_pawn(const void* world, void** pc_out) {
    void* const pc = world != nullptr ? g_get_player_controller(world, 0) : nullptr;
    *pc_out = pc;
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn == nullptr || g_player_character_class == nullptr) {
        return pawn;
    }
    return is_a(pawn, g_player_character_class()) ? pawn : nullptr;
}

} // namespace

void detour_c138_on_rep(void* gs) {
    const auto* const bytes = static_cast<const uint8_t*>(gs);
    if (bytes[kActorLocalRole] == kRoleAuthority || bytes[kGsReplicatedHasBegunPlay] == 0 || g_weak_assign == nullptr ||
        g_weak_get == nullptr) {
        g_orig_c138_on_rep(gs);
        return;
    }
    const ULONGLONG now = GetTickCount64();
    void* const world = g_actor_get_world(gs);
    // Card 154: the host's hangar (3D_Frontend) has no BP_PlayerCharacterMI pawn (the local one is
    // BP_Pawn_3DFrontend_C), so a hold there never released: no hangar BeginPlay and no Pong (c154-r1).
    if (world != nullptr && strstr(object_name(read_ptr(world, kObjectOuter)).text, kC154MenuMap) != nullptr) {
        log::line("card154: client t=%llu no hold: GameState %p begun play on the menu map (no MI pawn there)",
                  static_cast<unsigned long long>(now), gs);
        g_orig_c138_on_rep(gs);
        return;
    }
    void* pc = nullptr;
    void* const pawn = c138_local_pawn(world, &pc);
    if (pawn != nullptr && g_c138_hold_ms == 0) {
        log::line("card138: client t=%llu no hold: GameState %p begun play with pawn %p (%s) on PC %p",
                  static_cast<unsigned long long>(now), gs, pawn, class_name(pawn).text, pc);
        g_orig_c138_on_rep(gs);
        return;
    }
    if (g_c138.gs_weak != 0) {
        log::line("WARN card138: client t=%llu hold start on GameState %p while a hold was on (%llu ms): replaced",
                  static_cast<unsigned long long>(now), gs, static_cast<unsigned long long>(now - g_c138.start_ms));
    }
    void* object = gs;
    g_weak_assign(&g_c138.gs_weak, &object);
    g_c138.start_ms = now;
    g_c138.next_warn_ms = now + kC138WarnMs;
    log::line("card138: client t=%llu hold start: GameState %p begun play, PC %p pawn %p (%s): BeginPlay dispatch held%s",
              static_cast<unsigned long long>(now), gs, pc, pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr,
              pc != nullptr ? class_name(read_ptr(pc, kControllerPawn)).text : "no PC",
              g_c138_hold_ms != 0 ? " (UBWCOOP_C138_HOLD_MS: forced)" : "");
}

// Client, every engine tick and before each Pong: releases the hold once the local player pawn is possessed.
void c138_step() {
    if (g_c138.gs_weak == 0) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    void* const gs = g_weak_get(&g_c138.gs_weak);
    if (gs == nullptr) {
        log::line("WARN card138: client t=%llu hold dropped after %llu ms: the GameState is gone",
                  static_cast<unsigned long long>(now), static_cast<unsigned long long>(now - g_c138.start_ms));
        g_c138 = C138Hold{};
        return;
    }
    void* pc = nullptr;
    void* const pawn = c138_local_pawn(g_actor_get_world(gs), &pc);
    if (pawn != nullptr && !g_c138.pawn_logged) {
        g_c138.pawn_logged = true;
        log::line("card138: client t=%llu pawn possessed: %p (%s) on PC %p, %llu ms into the hold",
                  static_cast<unsigned long long>(now), pawn, class_name(pawn).text, pc,
                  static_cast<unsigned long long>(now - g_c138.start_ms));
    }
    if (pawn == nullptr || now - g_c138.start_ms < g_c138_hold_ms) {
        if (pawn == nullptr && now >= g_c138.next_warn_ms) {
            g_c138.next_warn_ms = now + kC138WarnMs;
            log::line("WARN card138: client t=%llu hold still on after %llu ms: PC %p pawn %p (%s)",
                      static_cast<unsigned long long>(now), static_cast<unsigned long long>(now - g_c138.start_ms), pc,
                      pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr,
                      pc != nullptr ? class_name(read_ptr(pc, kControllerPawn)).text : "no PC");
        }
        return;
    }
    const ULONGLONG held = now - g_c138.start_ms;
    const uint32_t pings = g_c138.pings_held;
    g_c138 = C138Hold{}; // cleared first, so a re-entry during the dispatch finds no hold
    g_c138_hold_ms = 0;  // the test's forced hold is the first one only: later ones show the real order
    g_orig_c138_on_rep(gs);
    log::line("card138: client t=%llu release after %llu ms: BeginPlay dispatch ran (%u host Pings held)",
              static_cast<unsigned long long>(GetTickCount64()), static_cast<unsigned long long>(held), pings);
}

// Client, from the card 120 Pong gate: releases the hold when it can, else counts the Ping and returns true.
bool c138_hold_ping() {
    c138_step();
    g_c138.pings_held += g_c138.gs_weak != 0 ? 1u : 0u;
    return g_c138.gs_weak != 0;
}

// Client, from the engine tick (card 157 batch, the card 154 Ping-to-Pong time): the same hold, without counting a Ping.
bool c138_holding() {
    c138_step();
    return g_c138.gs_weak != 0;
}

// Client, from the BeginPlay detour: the pawn BP_PickupTracker reads in its BeginPlay (hb59 read null and crashed).
void c138_note_begin_play(const void* actor) {
    if (strncmp(class_name(actor).text, "BP_PickupTracker", 16) != 0) {
        return;
    }
    void* pc = nullptr;
    void* const pawn = c138_local_pawn(g_actor_get_world(actor), &pc);
    log::line("%scard138: client t=%llu BeginPlay of %s: local pawn %p (%s)", pawn != nullptr ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), object_name(actor).text, pawn,
              pc != nullptr ? class_name(read_ptr(pc, kControllerPawn)).text : "no PC");
}

// Both sides, at LoadMap: a hold never outlives its world (the weak pointer would say so too).
void c138_load_map() {
    if (g_c138.gs_weak != 0) {
        log::line("WARN card138: %s hold dropped at LoadMap after %llu ms", side(),
                  static_cast<unsigned long long>(GetTickCount64() - g_c138.start_ms));
    }
    g_c138 = C138Hold{};
}

} // namespace ubw::hooks
