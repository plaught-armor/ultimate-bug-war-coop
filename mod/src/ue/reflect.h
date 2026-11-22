#pragma once

// UE reflection: UObject/FName/FProperty layout, the engine function pointers that read it, and the
// property walker (split S3). The pointers are resolved by the kCallsUe table in ue/hooktable.cpp.

#include <stddef.h>
#include <stdint.h>

namespace ue::reflect {

// --- Engine function types and pointers (defined as nullptr in reflect.cpp, resolved by kCallsUe) ---------

using StaticClassFn = void* (*)();
using ObjectPtrIsAFn = bool (*)(void* const* object_ptr, const void* cls);
using SetRootFlagsFn = bool (*)(void* item, int32_t flags); // FUObjectItem::SetRootFlags(EInternalObjectFlags)
using WeakGetFn = void* (*)(const void* weak_ptr);
using WeakAssignFn = void (*)(void* weak, void* const* object);
using GetNameEntryFn = const uint8_t* (*)(const void* fname);
using FNameCtorFn = void* (*)(void* self, const wchar_t* name, int32_t find_type);
using FindFunctionFn = void* (*)(const void* object, uint64_t name);
using ProcessEventFn = void (*)(void* object, void* function, void* parms);
using ExecThunkFn = void (*)(void* context, void* frame, void* result);
using StaticFindObjectFn = void* (*)(void* cls, void* outer, const wchar_t* name, bool exact);
using FindFirstObjectFn = void* (*)(void* cls, const wchar_t* name, uint32_t options, uint32_t verbosity,
                                    const wchar_t* operation);
using StaticLoadObjectFn = void* (*)(void* cls, void* outer, const wchar_t* name, const wchar_t* filename,
                                     uint32_t flags, void* sandbox, bool reconcile, const void* instancing);
using MemoryMallocFn = void* (*)(size_t count, uint32_t alignment);
using MemoryReallocFn = void* (*)(void* block, size_t count, uint32_t alignment);
using MemoryFreeFn = void (*)(void* block);

extern StaticClassFn g_uclass_class;             // UClass
extern ObjectPtrIsAFn g_object_ptr_is_a;         // FObjectPtr::IsA, called
extern SetRootFlagsFn g_set_root_flags;          // FUObjectItem::SetRootFlags, called (card 496)
extern WeakGetFn g_weak_get;
extern WeakAssignFn g_weak_assign;
extern GetNameEntryFn g_name_entry;              // FName::GetDisplayNameEntry
extern FNameCtorFn g_fname_ctor;
extern FindFunctionFn g_find_function_checked;
extern FindFunctionFn g_find_function;           // UObject::FindFunction: null when absent
extern StaticFindObjectFn g_static_find_object;
extern FindFirstObjectFn g_find_first_object;
extern StaticLoadObjectFn g_static_load_object;
extern MemoryMallocFn g_memory_malloc;
extern MemoryReallocFn g_memory_realloc;
extern MemoryFreeFn g_memory_free;

// --- Engine layout --------------------------------------------------------------------------------------

inline constexpr size_t kObjectFlags = 0x8;           // UObjectBase::ObjectFlags (EObjectFlags, int32)
inline constexpr int32_t kObjectFlagCdo = 0x10;       // RF_ClassDefaultObject
inline constexpr size_t kObjectIndex = 0xC;           // UObjectBase::InternalIndex
inline constexpr size_t kObjectClass = 0x10;          // UObjectBase::ClassPrivate
inline constexpr size_t kObjectName = 0x18;           // UObjectBase::NamePrivate (UObjectBase::GetFName)
inline constexpr size_t kObjectOuter = 0x20;          // UObjectBase::OuterPrivate (ApplyUIInputConfig+0xEB)
inline constexpr size_t kVtProcessEvent = 0x260;
inline constexpr size_t kStructSuper = 0x40;          // UStruct::SuperStruct
inline constexpr size_t kStructChildProperties = 0x50;
inline constexpr size_t kStructChildren = 0x48;       // UStruct::Children (UField*: the UFunctions; card 067)
inline constexpr size_t kUFieldNext = 0x28;           // UField::Next (UObjectBase is 0x28 bytes)
inline constexpr size_t kClassDefaultObject = 0x110;  // UClass::ClassDefaultObject (UClass::GetDefaultObject +0x6, +0x1C)
inline constexpr size_t kFunctionParmsSize = 0xB6;    // UFunction::ParmsSize (uint16; Func at +0xD8 per Invoke+0x73)
inline constexpr size_t kFrameNode = 0x10;            // FFrame::Node (Object +0x18 and Code +0x20 per execSetActorEnableCollision)
inline constexpr size_t kFramePrevious = 0x78;      // FFrame::PreviousFrame (FFrame::GetStackTrace: mov rbx, [rbx+0x78])
inline constexpr size_t kFieldClassPrivate = 0x8;     // FField::ClassPrivate; FFieldClass starts with its FName
inline constexpr size_t kFieldNext = 0x18;
inline constexpr size_t kFieldName = 0x20;
inline constexpr size_t kPropertyElementSize = 0x34;
inline constexpr size_t kPropertyFlags = 0x38;
inline constexpr size_t kPropertyOffset = 0x44;
inline constexpr size_t kStructPropStruct = 0x70;
inline constexpr size_t kArrayPropInner = 0x78;
inline constexpr size_t kMapPropValue = 0x78;
inline constexpr size_t kMapValueOffset = 0x80;
inline constexpr size_t kMapSparseSize = 0x94;
inline constexpr size_t kBoolFieldSize = 0x70;
inline constexpr size_t kBoolByteOffset = 0x71;
inline constexpr size_t kBoolFieldMask = 0x73;
inline constexpr size_t kArrayData = 0x0;
inline constexpr size_t kArrayNum = 0x8;
inline constexpr size_t kArrayMax = 0xC;
inline constexpr size_t kScriptDelegateBytes = 0x10;  // FScriptDelegate: FWeakObjectPtr (8), FName FunctionName (8)
inline constexpr size_t kScriptDelegateName = 0x8;
inline constexpr size_t kFrameObject = 0x18;
inline constexpr int32_t kFNameAdd = 1;               // EFindName::FNAME_Add

// --- FString reading ------------------------------------------------------------------------------------
// UE FString = TArray<TCHAR>: {TCHAR* Data; int32 ArrayNum; int32 ArrayMax}, ArrayNum counts the
// NUL. Not verifiable from this PDB (type streams stripped); same TArray {ptr @0, int32 num @8}
// shape the exe itself uses for UEngine::WorldList (see SetClientTravel's prologue).
struct FStringRaw {
    const wchar_t* data;
    int32_t num;
    int32_t max;
};
inline constexpr int32_t kMaxLoggedFStringNum = 1024;

// Logs "<what>: \"text\"" for a non-empty FString; logs nothing when it is empty.
void log_fstring(const char* what, const void* fstring);

// --- Raw reads ------------------------------------------------------------------------------------------

// Card 405: inline. Every actor walk reads a few fields per actor through these; as out-of-line calls (no LTO) read_i32
// alone was 294 of the client's 1043 mod CPU samples in run c404b95b.
inline void* read_ptr(const void* base, size_t offset) {
    void* v = nullptr;
    __builtin_memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}
inline int32_t read_i32(const void* base, size_t offset) {
    int32_t v = 0;
    __builtin_memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}
inline float read_f32(const void* base, size_t offset) {
    float v = 0.0f;
    __builtin_memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}
inline uint64_t read_u64(const void* base, size_t offset) {
    uint64_t v = 0;
    __builtin_memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}

// ArrayNum of the TArray at self + array_offset.
int32_t array_num(const uint8_t* self, size_t array_offset);

// View of a TSparseArray (TSet storage): {TArray Data; TBitArray AllocationFlags (4 inline
// dwords @+0x10, secondary dword* @+0x20, NumBits @+0x28)}, the layout the exe iterates.
struct SparseView {
    const uint8_t* data;
    const uint32_t* bits;
    int32_t count;
};

// Fills `out` for the sparse array at `sparse`; false (logged) when its header is implausible for `cap`.
bool sparse_view(const uint8_t* sparse, int32_t cap, SparseView& out);
bool sparse_allocated(const SparseView& v, int32_t i);

// --- Names and objects ----------------------------------------------------------------------------------

inline constexpr uint32_t kNameChars = 64;

struct ObjectName {
    char text[kNameChars];
};

// Card 238: the FNameEntry for `fname`, or nullptr (counted) when the pool is not ready or the entry
// with its characters is not inside the pool's written bytes. Every reader of an FNameEntry goes through it.
const uint8_t* fname_entry_checked(const void* fname);
// FNames refused by fname_entry_checked since start.
uint32_t fname_refusals();
// An FName as printable ASCII (others become '?'), truncated to kNameChars - 1; "?" when refused.
ObjectName fname_text(const void* fname);
ObjectName object_name(const void* object);
ObjectName class_name(const void* object);
// Card 608: the FName of a live `object` (0 for null), for a row printed later, when the object may be freed. Print
// the copy with fname_text; the row's pointer stays a compare key, so a reused address merges two objects in it.
inline uint64_t object_fname(const void* object) {
    return object != nullptr ? read_u64(object, kObjectName) : 0;
}

// The FName for `text`, built once through g_fname_ctor and kept in `cache`; 0 until g_fname_ctor resolves.
uint64_t fname_cached(uint64_t& cache, const wchar_t* text);

// UObjectBase::InternalIndex, or -1 for null.
int32_t object_index(const void* object);

bool is_a(const void* object, const void* cls);
// Card 476: true only when `object` is a live entry of GUObjectArray: the item at its InternalIndex (range-checked
// first, so a stale pointer's index is safe) points back at it and is not flagged unreachable or garbage. The same
// lookup and flag test as FWeakObjectPtr::Get (the function c31_weak_get calls). A pointer kept or handed over from
// another frame goes through this before is_a or any other read through its class.
bool object_listed(const void* object);
// Card 476 (b179): true while `object` is still the GUObjectArray entry at its InternalIndex, flagged unreachable or
// garbage or not: false only when it is really freed (index out of range, or the entry no longer points at it).
bool object_present(const void* object);
// Card 594: the GUObjectArray serial number of `object` (0 while nothing ever weakly referenced it), or -1 when
// `object` is no live entry (not listed, or flagged unreachable or garbage). With the index, it names one object for
// its whole life: a later object at the same address and index gets a new serial once it is weakly referenced.
int32_t object_serial(const void* object);
// Card 496: UObject::AddToRoot: FUObjectItem::SetRootFlags(RootSet) on `object`'s GUObjectArray item, which adds the
// item to the GC root list (GC marks roots from that list, not from the flag), so GC keeps it. `flags_out` gets the
// item's flags word after the call. False when `object` is no live entry (not listed, or flagged unreachable or
// garbage) or SetRootFlags is unresolved; nothing is written then.
bool object_add_root(const void* object, int32_t* flags_out);

// The UClass at `path` via StaticFindObject, or null.
void* find_class(const wchar_t* path);

// Card 220 (b187): the live objects whose class is exactly `cls` (GetAllWidgetsOfClass without the subclass test),
// the class default object left out; the first `cap` go to `out`. Returns how many there are (may exceed cap). A full
// GUObjectArray walk: diagnostics only, a few calls per map.
// subclasses: also objects whose class derives from cls (is_a); class default objects are never listed.
int32_t objects_of_class(const void* cls, const void** out, int32_t cap, bool subclasses = false);

// Card 252 step 2: one GUObjectArray walk counting the live objects (class default objects included, gone items not)
// and, per entry of `classes` (exact class pointer, null entries never match), the objects of that class into
// `counts`. Returns the live object count. A full walk: diagnostics only, once per 30 s window behind UBWCOOP_C132_FX.
int32_t objects_census(const void* const* classes, int32_t n_classes, int32_t* counts);

// --- Property walker ------------------------------------------------------------------------------------

inline constexpr int kPropMaxSupers = 32;
inline constexpr int kPropMaxFields = 512; // per struct

enum class PropKind : uint8_t { Other, Struct, Array, Map, Set, Object, Bool, Float, Double, SInt, UInt, Name, Str };

// The kind of FProperty `field`; its FFieldClass name in `name`.
PropKind prop_kind(const void* field, ObjectName& name);
bool prop_kind_is(const void* field, const char* kind);
bool prop_is_leaf(PropKind k);

// The property `name` of `ustruct` or a super, or null.
const uint8_t* prop_find_prop(const void* ustruct, const char* name);

bool prop_bool_ok(const uint8_t* field);

// An FName value with its number suffix (FName is 8 bytes here: ComparisonIndex, Number).
ObjectName prop_name(const uint8_t* addr, int32_t size);

// The object field `name` of `object` (reflected, ObjectProperty-like), its property in *prop. Null when absent.
const void* prop_object_field(const void* object, const char* name, const uint8_t** prop);

} // namespace ue::reflect
