// Card 114: the difficulty values as an editable file, %LOCALAPPDATA%\StarshipTroopers_UBW\Saved\UBWCoop-Difficulty.ini.
//
// - Dump (both sides, first engine tick, once per launch): every field of each DT_DifficultyDataTable row, each
//   damage modifier preset CDO (one line per TagModifierSets entry, keyed by its Description text) and each pre-made
//   wave data CDO (WaveBudget and the two FSTBudgetMultipliers sets). The walk follows reflection on native structs
//   and classes only (a _C class's own properties are skipped), so every FProperty it keeps is permanent. Numbers
//   (float, double, int, bool, non-enum byte) are editable "key = value" lines; text, tags, references, enums and
//   maps are "; key = value" comments. The user file is written only when absent; UBWCoop-Difficulty.defaults.ini
//   (read-only) is rewritten every launch.
// - Apply: the file sets a wanted value per key; a bad line or unknown key is logged and skipped. The values are
//   written into the objects at startup, on every world change (objects that loaded anew or a new generation), before
//   FactorDifficultyIntoWaveBudget reads a wave CDO (card 111's player factor then applies on top) and before
//   GetModifiedDamageByTags reads the subsystem's active preset. A changed row calls USTDifficulty::LoadSettings on
//   the world's settings object, which copies the row values the game's getters return.
// - Sync: the host sends the values that differ from the defaults to each client at its first Pong per map
//   (ChanOp::Difficulty text frames "B n", "I sec|key=val~...", "E n"); the client applies them in place of its own
//   file until it next plays a Standalone world.
// - Read counters per side and per map (damage, ammo, healing, fedscore getters, wave factor) show which side reads
//   the values. Getters the compiler inlined elsewhere are not counted.

#include "ubw/hooks_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ubw::hooks {

C114GetterFn g_orig_c114_ammo = nullptr;
C114GetterFn g_orig_c114_heal = nullptr;
C114GetterFn g_orig_c114_fedscore = nullptr;
C114HealGiveFn g_orig_c114_heal_give = nullptr;
C114CurrentFn g_c114_current = nullptr;
C491LevelFn g_c491_difficulty_level = nullptr; // card 491 diag
C114LoadFn g_c114_load = nullptr;

namespace {

constexpr wchar_t kC114File[] = L"\\StarshipTroopers_UBW\\Saved\\UBWCoop-Difficulty.ini";
constexpr wchar_t kC114Defaults[] = L"\\StarshipTroopers_UBW\\Saved\\UBWCoop-Difficulty.defaults.ini";
constexpr wchar_t kC114Table[] = L"/Game/Data/Difficulty/DT_DifficultyDataTable.DT_DifficultyDataTable";
constexpr const wchar_t* kC114Presets[] = {
    L"/Game/Data/Difficulty/DamageModifierPresets/DamageModPreset_Default.DamageModPreset_Default_C",
    L"/Game/Data/Difficulty/DamageModifierPresets/DamageModPreset_Easy.DamageModPreset_Easy_C",
    L"/Game/Data/Difficulty/DamageModifierPresets/DamageModPreset_Hard.DamageModPreset_Hard_C",
    L"/Game/Data/Difficulty/DamageModifierPresets/DamageModPreset_Extreme.DamageModPreset_Extreme_C",
};
constexpr const wchar_t* kC114Waves[] = {
#include "ubw/difficulty_waves.inc"
};
constexpr const char* kC114WaveFields[] = {"WaveBudget", "WaveMultipliers_Human", "WaveMultipliers_Bug"};

constexpr size_t kPropertyArrayDim = 0x30;
constexpr size_t kStructPropertiesSize = 0x58;  // UStruct::PropertiesSize
constexpr size_t kBytePropEnum = 0x70;          // FByteProperty::Enum (null for a plain byte)
constexpr size_t kSoftPackage = 0x8;            // TSoftObjectPtr: weak (8), FSoftObjectPath {PackageName, AssetName, ..}
constexpr size_t kSoftAsset = 0x10;
constexpr size_t kC114SubsystemPreset = 0x30;   // UADDamageModifierSubsystem: the active preset (GetModifiedDamageByTags)
constexpr int32_t kC114MaxSections = 160;
constexpr int32_t kC114MaxLeaves = 4096;
constexpr int32_t kC114MaxDim = 16;
constexpr int32_t kC114MaxElements = 256;
constexpr uint32_t kC114MaxDepth = 6;
constexpr size_t kC114KeyChars = 112;
constexpr size_t kC114NameChars = 96;
constexpr size_t kC114TextBytes = 1u << 20;
constexpr uint32_t kC114MaxLines = 600;         // log lines per launch
constexpr uint32_t kC114MaxFileLines = 50000;
constexpr uint32_t kC114DamageLogs = 12;        // logged damage reads per map
constexpr uint32_t kC114GetterLogs = 2;         // logged reads per getter per map
constexpr int32_t kC114SendFrames = 64;

enum class C114Type : uint8_t { Row, Preset, Wave };

struct C114Section {
    char name[kC114NameChars];
    char row[kChanTextMax < 64 ? kChanTextMax : 64]; // the row name (Row sections)
    const wchar_t* path;  // the DT or class path
    C114Type type;
    C31Weak object;       // the object last applied to (the table for rows, the CDO otherwise)
    uint32_t gen;         // the generation last applied
    int32_t first;        // its leaves: [first, first + count)
    int32_t count;
};

struct C114Leaf {
    char key[kC114KeyChars];
    double def;   // the game's value at the dump
    double file;  // this machine's file
    double want;  // the value in force (the file, or the host's)
    int16_t section;
    PropKind kind;
    bool has_file;
    bool has_want;
};

struct C114Reads {
    uint32_t damage;
    uint32_t damage_logs; // logged hits on others
    uint32_t player_logs; // logged hits on a player
    uint32_t ammo;
    uint32_t heal;
    uint32_t fedscore;
    uint32_t wave;
};

struct C114State {
    bool started;
    bool from_host;       // the wants came from the host (client), not this machine's file
    bool row_touched;     // a row value was ever written: LoadSettings on world changes
    uint32_t gen;         // bumped when the wants change
    uint32_t lines;
    uint32_t bad_lines;
    uintptr_t world_id;   // compared only
    void* world;          // the current game world (game thread)
    void* settings;       // the USTDifficulty LoadSettings last ran on
    uint32_t settings_gen;
    const void* preset;   // the active preset last checked in the damage detour
    uint32_t preset_gen;
    int32_t sections;
    int32_t leaves;
    int32_t rx_items;
    C114Reads reads;
    bool map_client;      // the current map is a client session's (for its read summary)
};

C114State g_c114{};
C114Section g_c114_sec[kC114MaxSections];
C114Leaf g_c114_leaf[kC114MaxLeaves];
char g_c114_text[kC114TextBytes];
size_t g_c114_len = 0;
bool g_c114_full = false;

// The side this process plays: "client" in a client session, else "host or solo".
const char* c114_side() {
    return g_client_session ? "client" : "host or solo";
}

bool c114_line() {
    return g_c114.lines++ < kC114MaxLines;
}

void c114_put(const char* fmt, ...) {
    if (g_c114_full) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(g_c114_text + g_c114_len, kC114TextBytes - g_c114_len - 2, fmt, ap);
    va_end(ap);
    if (n < 0 || g_c114_len + static_cast<size_t>(n) + 2 >= kC114TextBytes - 2) {
        g_c114_full = true;
        log::line("WARN card114: dump text full at %zu bytes", g_c114_len);
        return;
    }
    g_c114_len += static_cast<size_t>(n);
    g_c114_text[g_c114_len++] = '\r';
    g_c114_text[g_c114_len++] = '\n';
}

bool c114_ends_with(const char* s, const char* tail) {
    const size_t a = strlen(s);
    const size_t b = strlen(tail);
    return a >= b && strcmp(s + a - b, tail) == 0;
}

// Printable ASCII without the file's and the wire's separators, trimmed.
void c114_clean(const wchar_t* in, int32_t n, char* out, size_t cap) {
    size_t o = 0;
    for (int32_t i = 0; i < n && in[i] != L'\0' && o + 1 < cap; ++i) {
        const wchar_t c = in[i];
        const bool bad = c < 0x20 || c > 0x7E || c == L'=' || c == L';' || c == L'[' || c == L']' || c == L'|' ||
                         c == L'~';
        if (!(o == 0 && (bad || c == L' '))) {
            out[o++] = bad ? '_' : static_cast<char>(c);
        }
    }
    while (o > 0 && out[o - 1] == ' ') {
        --o;
    }
    out[o] = '\0';
}

void c114_fstring(const FStringRaw* s, char* out, size_t cap) {
    out[0] = '\0';
    if (s != nullptr && s->data != nullptr && s->num > 0 && s->num <= 4096) {
        c114_clean(s->data, s->num, out, cap);
    }
}

} // namespace

// Card 220 (b187): shared with objectives.cpp (the prompt widget rows).
void c114_ftext(const uint8_t* addr, char* out, size_t cap) {
    out[0] = '\0';
    if (g_ftext_to_string != nullptr) {
        c114_fstring(g_ftext_to_string(addr), out, cap);
    }
}

namespace {

// An FName's text without fname_text's 63-character cap (package paths are longer).
void c114_fname(const void* fname, char* out, size_t cap) {
    const uint8_t* const entry = fname_entry_checked(fname); // card 238
    out[0] = '\0';
    if (entry == nullptr || cap == 0) {
        return;
    }
    uint16_t header = 0;
    memcpy(&header, entry, sizeof(header));
    const bool wide = (header & 1u) != 0;
    const size_t len = static_cast<size_t>(header >> 6);
    const size_t n = len < cap - 1 ? len : cap - 1;
    for (size_t i = 0; i < n; ++i) {
        uint16_t c = entry[2 + i];
        if (wide) {
            memcpy(&c, entry + 2 + 2 * i, sizeof(c));
        }
        out[i] = c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?';
    }
    out[n] = '\0';
}

// ---- Leaf values ----

bool c114_editable(PropKind k, const char* kind_name, const uint8_t* field) {
    switch (k) {
    case PropKind::Float:
    case PropKind::Double:
    case PropKind::SInt: return true;
    case PropKind::Bool: return prop_bool_ok(field);
    case PropKind::UInt:
        return strcmp(kind_name, "EnumProperty") != 0 &&
               !(strcmp(kind_name, "ByteProperty") == 0 && read_ptr(field, kBytePropEnum) != nullptr);
    default: return false;
    }
}

double c114_get(PropKind k, const uint8_t* field, const uint8_t* addr) {
    const int32_t size = read_i32(field, kPropertyElementSize);
    float f = 0.0f;
    double d = 0.0;
    switch (k) {
    case PropKind::Float: memcpy(&f, addr, sizeof(f)); return static_cast<double>(f);
    case PropKind::Double: memcpy(&d, addr, sizeof(d)); return d;
    case PropKind::SInt: return static_cast<double>(c27_sint(addr, size));
    case PropKind::UInt: return static_cast<double>(c27_uint(addr, size));
    case PropKind::Bool: return (addr[field[kBoolByteOffset]] & field[kBoolFieldMask]) != 0 ? 1.0 : 0.0;
    default: return 0.0;
    }
}

void c114_set(PropKind k, const uint8_t* field, uint8_t* addr, double v) {
    const int32_t size = read_i32(field, kPropertyElementSize);
    const float f = static_cast<float>(v);
    const long long i = static_cast<long long>(v);
    const unsigned long long u = static_cast<unsigned long long>(v < 0.0 ? 0.0 : v);
    switch (k) {
    case PropKind::Float: memcpy(addr, &f, sizeof(f)); return;
    case PropKind::Double: memcpy(addr, &v, sizeof(v)); return;
    case PropKind::SInt: memcpy(addr, &i, static_cast<size_t>(size > 8 ? 8 : size)); return; // little-endian
    case PropKind::UInt: memcpy(addr, &u, static_cast<size_t>(size > 8 ? 8 : size)); return;
    case PropKind::Bool:
        addr[field[kBoolByteOffset]] = static_cast<uint8_t>(v != 0.0 ? addr[field[kBoolByteOffset]] | field[kBoolFieldMask]
                                                                     : addr[field[kBoolByteOffset]] & ~field[kBoolFieldMask]);
        return;
    default: return;
    }
}

// The value as the field stores it, so a parsed "1.6" equals the dumped float 1.6.
double c114_norm(PropKind k, double v) {
    switch (k) {
    case PropKind::Float: return static_cast<double>(static_cast<float>(v));
    case PropKind::SInt:
    case PropKind::UInt: return static_cast<double>(static_cast<long long>(v < 0.0 ? v - 0.5 : v + 0.5));
    case PropKind::Bool: return v != 0.0 ? 1.0 : 0.0;
    default: return v;
    }
}

void c114_num(PropKind k, double v, char* out, size_t cap) {
    switch (k) {
    case PropKind::Float: snprintf(out, cap, "%.7g", v); return;
    case PropKind::Double: snprintf(out, cap, "%.15g", v); return;
    case PropKind::Bool: snprintf(out, cap, "%s", v != 0.0 ? "true" : "false"); return;
    default: snprintf(out, cap, "%lld", static_cast<long long>(v)); return;
    }
}

C114Leaf* c114_find(int32_t section, const char* key) {
    const C114Section& s = g_c114_sec[section];
    for (int32_t i = s.first; i < s.first + s.count; ++i) {
        if (strcmp(g_c114_leaf[i].key, key) == 0) {
            return &g_c114_leaf[i];
        }
    }
    return nullptr;
}

int32_t c114_section(const char* name) {
    for (int32_t i = 0; i < g_c114.sections; ++i) {
        if (strcmp(g_c114_sec[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// ---- The walk (dump and apply share it, so both build the same keys) ----

struct C114Walk {
    bool apply;
    int16_t section;
    uint32_t keyed_depth;  // fields at this depth belong to an element keyed by its Description (0: none)
    bool collapse;         // ... and its one editable field takes the element's key
    char path[kC114KeyChars];
    size_t len;
    const void* object;
    uint32_t changes;
    uint32_t missing;
};

size_t c114_push(C114Walk& w, const char* fmt, const char* a, int32_t b) {
    const size_t old = w.len;
    const int n = snprintf(w.path + w.len, sizeof(w.path) - w.len, fmt, w.len != 0 ? "." : "", a, b);
    w.len = n > 0 && w.len + static_cast<size_t>(n) < sizeof(w.path) ? w.len + static_cast<size_t>(n) : sizeof(w.path) - 1;
    return old;
}

void c114_pop(C114Walk& w, size_t old) {
    w.len = old;
    w.path[old] = '\0';
}

void c114_leaf_dump(C114Walk& w, PropKind k, const uint8_t* field, const uint8_t* addr) {
    const double v = c114_get(k, field, addr);
    char text[48];
    c114_num(k, v, text, sizeof(text));
    if (g_c114.leaves >= kC114MaxLeaves) {
        c114_put("; %s = %s (not editable: leaf table full)", w.path, text);
        return;
    }
    C114Leaf& l = g_c114_leaf[g_c114.leaves++];
    snprintf(l.key, sizeof(l.key), "%s", w.path);
    l.def = v;
    l.want = v;
    l.section = w.section;
    l.kind = k;
    ++g_c114_sec[w.section].count;
    c114_put("%s = %s", w.path, text);
}

void c114_leaf_apply(C114Walk& w, PropKind k, const uint8_t* field, uint8_t* addr) {
    const C114Leaf* const l = c114_find(w.section, w.path);
    if (l == nullptr) {
        ++w.missing;
        return;
    }
    const double target = l->has_want ? l->want : l->def;
    const double cur = c114_get(k, field, addr);
    if (cur == target) {
        return;
    }
    c114_set(k, field, addr, target);
    ++w.changes;
    if (c114_line()) {
        char a[48];
        char b[48];
        c114_num(k, cur, a, sizeof(a));
        c114_num(k, c114_get(k, field, addr), b, sizeof(b));
        log::line("card114: %s: [%s] %s: %s -> %s (%s)", c114_side(),
                  g_c114_sec[w.section].name, w.path, a, b, object_name(w.object).text);
    }
}

void c114_tags(const uint8_t* addr, char* out, size_t cap) {
    const auto* const data = static_cast<const uint8_t*>(read_ptr(addr, kArrayData));
    const int32_t num = read_i32(addr, kArrayNum);
    size_t o = 0;
    out[0] = '\0';
    for (int32_t i = 0; i < num && i < 32 && data != nullptr && o + 1 < cap; ++i) {
        const int n = snprintf(out + o, cap - o, "%s%s", i != 0 ? ", " : "", prop_name(data + 8 * i, 8).text);
        o = n > 0 && o + static_cast<size_t>(n) < cap ? o + static_cast<size_t>(n) : cap - 1;
    }
    if (num == 0) {
        snprintf(out, cap, "(none)");
    } else if (o + 1 >= cap && cap > 5) {
        memcpy(out + cap - 5, " ...", 5); // cut at the line cap
    }
}

// A read-only value as a comment line.
void c114_comment(C114Walk& w, PropKind k, const char* kind_name, const uint8_t* field, const uint8_t* addr) {
    if (w.apply) {
        return;
    }
    char text[kC27TextChars];
    if (strcmp(kind_name, "TextProperty") == 0) {
        c114_ftext(addr, text, sizeof(text));
    } else if (strcmp(kind_name, "SoftObjectProperty") == 0 || strcmp(kind_name, "SoftClassProperty") == 0) {
        char package[kC114KeyChars * 2];
        char asset[kC114KeyChars];
        c114_fname(addr + kSoftPackage, package, sizeof(package));
        c114_fname(addr + kSoftAsset, asset, sizeof(asset));
        snprintf(text, sizeof(text), "%s.%s", package, asset);
    } else if (k == PropKind::Map || k == PropKind::Set) {
        C27Sparse s{};
        const bool ok = k == PropKind::Map && c27_map_ok(field) && c27_sparse(addr, s);
        snprintf(text, sizeof(text), "<%s, %d entries, not editable>", k == PropKind::Map ? "map" : "set", ok ? s.live : -1);
    } else {
        c27_text(k, kind_name, field, addr, text, sizeof(text));
    }
    c114_put("; %s = %s", w.path, text);
}

void c114_value(C114Walk& w, const uint8_t* field, uint8_t* addr, uint32_t depth);

void c114_field(C114Walk& w, const uint8_t* f, uint8_t* base, int32_t limit, uint32_t depth) {
    const int32_t dim = read_i32(f, kPropertyArrayDim);
    const int32_t size = read_i32(f, kPropertyElementSize);
    const int32_t off = read_i32(f, kPropertyOffset);
    const ObjectName name = fname_text(f + kFieldName);
    if (dim < 1 || dim > kC114MaxDim || size <= 0 || off < 0 || static_cast<int64_t>(off) + dim * size > limit) {
        if (!w.apply) {
            c114_put("; %s.%s = <bad property: dim %d size %d offset %d>", w.path, name.text, dim, size, off);
        }
        return;
    }
    const bool keyed = w.keyed_depth != 0 && w.keyed_depth == depth;
    if (keyed && strcmp(name.text, "Description") == 0) {
        return; // it is the element's key
    }
    ObjectName kind_name{};
    const PropKind k = prop_kind(f, kind_name);
    const bool take_key = keyed && w.collapse && dim == 1 && c114_editable(k, kind_name.text, f);
    for (int32_t i = 0; i < dim; ++i) {
        const size_t old = take_key ? w.len : dim == 1 ? c114_push(w, "%s%s", name.text, 0)
                                                       : c114_push(w, "%s%s[%d]", name.text, i);
        c114_value(w, f, base + off + static_cast<ptrdiff_t>(i) * size, depth);
        c114_pop(w, old);
    }
}

void c114_fields(C114Walk& w, const void* ustruct, uint8_t* base, uint32_t depth) {
    const void* chain[kPropMaxSupers] = {};
    int n = 0;
    for (const void* s = ustruct; s != nullptr && n < kPropMaxSupers; s = read_ptr(s, kStructSuper)) {
        chain[n++] = s;
    }
    const int32_t limit = read_i32(ustruct, kStructPropertiesSize);
    for (int c = n - 1; c >= 0; --c) {
        const auto* f = static_cast<const uint8_t*>(read_ptr(chain[c], kStructChildProperties));
        for (int i = 0; i < kPropMaxFields && f != nullptr; ++i, f = static_cast<const uint8_t*>(read_ptr(f, kFieldNext))) {
            c114_field(w, f, base, limit, depth);
        }
    }
}

// Direct editable fields of `s`.
int32_t c114_editables(const void* s) {
    int32_t n = 0;
    for (const void* c = s; c != nullptr && n < 2; c = read_ptr(c, kStructSuper)) {
        const auto* f = static_cast<const uint8_t*>(read_ptr(c, kStructChildProperties));
        for (int i = 0; i < kPropMaxFields && f != nullptr; ++i, f = static_cast<const uint8_t*>(read_ptr(f, kFieldNext))) {
            ObjectName kind_name{};
            n += c114_editable(prop_kind(f, kind_name), kind_name.text, f) ? 1 : 0;
        }
    }
    return n;
}

// The key of struct element `elem`: its Description text (" #2" from the second same text on), else "Name[i]".
void c114_element_key(const void* s, const uint8_t* elem, uint32_t (&seen)[kC114MaxElements], int32_t i, char* out,
                      size_t cap) {
    out[0] = '\0';
    const uint8_t* const desc = prop_find_prop(s, "Description");
    ObjectName kind_name{};
    const PropKind k = desc != nullptr ? prop_kind(desc, kind_name) : PropKind::Other;
    const uint8_t* const at = desc != nullptr ? elem + read_i32(desc, kPropertyOffset) : nullptr;
    if (at != nullptr && strcmp(kind_name.text, "TextProperty") == 0) {
        c114_ftext(at, out, cap);
    } else if (at != nullptr && k == PropKind::Str) {
        c114_fstring(reinterpret_cast<const FStringRaw*>(at), out, cap);
    }
    uint32_t hash = 2166136261u;
    for (const char* p = out; *p != '\0'; ++p) {
        hash = (hash ^ static_cast<uint8_t>(*p)) * 16777619u;
    }
    seen[i] = out[0] != '\0' ? hash : 0;
    int32_t same = 0;
    for (int32_t j = 0; j < i; ++j) {
        same += seen[j] == seen[i] && seen[i] != 0 ? 1 : 0;
    }
    const size_t len = strlen(out);
    if (same != 0 && len + 8 < cap) {
        snprintf(out + len, cap - len, " #%d", same + 1);
    }
}

void c114_array(C114Walk& w, const uint8_t* field, uint8_t* addr, uint32_t depth, size_t owner_len) {
    const auto* const inner = static_cast<const uint8_t*>(read_ptr(field, kArrayPropInner));
    const int32_t stride = inner != nullptr ? read_i32(inner, kPropertyElementSize) : 0;
    auto* const data = static_cast<uint8_t*>(read_ptr(addr, kArrayData));
    const int32_t num = read_i32(addr, kArrayNum);
    if (!c27_owned(inner, field) || stride <= 0 || num < 0 || num > read_i32(addr, kArrayMax) || (num > 0 && data == nullptr)) {
        c114_comment(w, PropKind::Other, "<unreadable array>", field, addr);
        return;
    }
    ObjectName kind_name{};
    const PropKind k = prop_kind(inner, kind_name);
    const void* const s = k == PropKind::Struct ? read_ptr(inner, kStructPropStruct) : nullptr;
    const bool by_desc = s != nullptr && prop_find_prop(s, "Description") != nullptr;
    uint32_t seen[kC114MaxElements] = {};
    char full[kC114KeyChars]; // the path with the array's name, restored after each element
    const size_t full_len = w.len;
    memcpy(full, w.path, sizeof(full));
    for (int32_t i = 0; i < num && i < kC114MaxElements; ++i) {
        uint8_t* const elem = data + static_cast<ptrdiff_t>(i) * stride;
        char key[kC114KeyChars] = {};
        if (by_desc) {
            c114_element_key(s, elem, seen, i, key, sizeof(key));
        }
        if (key[0] != '\0') {
            c114_pop(w, owner_len); // the element's own text names it, not the array
            c114_push(w, "%s%s", key, 0);
            const uint32_t keyed = w.keyed_depth;
            const bool collapse = w.collapse;
            w.keyed_depth = depth + 1;
            w.collapse = c114_editables(s) == 1;
            c114_fields(w, s, elem, depth + 1);
            w.keyed_depth = keyed;
            w.collapse = collapse;
        } else {
            const int n = snprintf(w.path + w.len, sizeof(w.path) - w.len, "[%d]", i); // follows the array's name
            w.len = n > 0 && w.len + static_cast<size_t>(n) < sizeof(w.path) ? w.len + static_cast<size_t>(n) : sizeof(w.path) - 1;
            c114_value(w, inner, elem, depth + 1);
        }
        memcpy(w.path, full, sizeof(full));
        w.len = full_len;
    }
}

void c114_value(C114Walk& w, const uint8_t* field, uint8_t* addr, uint32_t depth) {
    ObjectName kind_name{};
    const PropKind k = prop_kind(field, kind_name);
    if (!prop_is_leaf(k) && depth >= kC114MaxDepth) {
        c114_comment(w, PropKind::Other, "<depth cap>", field, addr);
        return;
    }
    const void* const s = k == PropKind::Struct ? read_ptr(field, kStructPropStruct) : nullptr;
    if (k == PropKind::Struct && s != nullptr && strcmp(object_name(s).text, "GameplayTagContainer") == 0) {
        char tags[kC27TextChars];
        c114_tags(addr, tags, sizeof(tags));
        if (!w.apply) {
            c114_put("; %s = %s", w.path, tags);
        }
        return;
    }
    if (k == PropKind::Struct) {
        if (c27_struct_ok(field, s)) {
            c114_fields(w, s, addr, depth + 1);
        } else {
            c114_comment(w, PropKind::Other, "<unreadable struct>", field, addr);
        }
        return;
    }
    if (k == PropKind::Array) {
        size_t owner = w.len;
        while (owner > 0 && w.path[owner - 1] != '.') {
            --owner;
        }
        c114_array(w, field, addr, depth, owner > 0 ? owner - 1 : 0);
        return;
    }
    if (!c114_editable(k, kind_name.text, field)) {
        c114_comment(w, k, kind_name.text, field, addr);
    } else if (w.apply) {
        c114_leaf_apply(w, k, field, addr);
    } else {
        c114_leaf_dump(w, k, field, addr);
    }
}

// ---- Objects ----

void* c114_load(const wchar_t* path, void* cls) {
    return g_static_load_object != nullptr ? g_static_load_object(cls, nullptr, path, nullptr, kC29LoadFlags, nullptr, true, nullptr)
                                           : nullptr;
}

// The section's object: the table for rows, the CDO otherwise. `load` loads it when it is not in memory.
void* c114_object(const C114Section& s, bool load) {
    if (s.type == C114Type::Row) {
        void* const table = g_static_find_object != nullptr ? g_static_find_object(nullptr, nullptr, s.path, false) : nullptr;
        return table != nullptr || !load ? table : c114_load(s.path, nullptr);
    }
    void* const cls = load ? c114_load(s.path, g_uclass_class()) : find_class(s.path);
    return cls != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
}

// The first class of the chain that is not a blueprint's (_C) class.
const void* c114_native(const void* cls) {
    for (int i = 0; i < kPropMaxSupers && cls != nullptr; ++i, cls = read_ptr(cls, kStructSuper)) {
        if (!c114_ends_with(object_name(cls).text, "_C")) {
            return cls;
        }
    }
    return nullptr;
}

const void* c114_row_struct(const void* table) {
    const uint8_t* prop = nullptr;
    const void* const s = prop_object_field(table, "RowStruct", &prop);
    return s != nullptr && strcmp(class_name(s).text, "ScriptStruct") == 0 ? s : nullptr;
}

uint8_t* c114_row(const void* table, const char* row) {
    const uint8_t* const map = c21_row_map(table);
    const int32_t n = map != nullptr ? array_num(map, 0) : 0;
    const auto* const data = map != nullptr ? static_cast<const uint8_t*>(read_ptr(map, kArrayData)) : nullptr;
    for (int32_t i = 0; i < n && i < kC21RowsListed && data != nullptr; ++i) {
        const uint8_t* const e = data + kRowMapElement * static_cast<size_t>(i);
        if (strcmp(fname_text(e).text, row) == 0) {
            return static_cast<uint8_t*>(read_ptr(e, 8));
        }
    }
    return nullptr;
}

// Walks section `i` on `object` (dump or apply). False when its layout is not there.
bool c114_walk(C114Walk& w, int32_t i, void* object) {
    const C114Section& s = g_c114_sec[i];
    w.section = static_cast<int16_t>(i);
    w.object = object;
    if (s.type == C114Type::Row) {
        const void* const rs = c114_row_struct(object);
        uint8_t* const row = rs != nullptr ? c114_row(object, s.row) : nullptr;
        if (row != nullptr) {
            c114_fields(w, rs, row, 0);
        }
        return row != nullptr;
    }
    const void* const cls = read_ptr(object, kObjectClass);
    if (s.type == C114Type::Preset) {
        const void* const native = c114_native(cls);
        if (native != nullptr) {
            c114_fields(w, native, static_cast<uint8_t*>(object), 0);
        }
        return native != nullptr;
    }
    const int32_t limit = read_i32(cls, kStructPropertiesSize);
    for (const char* name : kC114WaveFields) {
        const uint8_t* const f = prop_find_prop(cls, name);
        if (f != nullptr) {
            c114_field(w, f, static_cast<uint8_t*>(object), limit, 0);
        } else if (!w.apply) {
            c114_put("; %s = <missing>", name);
        }
    }
    return true;
}

uint32_t c114_apply(int32_t i, void* object) {
    C114Walk w{};
    w.apply = true;
    const bool ok = c114_walk(w, i, object);
    g_c114_sec[i].gen = g_c114.gen;
    c31_weak_set(g_c114_sec[i].object, object);
    g_c114.row_touched = g_c114.row_touched || (g_c114_sec[i].type == C114Type::Row && w.changes != 0);
    if ((!ok || w.missing != 0) && c114_line()) {
        log::line("WARN card114: [%s] on %s: layout %s, %u keys not in the table", g_c114_sec[i].name,
                  object_name(object).text, ok ? "ok" : "missing", w.missing);
    }
    return w.changes;
}

// ---- Dump ----

int32_t c114_add(C114Type type, const wchar_t* path, const char* name, const char* row) {
    if (g_c114.sections >= kC114MaxSections) {
        log::line("WARN card114: section table full at %s", name);
        return -1;
    }
    C114Section& s = g_c114_sec[g_c114.sections];
    snprintf(s.name, sizeof(s.name), "%s", name);
    snprintf(s.row, sizeof(s.row), "%s", row);
    s.path = path;
    s.type = type;
    s.first = g_c114.leaves;
    return g_c114.sections++;
}

void c114_dump(int32_t i, void* object, const char* note) {
    c114_put("");
    c114_put("[%s]", g_c114_sec[i].name);
    if (note != nullptr) {
        c114_put("; %s", note);
    }
    C114Walk w{};
    if (!c114_walk(w, i, object)) {
        c114_put("; <layout not found>");
    }
    c31_weak_set(g_c114_sec[i].object, object);
    g_c114_sec[i].gen = g_c114.gen;
}

// "/Game/../BP_X.BP_X_C" -> "BP_X".
void c114_short(const wchar_t* path, char* out, size_t cap) {
    const wchar_t* dot = wcsrchr(path, L'.');
    char text[kC114NameChars];
    c114_clean(dot != nullptr ? dot + 1 : path, 200, text, sizeof(text));
    if (c114_ends_with(text, "_C")) {
        text[strlen(text) - 2] = '\0';
    }
    snprintf(out, cap, "%s", text);
}

int32_t c114_dump_rows() {
    void* const table = c114_load(kC114Table, nullptr);
    const uint8_t* const map = table != nullptr ? c21_row_map(table) : nullptr;
    const int32_t n = map != nullptr && c114_row_struct(table) != nullptr ? array_num(map, 0) : 0;
    const auto* const data = map != nullptr ? static_cast<const uint8_t*>(read_ptr(map, kArrayData)) : nullptr;
    for (int32_t r = 0; r < n && r < kC21RowsListed && data != nullptr; ++r) {
        const ObjectName row = fname_text(data + kRowMapElement * static_cast<size_t>(r));
        char name[kC114NameChars];
        snprintf(name, sizeof(name), "Difficulty.%s", row.text);
        const int32_t i = c114_add(C114Type::Row, kC114Table, name, row.text);
        if (i >= 0) {
            c114_dump(i, table, r == 0 ? "a row of DT_DifficultyDataTable; DamageModifiers names the preset it uses" : nullptr);
        }
    }
    if (n == 0) {
        log::line("WARN card114: DT_DifficultyDataTable %s, no rows dumped", table != nullptr ? "has no readable rows" : "not loaded");
    }
    return n;
}

int32_t c114_dump_classes(C114Type type, const wchar_t* const* paths, int32_t count, const char* prefix) {
    int32_t done = 0;
    for (int32_t p = 0; p < count; ++p) {
        void* const object = c114_object(C114Section{{}, {}, paths[p], type, {}, 0, 0, 0}, true);
        char name[kC114NameChars];
        char tail[kC114NameChars];
        c114_short(paths[p], tail, sizeof(tail));
        snprintf(name, sizeof(name), "%s%s", prefix, tail);
        if (object == nullptr) {
            log::line("WARN card114: %ls not loaded, no [%s]", paths[p], name);
            continue;
        }
        const int32_t i = c114_add(type, paths[p], name, "");
        if (i >= 0) {
            c114_dump(i, object, nullptr);
            ++done;
        }
    }
    return done;
}

void c114_header(bool defaults) {
    c114_put("; UBWCoop-Difficulty%s.ini: the game's difficulty values, for the co-op mod (card 114).",
             defaults ? ".defaults" : "");
    if (defaults) {
        c114_put("; READ ONLY: the game's defaults, rewritten every launch. Edit UBWCoop-Difficulty.ini instead.");
    } else {
        c114_put("; Written once from the game's own values. Edit a number, then start the game. Delete this file to");
        c114_put("; regenerate it. UBWCoop-Difficulty.defaults.ini next to it keeps the defaults.");
    }
    c114_put("; In co-op the host's file counts: the host sends its values to each player who joins.");
    c114_put("; \"key = value\" lines are editable numbers (true/false for a switch). \"; key = value\" lines are read only");
    c114_put("; (text, gameplay tags, asset references, enums, maps). A bad line is logged and skipped.");
    c114_put(";");
    c114_put("; [Difficulty.<Row>]     one row of DT_DifficultyDataTable per difficulty: ammo, healing period and");
    c114_put(";                        fedscore multipliers, extra enemies, the enemy aim curves, the damage preset.");
    c114_put("; [DamageModPreset_<X>]  damage multipliers: one line per entry, keyed by its description; the");
    c114_put(";                        instigator and receiver tags it matches are the comments under it.");
    c114_put("; [Wave.BP_PWD_<X>]      one pre-made wave: WaveBudget (its base size) and the size factor per");
    c114_put(";                        difficulty; WaveMultipliers_Human counts when the player is human (the");
    c114_put(";                        campaign), WaveMultipliers_Bug when the player is a bug. Wave size =");
    c114_put(";                        WaveBudget x the factor, then x the mod's co-op player factor.");
    c114_put(";");
    c114_put("; Ticket limits (how many bugs attack at once) are not per difficulty: they come from a ticket set,");
    c114_put("; not from this data, so they are not in this file.");
}

bool c114_path(const wchar_t* tail, wchar_t (&path)[MAX_PATH]) {
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    const size_t t = wcslen(tail);
    if (n == 0 || n + t >= MAX_PATH) {
        log::line("WARN card114: LOCALAPPDATA unusable (len %lu, err %lu)", n, GetLastError());
        return false;
    }
    wcscpy_s(path + n, MAX_PATH - n, tail);
    return true;
}

bool c114_write(const wchar_t* path, DWORD disposition, const char* head, size_t head_len, const char* body, size_t len) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS) {
            log::line("WARN card114: cannot create %ls (err %lu)", path, err);
        }
        return false;
    }
    DWORD a = 0;
    DWORD b = 0;
    const bool ok = WriteFile(f, head, static_cast<DWORD>(head_len), &a, nullptr) != 0 && a == head_len &&
                    WriteFile(f, body, static_cast<DWORD>(len), &b, nullptr) != 0 && b == len;
    CloseHandle(f);
    if (!ok) {
        log::line("WARN card114: write to %ls failed (err %lu)", path, GetLastError());
    }
    return ok;
}

// The two files from the dump text (body from `body`): the defaults always, the user file only when absent.
void c114_write_files(size_t body) {
    wchar_t user[MAX_PATH];
    wchar_t defs[MAX_PATH];
    if (!c114_path(kC114File, user) || !c114_path(kC114Defaults, defs)) {
        return;
    }
    static char head[2][4096];
    size_t head_len[2] = {};
    for (int d = 0; d < 2; ++d) {
        const size_t save = g_c114_len;
        c114_header(d == 1);
        head_len[d] = g_c114_len - save < sizeof(head[d]) ? g_c114_len - save : 0;
        memcpy(head[d], g_c114_text + save, head_len[d]);
        g_c114_len = save;
    }
    SetFileAttributesW(defs, FILE_ATTRIBUTE_NORMAL);
    const bool d_ok = c114_write(defs, CREATE_ALWAYS, head[1], head_len[1], g_c114_text + body, g_c114_len - body);
    SetFileAttributesW(defs, FILE_ATTRIBUTE_READONLY);
    const bool u_new = c114_write(user, CREATE_NEW, head[0], head_len[0], g_c114_text + body, g_c114_len - body);
    log::line("card114: %ls %s; defaults %ls %s (%zu bytes)", user,
              u_new ? "generated" : GetLastError() == ERROR_FILE_EXISTS ? "exists, kept" : "not written", defs,
              d_ok ? "rewritten" : "NOT written", g_c114_len - body);
}

// ---- Parse ----

void c114_bad(uint32_t line, const char* why, const char* text, size_t len) {
    ++g_c114.bad_lines;
    if (c114_line()) {
        log::line("WARN card114: file line %u skipped (%s): %.*s", line, why, static_cast<int>(len > 160 ? 160 : len), text);
    }
}

bool c114_number(const char* text, PropKind k, double* out) {
    if (k == PropKind::Bool && (_stricmp(text, "true") == 0 || _stricmp(text, "false") == 0)) {
        *out = _stricmp(text, "true") == 0 ? 1.0 : 0.0;
        return true;
    }
    char* end = nullptr;
    const double v = strtod(text, &end);
    if (end == text || *end != '\0' || v != v) {
        return false;
    }
    *out = c114_norm(k, v);
    return true;
}

void c114_trim(char* s) {
    size_t a = 0;
    size_t n = strlen(s);
    while (a < n && (s[a] == ' ' || s[a] == '\t')) {
        ++a;
    }
    while (n > a && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        --n;
    }
    memmove(s, s + a, n - a);
    s[n - a] = '\0';
}

// One line; `section` is the section it is under (-1 unknown, -2 none yet).
void c114_parse_line(char* line, size_t len, uint32_t no, int32_t* section) {
    char text[512];
    snprintf(text, sizeof(text), "%.*s", static_cast<int>(len < sizeof(text) - 1 ? len : sizeof(text) - 1), line);
    c114_trim(text);
    const size_t n = strlen(text);
    if (n == 0 || text[0] == ';' || text[0] == '#') {
        return;
    }
    if (text[0] == '[') {
        if (text[n - 1] != ']') {
            c114_bad(no, "section without ]", text, n);
            return;
        }
        text[n - 1] = '\0';
        *section = c114_section(text + 1);
        if (*section < 0) {
            c114_bad(no, "unknown section", text + 1, n - 2);
        }
        return;
    }
    char* const eq = strchr(text, '=');
    if (eq == nullptr || *section == -2) {
        c114_bad(no, eq == nullptr ? "no =" : "no section yet", text, n);
        return;
    }
    if (*section < 0) {
        return; // under an unknown section, already logged
    }
    *eq = '\0';
    char* const value = eq + 1;
    c114_trim(text);
    c114_trim(value);
    C114Leaf* const l = c114_find(*section, text);
    double v = 0.0;
    if (l == nullptr) {
        c114_bad(no, "unknown or read-only key", text, strlen(text));
    } else if (!c114_number(value, l->kind, &v)) {
        c114_bad(no, "not a number", value, strlen(value));
    } else {
        l->file = v;
        l->has_file = true;
    }
}

void c114_parse(char* data, size_t len) {
    int32_t section = -2;
    size_t start = 0;
    uint32_t no = 0;
    for (size_t i = 0; i <= len && no < kC114MaxFileLines; ++i) {
        if (i == len || data[i] == '\n') {
            c114_parse_line(data + start, i - start, ++no, &section);
            start = i + 1;
        }
    }
}

// This machine's file into the leaves' file values; false when it cannot be read.
bool c114_read_file() {
    wchar_t path[MAX_PATH];
    if (!c114_path(kC114File, path)) {
        return false;
    }
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        log::line("WARN card114: cannot read %ls (err %lu): the game's values stay", path, GetLastError());
        return false;
    }
    static char data[kC114TextBytes];
    DWORD got = 0;
    const bool ok = ReadFile(f, data, sizeof(data) - 1, &got, nullptr) != 0;
    CloseHandle(f);
    if (!ok || got >= sizeof(data) - 1) {
        log::line("WARN card114: %ls %s: the game's values stay", path, ok ? "too large" : "read failed");
        return false;
    }
    c114_parse(data, got);
    return true;
}

// The wants from this machine's file; logs each value that differs from the game's.
void c114_use_file() {
    uint32_t edits = 0;
    for (int32_t i = 0; i < g_c114.leaves; ++i) {
        C114Leaf& l = g_c114_leaf[i];
        l.has_want = l.has_file;
        l.want = l.has_file ? l.file : l.def;
        if (l.has_file && l.file != l.def && c114_line()) {
            char a[48];
            char b[48];
            c114_num(l.kind, l.def, a, sizeof(a));
            c114_num(l.kind, l.file, b, sizeof(b));
            log::line("card114: file sets [%s] %s = %s (game %s)", g_c114_sec[l.section].name, l.key, b, a);
        }
        edits += l.has_file && l.file != l.def ? 1u : 0u;
    }
    g_c114.from_host = false;
    ++g_c114.gen;
    log::line("card114: %u values from the file differ from the game's, %u bad lines", edits, g_c114.bad_lines);
}

// ---- Apply on the live objects ----

void c114_settings(void* world) {
    void* const d = g_c114.row_touched && world != nullptr && g_c114_current != nullptr ? g_c114_current(world) : nullptr;
    if (d == nullptr || g_c114_load == nullptr || (d == g_c114.settings && g_c114.settings_gen == g_c114.gen)) {
        return;
    }
    const bool ok = g_c114_load(d);
    g_c114.settings = d;
    g_c114.settings_gen = g_c114.gen;
    if (c114_line()) {
        log::line("card114: %s: USTDifficulty::LoadSettings on %s = %d", c114_side(),
                  object_name(d).text, ok ? 1 : 0);
    }
}

// Every section whose object is in memory and new, or behind the generation.
void c114_refresh(void* world) {
    uint32_t changes = 0;
    for (int32_t i = 0; i < g_c114.sections; ++i) {
        void* const object = c114_object(g_c114_sec[i], false);
        if (object != nullptr && (c31_weak_get(g_c114_sec[i].object) != object || g_c114_sec[i].gen != g_c114.gen)) {
            changes += c114_apply(i, object);
        }
    }
    if (changes != 0 && c114_line()) {
        log::line("card114: %s: %u values written (generation %u)", c114_side(), changes, g_c114.gen);
    }
    c114_settings(world);
}

void c114_start() {
    const ULONGLONG t0 = GetTickCount64();
    g_c114.started = true;
    const int32_t rows = c114_dump_rows();
    const size_t body = 0;
    const int32_t presets = c114_dump_classes(C114Type::Preset, kC114Presets, 4, "");
    const int32_t waves = c114_dump_classes(C114Type::Wave, kC114Waves, static_cast<int32_t>(sizeof(kC114Waves) / sizeof(kC114Waves[0])), "Wave.");
    const ULONGLONG t1 = GetTickCount64();
    log::line("card114: dumped %d rows, %d presets, %d waves: %d sections, %d editable values, %zu bytes, %llu ms",
              rows, presets, waves, g_c114.sections, g_c114.leaves, g_c114_len, static_cast<unsigned long long>(t1 - t0));
    c114_write_files(body);
    if (c114_read_file()) {
        c114_use_file();
    }
    c114_refresh(nullptr);
}

void c114_map_summary() {
    const C114Reads& r = g_c114.reads;
    log::line("card114: reads on the %s, last map: damage %u, ammo %u, healing %u, fedscore %u, wave factor %u",
              g_c114.map_client ? "client" : "host or solo", r.damage, r.ammo, r.heal, r.fedscore, r.wave);
    g_c114.reads = {};
}

} // namespace

// Engine tick: the dump at the first tick; on a world change the read summary, the file restored after a session
// on a Standalone world, and the refresh.
void c114_step(void* engine) {
    if (!g_c114.started) {
        if (g_static_load_object == nullptr || g_uclass_class == nullptr || g_static_find_object == nullptr) {
            return;
        }
        c114_start();
    }
    void* const world = g_get_game_world(engine);
    if (world == nullptr || (reinterpret_cast<uintptr_t>(world) == g_c114.world_id && g_c114.world != nullptr)) {
        return; // the same world (a world at the old one's address after c114_arm_map runs on)
    }
    if (g_c114.world_id != 0) {
        c114_map_summary();
    }
    g_c114.world_id = reinterpret_cast<uintptr_t>(world);
    g_c114.map_client = g_client_session;
    g_c114.world = world;
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = g_world_first_pc(world);
    if (g_c114.from_host && pc != nullptr && net_mode(pc) == kNetModeStandalone) {
        log::line("card114: standalone world after a session: this machine's file again");
        c114_use_file();
    }
    c114_refresh(world);
}

// Both sides, at a real map load (c50_arm_map): the old world is gone, so a Difficulty 'E' frame before the new
// world's first c114_step passes no world to c114_refresh (world_id stays, for the old map's summary).
void c114_arm_map() {
    g_c114.world = nullptr;
}

// Before FactorDifficultyIntoWaveBudget reads the wave data CDO (both sides).
void c114_wave(const void* data) {
    ++g_c114.reads.wave;
    if (!g_c114.started || data == nullptr) {
        return;
    }
    const ObjectName cls = class_name(data);
    char name[kC114NameChars];
    snprintf(name, sizeof(name), "Wave.%s", cls.text);
    if (c114_ends_with(name, "_C")) {
        name[strlen(name) - 2] = '\0';
    }
    const int32_t i = c114_section(name);
    if (i < 0) {
        if (c114_line()) {
            log::line("WARN card114: wave data %s (%s) has no section", object_name(data).text, cls.text);
        }
        return;
    }
    if (c31_weak_get(g_c114_sec[i].object) != data || g_c114_sec[i].gen != g_c114.gen) {
        c114_apply(i, const_cast<void*>(data));
    }
}

float detour_c114_damage(const void* subsystem, float damage, const void* instigator_tags, const void* receiver_tags) {
    const void* const preset = read_ptr(subsystem, kC114SubsystemPreset);
    if (g_c114.started && preset != nullptr && (preset != g_c114.preset || g_c114.preset_gen != g_c114.gen)) {
        g_c114.preset = preset;
        g_c114.preset_gen = g_c114.gen;
        void* object = const_cast<void*>(preset);
        if (strcmp(class_name(object).text, "BlueprintGeneratedClass") == 0) {
            object = read_ptr(object, kClassDefaultObject);
        }
        char name[kC114NameChars];
        snprintf(name, sizeof(name), "%s", class_name(object).text);
        if (c114_ends_with(name, "_C")) {
            name[strlen(name) - 2] = '\0';
        }
        const int32_t i = c114_section(name);
        if (i >= 0) {
            c114_apply(i, object);
        } else if (c114_line()) {
            log::line("WARN card114: active damage preset %s (%s) has no section", object_name(object).text, name);
        }
    }
    const float out = detour_c40_by_tags(subsystem, damage, instigator_tags, receiver_tags); // card 040's trace, then the game
    ++g_c114.reads.damage;
    if (g_c461_armed) { // card 461: a Fall hit inside a release window gets its caller stack and the fall fields
        char tags[kC27TextChars];
        c114_tags(static_cast<const uint8_t*>(instigator_tags), tags, sizeof(tags));
        if (strstr(tags, "DamageType.Fall") != nullptr) {
            char stack[256];
            caller_chain(stack, sizeof(stack), 10);
            c461_fall_rows(damage, out, stack);
        }
    }
    if (g_c114.reads.damage_logs >= kC114DamageLogs && g_c114.reads.player_logs >= kC114DamageLogs) {
        return out;
    }
    char a[kC27TextChars];
    char b[kC27TextChars];
    c114_tags(static_cast<const uint8_t*>(instigator_tags), a, sizeof(a));
    c114_tags(static_cast<const uint8_t*>(receiver_tags), b, sizeof(b));
    // Hits on a player get their own quota, so player shots on bugs do not use up the log.
    uint32_t& logs = strstr(b, "Player") != nullptr ? g_c114.reads.player_logs : g_c114.reads.damage_logs;
    if (logs++ < kC114DamageLogs && c114_line()) {
        log::line("card114: %s damage %.3f -> %.3f (preset %s; instigator %s; receiver %s)",
                  c114_side(), static_cast<double>(damage), static_cast<double>(out),
                  object_name(preset).text, a, b);
    }
    return out;
}

namespace {

float c114_getter(C114GetterFn orig, const void* d, uint32_t& count, const char* what) {
    const float v = orig(d);
    if (++count <= kC114GetterLogs && c114_line()) {
        log::line("card114: %s reads %s = %.3f (%s)", c114_side(), what, static_cast<double>(v),
                  object_name(d).text);
    }
    return v;
}

} // namespace

float detour_c114_ammo(const void* d) {
    return c114_getter(g_orig_c114_ammo, d, g_c114.reads.ammo, "AmmoReceivedMultiplier");
}

float detour_c114_heal(const void* d) {
    return c114_getter(g_orig_c114_heal, d, g_c114.reads.heal, "HealingPeriodMultiplier");
}

float detour_c114_fedscore(const void* d) {
    return c114_getter(g_orig_c114_fedscore, d, g_c114.reads.fedscore, "FedscoreMultiplier");
}

// Card 117: the getter is never called for healing. USTSelfHealAbility::OnGiveAbility reads the multiplier inline
// (USTDifficulty +0x44) and writes multiplier * the ability's base period (+0x3BC) into its regen effect spec's
// Period. Log only: the values it used, read the same way after it ran.
void detour_c114_heal_give(void* ability, const void* actor_info, const void* spec) {
    g_orig_c114_heal_give(ability, actor_info, spec);
    constexpr size_t kHealBasePeriod = 0x3BC;    // USTSelfHealAbility: base regen period
    constexpr size_t kDifficultyHealing = 0x44;  // USTDifficulty: HealingPeriodMultiplier (LoadSettings copy)
    if (++g_c114.reads.heal > kC114GetterLogs || !c114_line()) {
        return;
    }
    float base = 0.0f;
    memcpy(&base, static_cast<const uint8_t*>(ability) + kHealBasePeriod, sizeof(base));
    const void* const d = g_c114_current != nullptr ? g_c114_current(ability) : nullptr;
    if (d == nullptr) {
        log::line("card114: %s self heal given (%s): base period %.3f, no difficulty settings object", c114_side(),
                  object_name(ability).text, static_cast<double>(base));
        return;
    }
    float mult = 0.0f;
    memcpy(&mult, static_cast<const uint8_t*>(d) + kDifficultyHealing, sizeof(mult));
    log::line("card114: %s self heal given (%s): HealingPeriodMultiplier %.3f (inline read, %s) x base period %.3f "
              "= regen period %.3f", c114_side(), object_name(ability).text, static_cast<double>(mult),
              object_name(d).text, static_cast<double>(base), static_cast<double>(mult * base));
}

// Host, at a client's first Pong per map (from c49_join_replay, which runs it once per map per PC): the values that differ from the game's, as "B n", "I ..." and "E n" frames.
void c114_send(void* pc) {
    if (g_client_session || !g_c114.started) {
        return;
    }
    static wchar_t wire[kChanTextFrame];
    char text[kChanTextMax + 1];
    int32_t items = 0;
    for (int32_t i = 0; i < g_c114.leaves; ++i) {
        const C114Leaf& l = g_c114_leaf[i];
        items += l.has_want && l.want != l.def ? 1 : 0;
    }
    snprintf(text, sizeof(text), "B %d", items);
    bool ok = chan_send_client(pc, chan_text_frame(ChanOp::Difficulty, text, wire));
    int32_t frames = 1;
    size_t len = 0;
    text[0] = '\0';
    for (int32_t i = 0; i <= g_c114.leaves && ok && frames < kC114SendFrames; ++i) {
        char item[kC114NameChars + kC114KeyChars + 48];
        int n = 0;
        if (i < g_c114.leaves && g_c114_leaf[i].has_want && g_c114_leaf[i].want != g_c114_leaf[i].def) {
            const C114Leaf& l = g_c114_leaf[i];
            n = snprintf(item, sizeof(item), "%s|%s=%.17g~", g_c114_sec[l.section].name, l.key, l.want);
        }
        const bool last = i == g_c114.leaves;
        if (len != 0 && (last || len + static_cast<size_t>(n > 0 ? n : 0) + 2 > static_cast<size_t>(kChanTextMax))) {
            ok = chan_send_client(pc, chan_text_frame(ChanOp::Difficulty, text, wire));
            ++frames;
            len = 0;
        }
        if (n > 0 && static_cast<size_t>(n) + 2 <= static_cast<size_t>(kChanTextMax)) {
            len += static_cast<size_t>(snprintf(text + len, sizeof(text) - len, "%s%s", len == 0 ? "I " : "", item));
        }
    }
    snprintf(text, sizeof(text), "E %d", items);
    ok = ok && chan_send_client(pc, chan_text_frame(ChanOp::Difficulty, text, wire));
    log::line("%scard114: host sent %d values to %s in %d frames", ok ? "" : "WARN ", items, object_name(pc).text, frames + 1);
}

namespace {

void c114_receive_items(char* text) {
    char* p = text;
    for (int32_t guard = 0; guard < kChanTextMax && p != nullptr && *p != '\0'; ++guard) {
        char* const end = strchr(p, '~');
        if (end != nullptr) {
            *end = '\0';
        }
        char* const bar = strchr(p, '|');
        char* const eq = bar != nullptr ? strchr(bar, '=') : nullptr;
        const int32_t s = bar != nullptr ? (static_cast<void>(*bar = '\0'), c114_section(p)) : -1;
        C114Leaf* const l = s >= 0 && eq != nullptr ? (static_cast<void>(*eq = '\0'), c114_find(s, bar + 1)) : nullptr;
        double v = 0.0;
        if (l != nullptr && c114_number(eq + 1, l->kind, &v)) {
            l->want = v;
            l->has_want = true;
            ++g_c114.rx_items;
        } else if (c114_line()) {
            log::line("WARN card114: client: host item not applied: %s", p);
        }
        p = end != nullptr ? end + 1 : nullptr;
    }
}

} // namespace

// Client: the host's values replace this machine's file ("B" clears, "I" sets, "E" applies).
void c114_receive(void*, const WireString* s) {
    char text[kChanTextMax + 1];
    if (!g_c114.started || chan_text(s, text, sizeof(text)) < 1) {
        log::line("WARN card114: client: Difficulty frame dropped (%s)", g_c114.started ? "no text" : "not started");
        return;
    }
    if (text[0] == 'B') {
        for (int32_t i = 0; i < g_c114.leaves; ++i) {
            g_c114_leaf[i].has_want = false;
            g_c114_leaf[i].want = g_c114_leaf[i].def;
        }
        g_c114.rx_items = 0;
    } else if (text[0] == 'I' && text[1] == ' ') {
        c114_receive_items(text + 2);
    } else if (text[0] == 'E') {
        g_c114.from_host = true;
        ++g_c114.gen;
        // A count short of the host's: frames lost, or the host stopped at kC114SendFrames.
        log::line("%scard114: client: host values in force, %d applied (host sent %s)",
                  g_c114.rx_items == atoi(text + 2) ? "" : "WARN ", g_c114.rx_items, text + 2);
        c114_refresh(g_c114.world);
    } else {
        log::line("WARN card114: client: unknown Difficulty frame: %.40s", text);
    }
}

} // namespace ubw::hooks
