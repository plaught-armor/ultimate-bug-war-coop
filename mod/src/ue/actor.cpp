#include "ue/actor.h"

#include <math.h>
#include <string.h>

namespace ue::actor {

using ue::reflect::read_ptr;

GetWorldFn g_actor_get_world = nullptr;
AttachParentFn g_get_attach_parent_actor = nullptr; // AActor::GetAttachParentActor
GameInstanceRefFn g_gi_register_ref = nullptr;      // UGameInstance::RegisterReferencedObject
GameInstanceRefFn g_gi_unregister_ref = nullptr;    // UGameInstance::UnregisterReferencedObject
ConstBoolFn g_world_is_paused = nullptr;             // UWorld::IsPaused
ThreadCheckFn g_is_in_game_thread = nullptr;         // ::IsInGameThread
SetActorLocationFn g_set_actor_location = nullptr;
SetActorLocationAndRotationFn g_set_actor_location_rotation = nullptr; // card 599
SetHiddenInGameFn g_set_hidden_in_game = nullptr; // USceneComponent::SetHiddenInGame(bool, EVisibilityPropagation)
SetHiddenInGameFn g_scene_set_visibility = nullptr; // USceneComponent::SetVisibility(bool, EVisibilityPropagation), card 159
SplineLengthFn g_spline_length = nullptr;          // USplineComponent::GetSplineLength, card 159
ActorFloatFn g_life_span = nullptr;
AddComponentByClassFn g_add_component_by_class = nullptr; // AActor::AddComponentByClass (TSubclassOf by pointer)
FinishAddComponentFn g_finish_add_component = nullptr;    // AActor::FinishAddComponent
QueryParamsCtorFn g_query_params_ctor = nullptr;
LineTraceByProfileFn g_line_trace_by_profile = nullptr;
LineTraceByChannelFn g_line_trace_by_channel = nullptr;
SpawnParamsCtorFn g_spawn_params_ctor = nullptr;
WorldSpawnActorFn g_world_spawn_actor = nullptr;
RecentlyRenderedFn g_actor_recently_rendered = nullptr;
SpawnAiFromClassFn g_spawn_ai_from_class = nullptr;
SpawnNiagaraFn g_spawn_niagara = nullptr;
GetSubsystemFn g_get_data_table_subsystem = nullptr;

Vec3 read_vec3(const void* base, size_t offset) {
    Vec3 v{};
    memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}

double vec_len(const Vec3& v) {
    return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

double vec_dist(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

Vec3 quat_pitch_yaw(const double* q) {
    const double fx = 1.0 - 2.0 * (q[1] * q[1] + q[2] * q[2]);
    const double fy = 2.0 * (q[0] * q[1] + q[3] * q[2]);
    const double fz = 2.0 * (q[0] * q[2] - q[3] * q[1]);
    return Vec3{atan2(fz, sqrt(fx * fx + fy * fy)) * kRadToDeg, atan2(fy, fx) * kRadToDeg, 0.0};
}

Vec3 component_pitch_yaw(const void* component) {
    double q[4] = {};
    memcpy(q, static_cast<const uint8_t*>(component) + kComponentRotation, sizeof(q));
    return quat_pitch_yaw(q);
}

Vec3 actor_location(const void* actor) {
    // b7 run 2026-11-13: card 306's c50_send_use_name passed a null interact target (a press at nothing) and the
    // client crashed reading 0x1B8; a null actor has no location, as a null root already gives.
    if (actor == nullptr) {
        return Vec3{};
    }
    const void* const root = read_ptr(actor, kActorRootComponent);
    return root != nullptr ? read_vec3(root, kComponentTranslation) : Vec3{};
}

void* find_component_of(const void* actor, void* cls) {
    const auto fn = reinterpret_cast<FindComponentFn>(read_ptr(read_ptr(actor, 0), kActorFindComponentByClass));
    return cls != nullptr && fn != nullptr ? fn(actor, &cls) : nullptr;
}

void* find_component(const void* actor, ue::reflect::StaticClassFn cls) {
    return find_component_of(actor, cls());
}

} // namespace ue::actor
