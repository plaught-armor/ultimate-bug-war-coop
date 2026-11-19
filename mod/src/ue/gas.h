#pragma once

// UE Gameplay Ability System: the ASC, ability spec, attribute and actor-info layout, and the engine
// function pointers that read them (split S4a). The pointers are resolved by the kCallsUe table in ue/hooktable.cpp.

#include "ue/reflect.h"

#include <stddef.h>
#include <stdint.h>

namespace ue::gas {

// FGameplayAttribute (0x38; layout from its FProperty* ctor 0x049B3AE0 and GetUProperty 0x049CDBF0):
// AttributeName FString, TFieldPath<FProperty> Attribute (ResolvedField +0x10, ResolvedOwner weak ptr +0x18, Path
// TArray<FName> +0x20), AttributeOwner +0x30. GetUProperty returns ResolvedField while the weak owner is not stale;
// a zero weak pointer is null, not stale. operator== (0x049B9590) compares the weak owner and Path, not ResolvedField,
// so a key for the ASC's attribute-change delegate map must come from the ctor, not be filled by hand.
struct GameplayAttributeRaw {
    ue::reflect::FStringRaw name;
    void* field;             // ResolvedField (the FProperty)
    int32_t owner_index;     // ResolvedOwner: FWeakObjectPtr, zero = null
    int32_t owner_serial;
    ue::reflect::FStringRaw path; // TArray<FName>, empty (data, num, max like FString)
    void* owner;             // AttributeOwner: the attribute set class
};
static_assert(sizeof(GameplayAttributeRaw) == 0x38, "FGameplayAttribute is 0x38 bytes");

// --- Engine function types and pointers (defined as nullptr in gas.cpp, resolved by kCallsUe) --------------

using GetAscFn = void* (*)(const void* actor, bool look_for_component);
using FindSpecFromClassFn = void* (*)(const void* asc, void* const* ability_class);
using SetNumericAttributeBaseFn = void (*)(void* asc, const GameplayAttributeRaw* attribute, float value);

extern GetAscFn g_get_asc;
extern FindSpecFromClassFn g_find_spec_from_class;
extern SetNumericAttributeBaseFn g_set_numeric_attribute_base;

// --- UAbilitySystemComponent, FGameplayAbilitySpec -------------------------------------------------------

inline constexpr size_t kAscSpecs = 0x508;             // ActivatableAbilities.Items data (Num at kAscAbilityNum)
inline constexpr size_t kAscAbilityNum = 0x510;
inline constexpr size_t kAscSpawnedAttributes = 0x10A8;
inline constexpr size_t kAscSpawnedAttributesNum = 0x10B0;
inline constexpr size_t kSpecHandle = 0xC;
inline constexpr size_t kSpecAbility = 0x10;
inline constexpr size_t kSpecActiveCount = 0x28;       // uint8 ActiveCount
inline constexpr size_t kSpecFlags = 0x29;             // bit 0 InputPressed (inferred), bit 2 PendingRemove
inline constexpr size_t kSpecBytes = 0xF0;

// --- FGameplayAttributeData, FOnAttributeChangeData ------------------------------------------------------

inline constexpr size_t kAttrDataBase = 0x8;           // FGameplayAttributeData::BaseValue
inline constexpr size_t kAttrDataCurrent = 0xC;        // FGameplayAttributeData::CurrentValue
inline constexpr uint64_t kCpfNet = 0x20;              // CPF_Net in FProperty::PropertyFlags
inline constexpr size_t kAttributeChangeNew = 0x38;    // FOnAttributeChangeData NewValue
inline constexpr size_t kAttributeChangeOld = 0x3C;    // FOnAttributeChangeData OldValue

// --- UGameplayAbility, FGameplayAbilityActorInfo --------------------------------------------------------

inline constexpr size_t kAbilityActorInfo = 0x328;     // UGameplayAbility::CurrentActorInfo
inline constexpr size_t kActorInfoAvatar = 0x10;       // FGameplayAbilityActorInfo::AvatarActor (weak)

// The AvatarActor of an FGameplayAbilityActorInfo (null for a null info).
void* avatar_of(const void* actor_info);

} // namespace ue::gas
