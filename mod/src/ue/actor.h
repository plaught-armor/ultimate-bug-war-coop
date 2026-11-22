#pragma once

// UE actors, components, movement, world and spawning: the engine function pointers, their
// typedefs, the layout offsets and the small helpers that read them (split S4a). The pointers are
// resolved by the kCallsUe table in ue/hooktable.cpp.

#include "ue/reflect.h"

#include <stddef.h>
#include <stdint.h>

namespace ue::actor {

inline constexpr double kRadToDeg = 57.29577951308232;

struct Vec3 {
    double x, y, z; // also FRotator as pitch, yaw, roll
};

struct ViewPoint {
    Vec3 loc;
    Vec3 dir;
};

// --- Engine function types and pointers (defined as nullptr in actor.cpp, resolved by kCallsUe) ------------

using GetWorldFn = void* (*)(const void* actor);
using AttachParentFn = void* (*)(const void* actor);
using ConstBoolFn = bool (*)(const void* self);
using ThreadCheckFn = bool (*)(); // IsInGameThread
using SetActorLocationFn = bool (*)(void* actor, const Vec3* location, bool sweep, void* out_hit, uint8_t teleport);
// FVector and FRotator by value: over 8 bytes, so the x64 ABI passes each as a pointer to the caller's copy.
using SetActorLocationAndRotationFn = bool (*)(void* actor, const Vec3* location, const Vec3* rotation, bool sweep,
                                               void* out_hit, uint8_t teleport);
using SetHiddenInGameFn = void (*)(void* component, bool hidden, uint8_t propagation);
using SplineLengthFn = float (*)(const void* spline);
using ActorFloatFn = float (*)(const void* actor);
using AddComponentByClassFn = void* (*)(void* actor, void* const* cls, bool manual, const void* transform, bool deferred);
using FinishAddComponentFn = void (*)(void* actor, void* component, bool manual, const void* transform);
using FindComponentFn = void* (*)(const void* actor, void* const* cls);
using QueryParamsCtorFn = void* (*)(void* params, uint64_t trace_tag, const void* stat_id, bool complex,
                                    const void* ignore_actor);
using LineTraceByProfileFn = bool (*)(const void* world, void* hit, const Vec3* start, const Vec3* end,
                                      uint64_t profile, const void* params);
// UWorld::LineTraceSingleByChannel (card 264): channel is ECollisionChannel by value; response a
// FCollisionResponseParams (FCollisionResponseParams::DefaultResponseParam for the channel's own responses).
using LineTraceByChannelFn = bool (*)(const void* world, void* hit, const Vec3* start, const Vec3* end,
                                      int32_t channel, const void* params, const void* response);
inline constexpr int32_t kEccWorldStatic = 0; // ECollisionChannel::ECC_WorldStatic
inline constexpr int32_t kEccVisibility = 3; // ECollisionChannel::ECC_Visibility
using SpawnParamsCtorFn = void* (*)(void* params);
using WorldSpawnActorFn = void* (*)(void* world, void* cls, const Vec3* loc, const Vec3* rot, const void* params);
using SpawnAiFromClassFn = void* (*)(void* world_context, void* const* pawn_class, void* behavior_tree,
                                     const Vec3* location, const Vec3* rotation, bool no_collision_fail, void* owner);
using SpawnNiagaraFn = void* (*)(const void* context, void* system, const Vec3* location, const Vec3* rotation,
                                 const Vec3* scale, bool auto_destroy, bool auto_activate, int32_t pool, bool pre_cull);
using RecentlyRenderedFn = bool (*)(const void* actor, float tolerance); // AActor::WasRecentlyRendered
using GetSubsystemFn = void* (*)(const void* outer); // the subsystem's owner: a local player or the engine
using GameInstanceRefFn = void (*)(void* game_instance, void* object); // UGameInstance::(Un)RegisterReferencedObject

extern GetWorldFn g_actor_get_world;
extern AttachParentFn g_get_attach_parent_actor;
extern GameInstanceRefFn g_gi_register_ref;
extern GameInstanceRefFn g_gi_unregister_ref;
extern ConstBoolFn g_world_is_paused;
extern ThreadCheckFn g_is_in_game_thread;
extern SetActorLocationFn g_set_actor_location;
extern SetActorLocationAndRotationFn g_set_actor_location_rotation;
extern SetHiddenInGameFn g_set_hidden_in_game;
extern SetHiddenInGameFn g_scene_set_visibility; // same shape: (component, visible, propagation)
extern SplineLengthFn g_spline_length; // USplineComponent::GetSplineLength, card 159
extern ActorFloatFn g_life_span;
extern AddComponentByClassFn g_add_component_by_class;
extern FinishAddComponentFn g_finish_add_component;
extern QueryParamsCtorFn g_query_params_ctor;
extern LineTraceByProfileFn g_line_trace_by_profile;
extern LineTraceByChannelFn g_line_trace_by_channel;
extern SpawnParamsCtorFn g_spawn_params_ctor;
extern WorldSpawnActorFn g_world_spawn_actor;
extern RecentlyRenderedFn g_actor_recently_rendered; // card 169
extern SpawnAiFromClassFn g_spawn_ai_from_class;
extern SpawnNiagaraFn g_spawn_niagara;
extern GetSubsystemFn g_get_data_table_subsystem;

// --- AActor, APawn, AController, APlayerController -------------------------------------------------------

inline constexpr size_t kActorCollisionFlags = 0x5D;   // bit 0 bActorEnableCollision (UPrimitiveComponent::GetCollisionEnabled)
inline constexpr uint8_t kActorEnableCollisionBit = 0x1;
inline constexpr size_t kActorOwner = 0x158;           // AActor::Owner (AActor::GetNetConnection)
inline constexpr size_t kActorInstigator = 0x1A0;      // AActor::Instigator (AActor::GetInstigator)
inline constexpr size_t kActorRootComponent = 0x1B8;   // AActor::execK2_GetActorLocation
inline constexpr size_t kActorFindComponentByClass = 0x720; // AActor vtable slot (USTSpriteVariantAnimatorComponent::BeginPlay)
inline constexpr size_t kPawnController = 0x2D8;       // APawn::Controller (APawn::PossessedBy)
inline constexpr size_t kPawnPlayerState = 0x2C8;      // APawn::PlayerState (APawn::SetPlayerState); replicated to all, null when unpossessed
inline constexpr size_t kControllerPawn = 0x2E8;       // AController::Pawn (AController::SetPawn stores it)
inline constexpr size_t kPcCursorFlags = 0x54C;        // bitfield, bit 0 = bShowMouseCursor (SetShowMouseCursor)

// --- UActorComponent, USceneComponent, UPrimitiveComponent, FTransform -----------------------------------

inline constexpr size_t kComponentOwner = 0xA0;        // UActorComponent::OwnerPrivate (GetCollisionEnabled)
inline constexpr size_t kComponentRotation = 0x1E0;    // ComponentToWorld quat X,Y,Z,W (execK2_GetActorRotation)
inline constexpr size_t kComponentTranslation = 0x200; // ComponentToWorld translation (execK2_GetActorLocation)
inline constexpr size_t kBodyCollisionEnabled = 0x38F; // BodyInstance (+0x378).CollisionEnabled (GetCollisionEnabled)
inline constexpr size_t kBodyObjectType = 0x38D; // BodyInstance.ObjectType, ECollisionChannel byte (GetCollisionObjectType)
inline constexpr size_t kBodyResponses = 0x3E0;  // BodyInstance responses, one ECollisionResponse byte per channel (GetCollisionResponseToChannel)
inline constexpr size_t kTransformTranslation = 0x20;  // FTransform<double>: quat +0, translation +0x20, scale +0x40

// --- ACharacter, UCharacterMovementComponent -------------------------------------------------------------

// Was read as 0x328, which is ACharacter::Mesh (ACharacter::PossessedBy tests it, then ORs
// Mesh+0xA81 bOnlyAllowAutonomousTickPose). CharacterMovement is what
// ACharacter::GetMovementComponent (0x039B19B0) returns: `mov rax, [rcx+0x330]; ret`.
inline constexpr size_t kCharacterMovement = 0x330;
inline constexpr size_t kCmcCharacterOwner = 0x198;    // UCharacterMovementComponent::SmoothClientPosition+0x27
inline constexpr size_t kCmcSmoothingMode = 0x233;     // byte SmoothClientPosition+0x1E tests (NetworkSmoothingMode)
// Card 524: UCharacterMovementComponent::ClientPredictionData (FNetworkPredictionData_Client_Character*, null until
// made), and in it MeshTranslationOffset (FVector), which SmoothCorrection grows and SmoothClientPosition decays;
// SmoothClientPosition_UpdateVisuals reads both +0x80 and the character's BaseTranslationOffset.
inline constexpr size_t kCmcClientData = 0x7B0;
inline constexpr size_t kClientMeshTranslationOffset = 0x80;
inline constexpr size_t kCharacterBaseTranslation = 0x400; // ACharacter::BaseTranslationOffset (execGetBaseTranslationOffset)
inline constexpr size_t kCompRelativeLocation = 0x158;     // USceneComponent::RelativeLocation (K2_SetRelativeLocation+0x37)
inline constexpr size_t kMoveUpdatedComponent = 0xB8;  // UMovementComponent::UpdatedComponent (UpdateComponentVelocity)
inline constexpr size_t kMoveVelocity = 0xD0;          // FVector Velocity (UpdateComponentVelocity)
inline constexpr size_t kCmcMovementMode = 0x231;      // uint8 MovementMode (GetMaxSpeed switch)
inline constexpr size_t kCmcMaxWalkSpeed = 0x278;      // float (GetMaxSpeed, walking/falling)
inline constexpr size_t kCmcMaxAcceleration = 0x28C;   // float (GetMaxAcceleration)
inline constexpr size_t kCmcAcceleration = 0x328;      // FVector Acceleration (PhysWalking zeroes it with Velocity)
inline constexpr size_t kCmcCurrentFloorFlags = 0x438; // FFindFloorResult bits (MoveAlongFloor+0x1E): 1 blocking, 2 walkable
inline constexpr size_t kCmcIgnoreClientErrors = 0x555; // bit 1: ServerCheckClientError+0x4 returns false when set
inline constexpr uint8_t kCmcIgnoreClientErrorsBit = 0x2;
inline constexpr size_t kVtSetMovementMode = 0x638; // UCharacterMovementComponent::SetMovementMode(EMovementMode, uint8)
inline constexpr uint8_t kMoveWalking = 1;          // EMovementMode::MOVE_Walking
inline constexpr uint8_t kMoveNavWalking = 2;       // EMovementMode::MOVE_NavWalking
inline constexpr uint8_t kMoveFalling = 3;          // EMovementMode::MOVE_Falling (a jump, a step off a ledge, a fall)
inline constexpr uint8_t kMoveFlying = 5;           // EMovementMode::MOVE_Flying (the kill floor's mode)

// --- FHitResult ------------------------------------------------------------------------------------------

inline constexpr size_t kHitLocation = 0x10;           // FVector Location
inline constexpr size_t kHitFlags = 0xAD;              // bit 0 bBlockingHit, bit 1 bStartPenetrating (SafeMove+0x150)
inline constexpr uint8_t kHitBlocking = 0x1;
inline constexpr uint8_t kHitStartPenetrating = 0x2;
inline constexpr size_t kHitSize = 0x100;          // FHitResult, as the original zeroes it
inline constexpr size_t kHitTime = 0x4;            // float Time = 1
inline constexpr size_t kHitItem = 0xA4;           // int32 Item = -1 (the original's init)
inline constexpr size_t kHitMyItem = 0xD0;         // int32 MyItem = -1 (the original's +0xC8 block, +8)

// --- UWorld, ULevel, UDataTable, actor spawning ----------------------------------------------------------

inline constexpr size_t kWorldLevels = 0x1C8;          // TArray<ULevel*>; Num at +0x8
inline constexpr size_t kLevelActors = 0xA0;           // TArray<AActor*>; Num at +0x8
inline constexpr int32_t kMaxLevels = 256;
inline constexpr int32_t kMaxLevelActors = 65536;      // per level
inline constexpr uint8_t kTeleportPhysics = 1;         // ETeleportType::TeleportPhysics (K2_SetActorLocation bTeleport = true)
inline constexpr size_t kRowMapElement = 0x18;
inline constexpr size_t kSpawnParamsSize = 0x70;       // FActorSpawnParameters (ctor exe+0x432DD30)
inline constexpr size_t kSpawnCollisionOverride = 0x30; // ESpawnActorCollisionHandlingMethod, ctor sets 0
inline constexpr uint8_t kSpawnAdjustAlways = 2;       // AdjustIfPossibleButAlwaysSpawn (what CheatSpawn sets)
inline constexpr size_t kSpawnPreSpawnFn = 0x40;       // TFunction CustomPreSpawnInitalization storage; 0 = empty

// --- Helpers ---------------------------------------------------------------------------------------------

Vec3 read_vec3(const void* base, size_t offset);
double vec_len(const Vec3& v);
double vec_dist(const Vec3& a, const Vec3& b);
// Pitch and yaw (degrees) of a quat's forward axis; quat X, Y, Z, W at `q`.
Vec3 quat_pitch_yaw(const double* q);
Vec3 component_pitch_yaw(const void* component);
Vec3 actor_location(const void* actor);
// The owner's first component of class `cls` (or a subclass), through AActor::FindComponentByClass.
void* find_component(const void* actor, ue::reflect::StaticClassFn cls);
// As find_component, for a UClass already in hand (from find_class); null when `cls` is null.
void* find_component_of(const void* actor, void* cls);

} // namespace ue::actor
