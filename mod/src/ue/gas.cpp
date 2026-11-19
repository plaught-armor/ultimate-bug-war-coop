#include "ue/gas.h"

namespace ue::gas {

GetAscFn g_get_asc = nullptr;                          // UAbilitySystemGlobals::GetAbilitySystemComponentFromActor
FindSpecFromClassFn g_find_spec_from_class = nullptr; // UAbilitySystemComponent::FindAbilitySpecFromClass
SetNumericAttributeBaseFn g_set_numeric_attribute_base = nullptr;

void* avatar_of(const void* actor_info) {
    return actor_info != nullptr ? ue::reflect::g_weak_get(static_cast<const uint8_t*>(actor_info) + kActorInfoAvatar)
                                 : nullptr;
}

} // namespace ue::gas
