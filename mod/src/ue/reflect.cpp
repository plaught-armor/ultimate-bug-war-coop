#include "ue/reflect.h"

#include "ue/hooktable.h"
#include "ue/log.h"

#include <stdio.h>
#include <windows.h>
#include <string.h>

namespace ue::reflect {

namespace log = ue::log;

StaticClassFn g_uclass_class = nullptr; // UClass
ObjectPtrIsAFn g_object_ptr_is_a = nullptr;     // FObjectPtr::IsA, called
SetRootFlagsFn g_set_root_flags = nullptr;      // FUObjectItem::SetRootFlags, called (card 496)
WeakGetFn g_weak_get = nullptr;
WeakAssignFn g_weak_assign = nullptr;
GetNameEntryFn g_name_entry = nullptr;                   // FName::GetDisplayNameEntry
FNameCtorFn g_fname_ctor = nullptr;
FindFunctionFn g_find_function_checked = nullptr;
FindFunctionFn g_find_function = nullptr; // UObject::FindFunction: null when absent
StaticFindObjectFn g_static_find_object = nullptr;
FindFirstObjectFn g_find_first_object = nullptr;
StaticLoadObjectFn g_static_load_object = nullptr;
MemoryMallocFn g_memory_malloc = nullptr;
MemoryReallocFn g_memory_realloc = nullptr;
MemoryFreeFn g_memory_free = nullptr;

namespace {

struct PropKindRow {
    const char* name; // FFieldClass name
    PropKind kind;
};
constexpr PropKindRow kPropKinds[] = {
    {"StructProperty", PropKind::Struct},  {"ArrayProperty", PropKind::Array},       {"MapProperty", PropKind::Map},
    {"SetProperty", PropKind::Set},        {"ObjectProperty", PropKind::Object},     {"ClassProperty", PropKind::Object},
    {"ObjectPtrProperty", PropKind::Object}, {"ClassPtrProperty", PropKind::Object}, {"InterfaceProperty", PropKind::Object},
    {"BoolProperty", PropKind::Bool},      {"FloatProperty", PropKind::Float},       {"DoubleProperty", PropKind::Double},
    {"Int8Property", PropKind::SInt},      {"Int16Property", PropKind::SInt},        {"IntProperty", PropKind::SInt},
    {"Int64Property", PropKind::SInt},     {"ByteProperty", PropKind::UInt},         {"UInt16Property", PropKind::UInt},
    {"UInt32Property", PropKind::UInt},    {"UInt64Property", PropKind::UInt},       {"EnumProperty", PropKind::UInt},
    {"NameProperty", PropKind::Name},      {"StrProperty", PropKind::Str},
};

} // namespace

// Logs "<what>: \"text\"" for a non-empty FString; logs nothing when it is empty.
void log_fstring(const char* what, const void* fstring) {
    if (fstring == nullptr) {
        log::line("%s: <null FString*>", what);
        return;
    }
    const auto* s = static_cast<const FStringRaw*>(fstring);
    if (s->num == 0 || (s->num == 1 && s->data != nullptr)) {
        return; // empty
    }
    if (s->data == nullptr || s->num < 0 || s->num > kMaxLoggedFStringNum) {
        log::line("%s: <unreadable FString data=%p num=%d>", what, static_cast<const void*>(s->data), s->num);
        return;
    }
    log::line("%s: \"%.*ls\"", what, static_cast<int>(s->num - 1), s->data);
}

int32_t array_num(const uint8_t* self, size_t array_offset) {
    int32_t n = 0;
    memcpy(&n, self + array_offset + kArrayNum, sizeof(n));
    return n;
}

bool sparse_view(const uint8_t* sparse, int32_t cap, SparseView& out) {
    const uint8_t* const data = *reinterpret_cast<const uint8_t* const*>(sparse);
    const int32_t num = *reinterpret_cast<const int32_t*>(sparse + 0x8);
    const uint32_t* const secondary = *reinterpret_cast<const uint32_t* const*>(sparse + 0x20);
    const int32_t bits = *reinterpret_cast<const int32_t*>(sparse + 0x28);
    constexpr int32_t kInlineBits = 4 * 32; // TBitArray's inline words; more bits need the secondary
    if (bits < 0 || bits > num || bits > cap || (bits > 0 && data == nullptr) ||
        (bits > kInlineBits && secondary == nullptr)) {
        log::line("ERROR client-input: implausible sparse array at %p (num=%d bits=%d cap=%d)",
                  static_cast<const void*>(sparse), num, bits, cap);
        return false;
    }
    out.data = data;
    out.bits = secondary != nullptr ? secondary : reinterpret_cast<const uint32_t*>(sparse + 0x10);
    out.count = bits;
    return true;
}

bool sparse_allocated(const SparseView& v, int32_t i) {
    return ((v.bits[i >> 5] >> (i & 31)) & 1u) != 0;
}

// Card 238: the FNamePool as FName::GetDisplayNameEntry reaches it. Its bytes are checked before use:
// cmp byte [rip+d], 0 (pool ready flag) at +6 and lea rdx, [rip+d] (the pool) at +0x11. Allocator layout
// (UE5 FNameEntryAllocator): CurrentBlock u32 +0x8, CurrentByteCursor u32 +0xC, Blocks[8192] +0x10;
// a block is 0x20000 bytes and an entry sits at Blocks[idx >> 16] + (idx & 0xFFFF) * 2.
namespace {
constexpr size_t kPoolCurrentBlock = 0x8;
constexpr size_t kPoolCursor = 0xC;
constexpr size_t kPoolBlocks = 0x10;
constexpr uint32_t kPoolMaxBlocks = 8192;
constexpr uint32_t kPoolBlockBytes = 0x20000;

struct NamePool {
    const uint8_t* ready; // bool, set once the pool is built
    const uint8_t* pool;  // FNamePool
    bool tried;
};
NamePool g_pool{};
uint32_t g_name_refusals = 0;

const uint8_t* rip_target(const uint8_t* insn, size_t disp_at, size_t insn_len) {
    int32_t disp = 0;
    memcpy(&disp, insn + disp_at, sizeof(disp));
    return insn + insn_len + disp;
}

void pool_resolve() {
    g_pool.tried = true;
    const auto* fn = reinterpret_cast<const uint8_t*>(g_name_entry);
    if (fn == nullptr) {
        return;
    }
    const bool shape = fn[6] == 0x80 && fn[7] == 0x3D && fn[12] == 0x00 && fn[0x11] == 0x48 &&
                       fn[0x12] == 0x8D && fn[0x13] == 0x15;
    if (!shape) {
        log::line("WARN card238: GetDisplayNameEntry bytes differ; FName pool guard off");
        return;
    }
    g_pool.ready = rip_target(fn + 6, 2, 7);
    g_pool.pool = rip_target(fn + 0x11, 3, 7);
    log::line("card238: FName pool at %p, ready flag at %p", static_cast<const void*>(g_pool.pool),
              static_cast<const void*>(g_pool.ready));
}

// The pool entry for `fname`, or nullptr when its index is not inside the pool's written bytes.
// The whole entry (header and characters) must end inside its block before the caller reads it.
const uint8_t* pool_entry_checked(const void* fname) {
    if (!g_pool.tried) {
        pool_resolve();
    }
    if (g_pool.pool == nullptr) {
        return g_name_entry(fname); // guard off (logged once): the old path
    }
    if (*g_pool.ready == 0) {
        return nullptr;
    }
    uint32_t idx = 0;
    uint32_t cur_block = 0;
    uint32_t cursor = 0;
    memcpy(&idx, fname, sizeof(idx));
    memcpy(&cur_block, g_pool.pool + kPoolCurrentBlock, sizeof(cur_block));
    memcpy(&cursor, g_pool.pool + kPoolCursor, sizeof(cursor));
    const uint32_t block = idx >> 16;
    const uint32_t offset = (idx & 0xFFFFu) * 2u;
    if (block > cur_block || cur_block >= kPoolMaxBlocks) {
        return nullptr;
    }
    const uint8_t* base = nullptr;
    memcpy(&base, g_pool.pool + kPoolBlocks + 8u * static_cast<size_t>(block), sizeof(base));
    const uint32_t limit = block == cur_block && cursor <= kPoolBlockBytes ? cursor : kPoolBlockBytes;
    if (base == nullptr || offset + 2u > limit) {
        return nullptr;
    }
    uint16_t header = 0;
    memcpy(&header, base + offset, sizeof(header));
    const uint32_t bytes = static_cast<uint32_t>(header >> 6) * ((header & 1u) != 0 ? 2u : 1u);
    return offset + 2u + bytes <= limit ? base + offset : nullptr;
}
} // namespace

const uint8_t* fname_entry_checked(const void* fname) {
    const uint8_t* const entry = fname != nullptr && g_name_entry != nullptr ? pool_entry_checked(fname) : nullptr;
    if (entry == nullptr) {
        ++g_name_refusals;
    }
    return entry;
}

uint32_t fname_refusals() {
    return g_name_refusals;
}

// An FName as printable ASCII (others become '?'), truncated to kNameChars - 1.
// FNameEntry layout from FNameEntry::GetPlainNameString: uint16 header at +0 (bit 0 = wide,
// length = header >> 6), then the characters at +2. The FName number suffix is not shown.
// Card 010: split out of object_name so a bare FName field (AController::StateName) can be shown.
// Card 238: an FName whose entry is not inside the name pool reads as "?" (fname_entry_checked).
ObjectName fname_text(const void* fname) {
    ObjectName out{};
    const uint8_t* const entry = fname_entry_checked(fname);
    if (entry == nullptr) {
        memcpy(out.text, "?", 2);
        return out;
    }
    uint16_t header = 0;
    memcpy(&header, entry, sizeof(header));
    const bool wide = (header & 1u) != 0;
    const uint32_t len = static_cast<uint32_t>(header >> 6);
    const uint32_t n = len < kNameChars - 1 ? len : kNameChars - 1;
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t c = entry[2 + i];
        if (wide) {
            memcpy(&c, entry + 2 + 2 * static_cast<size_t>(i), sizeof(c));
        }
        out.text[i] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    out.text[n] = '\0';
    return out;
}

ObjectName object_name(const void* object) {
    if (object == nullptr) {
        ObjectName out{};
        memcpy(out.text, "null", 5);
        return out;
    }
    return fname_text(static_cast<const uint8_t*>(object) + kObjectName);
}

ObjectName class_name(const void* object) {
    if (object == nullptr) {
        return object_name(nullptr);
    }
    return object_name(*reinterpret_cast<const void* const*>(static_cast<const uint8_t*>(object) + kObjectClass));
}

uint64_t fname_cached(uint64_t& cache, const wchar_t* text) {
    if (cache == 0 && g_fname_ctor != nullptr) {
        uint64_t n = 0;
        g_fname_ctor(&n, text, kFNameAdd);
        cache = n;
    }
    return cache;
}

int32_t object_index(const void* object) {
    return object != nullptr ? read_i32(object, kObjectIndex) : -1;
}

// Card 476: FUObjectArray as FWeakObjectPtr::Get (exe 0x15859A0) reads it: ObjObjects' chunk table at +0x10, its
// NumElements at +0x24, 64K items per chunk, FUObjectItem 0x18 bytes {Object, Flags, ..}, and the flag mask Get tests
// (EInternalObjectFlags Unreachable | Garbage). FWeakObjectPtr's own assign cannot take a stale pointer: it reads the
// index and writes a serial without a range check, so the lookup is done here with the check first.
constexpr size_t kObjArrayChunks = 0x10;
constexpr size_t kObjArrayNum = 0x24;
constexpr int32_t kObjChunkItems = 64 * 1024;
constexpr size_t kObjItemBytes = 0x18;
constexpr size_t kObjItemFlags = 0x8;
constexpr int32_t kObjItemGone = 0x10200000;

// The FUObjectItem whose Object is `object` (its InternalIndex in range, the chunk present, the item pointing back),
// or null: a freed object's index is out of range or reused.
static const uint8_t* object_item(const void* object) {
    const uint8_t* const base = ue::hooktable::g_exe_base;
    // Card 486: a field read off reused memory can hold any bits (c244b212a: 0x31F00000001). A UObject is 8-byte
    // aligned and in user space, so anything else is no object and is never read.
    const uintptr_t addr = reinterpret_cast<uintptr_t>(object);
    if (base == nullptr || (addr & 7u) != 0 || addr < 0x10000u || addr > 0x7FFFFFFFFFFFull) {
        return nullptr;
    }
    const uint8_t* const array = base + ubw::offsets::GUObjectArray.rva;
    const int32_t i = read_i32(object, kObjectIndex);
    if (i < 0 || i >= read_i32(array, kObjArrayNum)) {
        return nullptr;
    }
    const auto* const chunks = static_cast<const uint8_t* const*>(read_ptr(array, kObjArrayChunks));
    const uint8_t* const chunk = chunks != nullptr ? chunks[i / kObjChunkItems] : nullptr;
    if (chunk == nullptr) {
        return nullptr;
    }
    const uint8_t* const item = chunk + static_cast<size_t>(i % kObjChunkItems) * kObjItemBytes;
    return read_ptr(item, 0) == object ? item : nullptr;
}

bool object_listed(const void* object) {
    const uint8_t* const item = object_item(object);
    return item != nullptr && (read_i32(item, kObjItemFlags) & kObjItemGone) == 0;
}

bool object_present(const void* object) {
    return object_item(object) != nullptr;
}

// FUObjectItem::SerialNumber (+0x10): set on the first weak reference to the object, 0 until then.
constexpr size_t kObjItemSerial = 0x10;

int32_t object_serial(const void* object) {
    const uint8_t* const item = object_item(object);
    if (item == nullptr || (read_i32(item, kObjItemFlags) & kObjItemGone) != 0) {
        return -1;
    }
    return read_i32(item, kObjItemSerial);
}

// EInternalObjectFlags::RootSet: UObjectBase::AddObject (0x01569C40) sets bit 30 of the item flags (+0x8) for
// RF_MarkAsRootSet (btsl $0x1e at 0x01569C76), beside Native (bit 25) for RF_MarkAsNative.
constexpr int32_t kObjItemRootSet = 0x40000000;

bool object_add_root(const void* object, int32_t* flags_out) {
    const uint8_t* const item = object_item(object);
    if (item == nullptr || g_set_root_flags == nullptr || (read_i32(item, kObjItemFlags) & kObjItemGone) != 0) {
        return false;
    }
    // Card 496 (b246): this build is UE 5.5+, where GC marks roots from a root list, not from the flag; a bare or of
    // the flag (b245) left c247b245a's BP_BarkType_sawbughopper_C to be collected and loaded again. SetRootFlags
    // (0x0149F8E0, AddToRoot's path) takes the object array lock, adds the item to that list, then sets the flag.
    g_set_root_flags(const_cast<uint8_t*>(item), kObjItemRootSet);
    *flags_out = read_i32(item, kObjItemFlags);
    return true;
}

bool is_a(const void* object, const void* cls) {
    void* const slot = const_cast<void*>(object); // FObjectPtr::IsA reads through a pointer slot
    return object != nullptr && g_object_ptr_is_a(&slot, cls);
}

constexpr int32_t kObjScanMax = 8 * 1024 * 1024; // GUObjectArray items walked at most (MaxObjectsInGame is 2M here)

int32_t objects_of_class(const void* cls, const void** out, int32_t cap, bool subclasses) {
    const uint8_t* const base = ue::hooktable::g_exe_base;
    if (cls == nullptr || base == nullptr || out == nullptr) {
        return 0;
    }
    const uint8_t* const array = base + ubw::offsets::GUObjectArray.rva;
    const auto* const chunks = static_cast<const uint8_t* const*>(read_ptr(array, kObjArrayChunks));
    const int32_t num = read_i32(array, kObjArrayNum);
    if (chunks == nullptr || num <= 0) {
        return 0;
    }
    const void* const cdo = read_ptr(cls, kClassDefaultObject);
    int32_t n = 0;
    for (int32_t i = 0; i < num && i < kObjScanMax; ++i) {
        const uint8_t* const chunk = chunks[i / kObjChunkItems];
        if (chunk == nullptr) {
            i += kObjChunkItems - 1 - i % kObjChunkItems; // the rest of an absent chunk
            continue;
        }
        const uint8_t* const item = chunk + static_cast<size_t>(i % kObjChunkItems) * kObjItemBytes;
        const void* const object = read_ptr(item, 0);
        if (object == nullptr || object == cdo || (read_i32(item, kObjItemFlags) & kObjItemGone) != 0) {
            continue;
        }
        const bool match = subclasses ? (read_i32(object, kObjectFlags) & kObjectFlagCdo) == 0 && is_a(object, cls)
                                      : read_ptr(object, kObjectClass) == cls;
        if (!match) {
            continue;
        }
        if (n < cap) {
            out[n] = object;
        }
        ++n;
    }
    return n;
}

int32_t objects_census(const void* const* classes, int32_t n_classes, int32_t* counts) {
    const uint8_t* const base = ue::hooktable::g_exe_base;
    if (classes == nullptr || counts == nullptr || n_classes <= 0 || base == nullptr) {
        return 0;
    }
    for (int32_t k = 0; k < n_classes; ++k) {
        counts[k] = 0;
    }
    const uint8_t* const array = base + ubw::offsets::GUObjectArray.rva;
    const auto* const chunks = static_cast<const uint8_t* const*>(read_ptr(array, kObjArrayChunks));
    const int32_t num = read_i32(array, kObjArrayNum);
    if (chunks == nullptr || num <= 0) {
        return 0;
    }
    int32_t live = 0;
    for (int32_t i = 0; i < num && i < kObjScanMax; ++i) {
        const uint8_t* const chunk = chunks[i / kObjChunkItems];
        if (chunk == nullptr) {
            i += kObjChunkItems - 1 - i % kObjChunkItems; // the rest of an absent chunk
            continue;
        }
        const uint8_t* const item = chunk + static_cast<size_t>(i % kObjChunkItems) * kObjItemBytes;
        const void* const object = read_ptr(item, 0);
        if (object == nullptr || (read_i32(item, kObjItemFlags) & kObjItemGone) != 0) {
            continue;
        }
        ++live;
        const void* const cls = read_ptr(object, kObjectClass);
        for (int32_t k = 0; k < n_classes; ++k) {
            counts[k] += (cls == classes[k] && cls != nullptr) ? 1 : 0;
        }
    }
    return live;
}

void* find_class(const wchar_t* path) {
    return g_static_find_object != nullptr ? g_static_find_object(g_uclass_class(), nullptr, path, false) : nullptr;
}

PropKind prop_kind(const void* field, ObjectName& name) {
    name = fname_text(read_ptr(field, kFieldClassPrivate));
    for (const PropKindRow& r : kPropKinds) {
        if (strcmp(r.name, name.text) == 0) {
            return r.kind;
        }
    }
    return PropKind::Other;
}

bool prop_kind_is(const void* field, const char* kind) {
    return field != nullptr && strcmp(fname_text(read_ptr(field, kFieldClassPrivate)).text, kind) == 0;
}

bool prop_is_leaf(PropKind k) {
    return k != PropKind::Struct && k != PropKind::Array && k != PropKind::Map && k != PropKind::Set;
}

// The property `name` of `ustruct` or a super, or null.
// Card 411 diagnostic (UBWCOOP_DIAG=1, game thread callers): calls per name literal, the top rows every 2000 calls.
// A by-name walk costs a text compare per field of the class chain; a caller on a per-frame path should cache.
static void prop_find_count(const char* name) {
    constexpr int kNames = 96;
    static const char* names[kNames];
    static uint32_t counts[kNames];
    static uint32_t calls = 0;
    static int on = -1;
    if (on < 0) {
        char value[8];
        on = GetEnvironmentVariableA("UBWCOOP_DIAG", value, sizeof(value)) != 0 ? 1 : 0;
    }
    if (on == 0) {
        return;
    }
    int at = -1;
    for (int i = 0; i < kNames && at < 0; ++i) {
        if (names[i] == name || names[i] == nullptr) {
            at = i;
        }
    }
    if (at >= 0) {
        names[at] = name;
        ++counts[at];
    }
    if (++calls % 2000 != 0) {
        return;
    }
    char line[400];
    size_t n = 0;
    for (int row = 0; row < 6; ++row) {
        int best = -1;
        for (int i = 0; i < kNames && names[i] != nullptr; ++i) {
            if (counts[i] != 0 && (best < 0 || counts[i] > counts[best])) {
                best = i;
            }
        }
        if (best < 0 || n >= sizeof(line) - 64) {
            break;
        }
        n += static_cast<size_t>(snprintf(line + n, sizeof(line) - n, " %.40s %u,", names[best], counts[best]));
        counts[best] = 0;
    }
    for (int i = 0; i < kNames; ++i) {
        counts[i] = 0;
    }
    log::line("card411: prop_find_prop %u calls, top of the last 2000:%s", calls, line);
}

const uint8_t* prop_find_prop(const void* ustruct, const char* name) {
    prop_find_count(name);
    const void* s = ustruct;
    for (int depth = 0; depth < kPropMaxSupers && s != nullptr; ++depth, s = read_ptr(s, kStructSuper)) {
        const void* f = read_ptr(s, kStructChildProperties);
        for (int i = 0; i < kPropMaxFields && f != nullptr; ++i, f = read_ptr(f, kFieldNext)) {
            if (strcmp(fname_text(static_cast<const uint8_t*>(f) + kFieldName).text, name) == 0) {
                return static_cast<const uint8_t*>(f);
            }
        }
    }
    return nullptr;
}

bool prop_bool_ok(const uint8_t* field) {
    const int32_t size = read_i32(field, kPropertyElementSize);
    return field[kBoolFieldSize] == size && field[kBoolByteOffset] < size && field[kBoolFieldMask] != 0;
}

// An FName value with its number suffix (FName is 8 bytes here: ComparisonIndex, Number).
ObjectName prop_name(const uint8_t* addr, int32_t size) {
    ObjectName n = fname_text(addr);
    uint32_t number = 0;
    memcpy(&number, addr + 4, sizeof(number));
    const size_t len = strlen(n.text);
    if (size == 8 && number != 0) {
        snprintf(n.text + len, sizeof(n.text) - len, "_%u", number - 1);
    }
    return n;
}

// The object field `name` of `object` (reflected, ObjectProperty-like), its property in *prop. Null when absent.
const void* prop_object_field(const void* object, const char* name, const uint8_t** prop) {
    *prop = object != nullptr ? prop_find_prop(read_ptr(object, kObjectClass), name) : nullptr;
    ObjectName kind{};
    if (*prop == nullptr || prop_kind(*prop, kind) != PropKind::Object) {
        return nullptr;
    }
    return read_ptr(object, static_cast<size_t>(read_i32(*prop, kPropertyOffset)));
}

} // namespace ue::reflect
