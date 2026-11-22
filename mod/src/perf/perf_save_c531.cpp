// Card 531 (performance mod family, card 527): GetSaveId without its own component walk.
//
// GetSaveId (static FGuid GetSaveId(const AActor*), FGuid through a hidden out pointer) calls the actor's
// FindComponentByClass (vtable +0x720) with USTSaveComponent::StaticClass() and copies the FGuid at component +0x164.
// FindComponentByClass walks every owned component with IsA. The targeting code keys its per-target table by that
// GUID, so every target lookup pays one walk. c263L346 (perf -b, both sides): FindComponentByClass is 4.3% of the
// GameThread and 11% of Background Work, and GetSaveId is about 88% of the entries the branch stacks show (directly
// and through ACharacter::FindComponentByClass).
//
// b361..b388 kept a per-thread actor -> component cache here, checked with a weak pointer. Run c570b388a (host, card
// 571 counts): GetSaveId was the top hooked call (7.0M per 30 s), and every hit paid an exe FWeakObjectPtr::Get, the
// cost card 536 measured as 2.2% of Background Work. A "no save component" result was never stored: 954288 of 998246
// misses walked again.
//
// b390 (card 571 micro pass): the detour calls detour_c536_find directly. That cache holds the same answer, checked
// by card 537 generations (no exe call per hit), and stores "none" results too (99.73% hit in c570b388a). The
// virtual call is skipped: ACharacter's override only checks its mesh, movement and capsule members before the AActor
// walk, and the save component is none of them. The USTSaveComponent class is read once. Correctness and hit counts
// are card 536's (its verify and `card536:` report). Without card 536 (UBWCOOP_C536_OFF=1) the original runs.
// Off switch for A/B runs: UBWCOOP_C531_OFF=1 (the detour is then not installed).
//
// Not done: the original's incremental-GC barrier on the class pointer (it marks StaticClass's UClass reachable when
// incremental reachability is pending). A native UClass is never collected, so skipping it changes nothing.

#include "ubw/hooks_internal.h"

#include <atomic>
#include <string.h>

namespace ubw::hooks {

GetSaveIdFn g_orig_c531_save_id = nullptr;
StaticClassFn g_c531_static_class = nullptr;
bool g_c531_on = true; // written in ubw::init before any hook (UBWCOOP_C531_OFF=1 turns it off)

namespace {

constexpr size_t kC531Guid = 0x164;       // USTSaveComponent save FGuid
constexpr size_t kC531ObjectFlags = 0x08; // UObjectBase::ObjectFlags
constexpr uint32_t kC531GarbageBit = 30;  // the bit GetSaveId tests on the actor and the component

std::atomic<void*> g_c531_class{nullptr}; // USTSaveComponent::StaticClass(), read once (any thread may store it)

bool c531_garbage(const void* object) {
    uint32_t flags = 0;
    memcpy(&flags, static_cast<const uint8_t*>(object) + kC531ObjectFlags, sizeof(flags));
    return ((flags >> kC531GarbageBit) & 1) != 0;
}

} // namespace

uint32_t* detour_c531_save_id(uint32_t* out, const void* actor) {
    c594_note_save_id(); // card 594 diag: in-range count while a target gather runs
    if (!g_c536_on || g_c531_static_class == nullptr) {
        return g_orig_c531_save_id(out, actor);
    }
    memset(out, 0, 16);
    if (actor == nullptr || c531_garbage(actor)) {
        return out; // the original's early out
    }
    void* cls = g_c531_class.load(std::memory_order_relaxed);
    if (cls == nullptr) {
        cls = g_c531_static_class();
        g_c531_class.store(cls, std::memory_order_relaxed);
    }
    const void* const component = detour_c536_find(actor, &cls);
    if (component != nullptr && !c531_garbage(component)) {
        memcpy(out, static_cast<const uint8_t*>(component) + kC531Guid, 16);
    }
    return out;
}

} // namespace ubw::hooks
