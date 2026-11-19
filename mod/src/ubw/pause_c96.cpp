// Card 096: the host's pause must not freeze the clients. USTGameInstance::UpdatePause (the pause menu, the mission
// brief movie and the "PRESS ANY KEY TO CONTINUE" screen all hold its requesters) calls UGameplayStatics::SetGamePaused,
// which on a listen server pauses the server world for everyone. While at least one client is connected (its server
// copy PC has sent its first Pong), the host skips the pause and returns false; the requester bookkeeping in
// USTGameInstance is untouched, so its unpause runs the same. Unpauses, a host with no client, and the client's own
// calls (no server copies there) run the original.
// Card 120: a pause already in force when the first client connects is lifted once (the original with paused false):
// from the client's first Pong (sync_join_replay, card 326) and from a skipped pause while the world is still paused. UpdatePause calls
// SetGamePaused(1) every frame while a requester is held (card 096's first run: skipped calls 2-10 ms apart), so the
// skip path alone would lift it within a frame; the Pong call lifts it at the moment the client arrives. The requester
// arrays are untouched: the host's menu stays open and its close runs the unpause as before.
// Card 320: while the title-screen hold is on (c320_holding, intro_c135.cpp) the host's pause goes through to the
// original with clients connected, an unpause is refused, and card 120 does not lift the pause.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

SetGamePausedFn g_orig_c96_set_game_paused = nullptr;

namespace {

constexpr uint32_t kC96Lines = 60;
uint32_t g_c96_lines = 0;
// Outcome of a call: (paused ? 2 : 0) + the original's result, kC96Skipped or kC96Held. -1 = nothing logged yet.
constexpr int kC96Skipped = 4;
constexpr int kC96Held = 5;
constexpr const char* kC96Outcome[] = {"= 0 (unpause refused)", "= 1 (unpaused)", "= 0 (pause refused)",
                                       "= 1 (world paused)", "skipped: the world keeps running",
                                       "refused: the card 320 title-screen hold keeps the world paused"};
int g_c96_last = -1;

// Remote players connected to this host: server copy PCs that have sent their first Pong. 0 on a client or solo.
int c96_clients(const void* world_context) {
    int n = 0;
    for (int s = 0; world_context != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world_context, s);
        const C50Copy* const c = pc != nullptr && sprint_side(pc) == kSideServerCopy ? c50_copy(object_index(pc), false)
                                                                                     : nullptr;
        n += c != nullptr && c->ponged ? 1 : 0;
    }
    return n;
}

// UpdatePause calls this every frame while a requester is held, so a line is written only when the outcome changes.
void c96_note(const void* world_context, bool paused, int outcome, int clients, unsigned long long caller) {
    if (outcome == g_c96_last || !note_event_cap(g_c96_lines, kC96Lines, "card096")) {
        return;
    }
    g_c96_last = outcome;
    log::line("card096: %s t=%llu SetGamePaused(%d) %s, %d client(s) connected; context %s, caller exe+0x%llX", side(),
              static_cast<unsigned long long>(GetTickCount64()), paused ? 1 : 0, kC96Outcome[outcome], clients,
              class_name(world_context).text, caller);
}

// Card 120: unpause the host world once if it is paused while a client is connected. `pc` is any PC of that world.
void c96_lift(const void* pc, const char* why) {
    void* const world = pc != nullptr ? g_actor_get_world(pc) : nullptr;
    if (world == nullptr || !g_world_is_paused(world) || c320_holding()) {
        return;
    }
    const bool ok = g_orig_c96_set_game_paused(pc, false);
    log::line("card096: %s t=%llu pause lifted: client connected while paused (%s), SetGamePaused(0) = %d", side(),
              static_cast<unsigned long long>(GetTickCount64()), why, ok ? 1 : 0);
}

} // namespace

// Card 120: host, a client's first Pong.
void c96_lift_on_join(const void* pc) {
    c96_lift(pc, "first Pong");
}

bool detour_c96_set_game_paused(const void* world_context, bool paused) {
    const int clients = paused ? c96_clients(world_context) : 0;
    const unsigned long long caller = exe_rva(_ReturnAddress());
    if (!paused && c320_holding()) {
        c96_note(world_context, paused, kC96Held, clients, caller);
        return false;
    }
    if (clients > 0 && !c320_holding()) {
        c96_note(world_context, paused, kC96Skipped, clients, caller);
        c96_lift(g_get_player_controller(world_context, 0), "pause skipped");
        return false;
    }
    const bool ok = g_orig_c96_set_game_paused(world_context, paused);
    c96_note(world_context, paused, (paused ? 2 : 0) + (ok ? 1 : 0), clients, caller);
    return ok;
}

} // namespace ubw::hooks
