// Card 150: the packed row codec (see ue/chan_pack.h for the byte format).

#include "ue/chan_pack.h"

#include <stdio.h>
#include <string.h>

namespace ue::chan {
namespace {

enum Kind : uint8_t { kEmpty, kDec, kHex, kB64, kInline, kRef, kDef };
constexpr uint8_t kFormRaw = 0;
constexpr uint8_t kFormTokens = 1;
constexpr int32_t kDecFracMax = 6;
constexpr int32_t kDecDigitsMax = 18;
constexpr int32_t kHexDigitsMax = 16;
constexpr int32_t kB64Min = 16;
constexpr int32_t kB64BytesMax = 1200; // kChanTextMax base64 chars decode to at most 1050 bytes
constexpr int32_t kDictSlots = kPackIds * 2;
constexpr int32_t kMarksMax = 512; // defs in one frame: each def token is 3+ chars and a delimiter, so at most 350
constexpr char kB64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

struct Dict { // one side's id -> string table; the host also hashes strings to ids
    char str[kPackIds][kPackDictLen + 1];
    uint8_t len[kPackIds]; // 0 = not defined
    int32_t n;             // host: ids given out
    int16_t slot[kDictSlots]; // host: id + 1 by hash, 0 = empty
};

Dict g_host_dict{};   // game thread
Dict g_client_dict{}; // game thread
PackStats g_stats{};  // game thread, this window

struct Writer {
    uint8_t* p;
    int32_t n;
    int32_t cap;
    bool over;
};

void put(Writer& w, uint8_t b) {
    if (w.n >= w.cap) {
        w.over = true;
        return;
    }
    w.p[w.n++] = b;
}

void put_varint(Writer& w, uint64_t v) {
    for (int32_t i = 0; i < 10; ++i) {
        const uint8_t low = static_cast<uint8_t>(v & 0x7Fu);
        v >>= 7;
        put(w, static_cast<uint8_t>(low | (v != 0 ? 0x80u : 0u)));
        if (v == 0) {
            return;
        }
    }
}

void put_chars(Writer& w, const char* s, int32_t n) {
    for (int32_t i = 0; i < n; ++i) {
        put(w, static_cast<uint8_t>(s[i]));
    }
}

void put_tag(Writer& w, uint8_t delim, Kind kind, uint8_t arg) {
    put(w, static_cast<uint8_t>(delim | (kind << 2) | (arg << 5)));
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

// A canonical decimal token: -?D(.F)? with no leading zero in D (unless D is "0"), 1..kDecFracMax digits in F, at
// most kDecDigitsMax digits in all and not a negative zero. The digits as one integer in *m, F's length in *k.
bool parse_dec(const char* t, int32_t n, int64_t* m, int32_t* k) {
    const bool neg = n > 0 && t[0] == '-';
    int32_t i = neg ? 1 : 0;
    const int32_t int_at = i;
    uint64_t v = 0;
    int32_t digits = 0;
    for (; i < n && is_digit(t[i]); ++i, ++digits) {
        v = v * 10 + static_cast<uint64_t>(t[i] - '0');
    }
    const int32_t int_digits = i - int_at;
    if (int_digits == 0 || (int_digits > 1 && t[int_at] == '0')) {
        return false;
    }
    int32_t frac = 0;
    if (i < n && t[i] == '.') {
        for (++i; i < n && is_digit(t[i]); ++i, ++frac, ++digits) {
            v = v * 10 + static_cast<uint64_t>(t[i] - '0');
        }
        if (frac == 0) {
            return false;
        }
    }
    if (i != n || frac > kDecFracMax || digits > kDecDigitsMax || (neg && v == 0)) {
        return false;
    }
    *m = neg ? -static_cast<int64_t>(v) : static_cast<int64_t>(v);
    *k = frac;
    return true;
}

// Lower-case hex with at least one letter, no leading zero, at most kHexDigitsMax digits.
bool parse_hex(const char* t, int32_t n, uint64_t* v) {
    if (n < 1 || n > kHexDigitsMax || t[0] == '0') {
        return false;
    }
    bool letter = false;
    uint64_t x = 0;
    for (int32_t i = 0; i < n; ++i) {
        const char c = t[i];
        const bool dig = is_digit(c);
        const bool hex = c >= 'a' && c <= 'f';
        if (!dig && !hex) {
            return false;
        }
        letter = letter || hex;
        x = x << 4 | static_cast<uint64_t>(dig ? c - '0' : c - 'a' + 10);
    }
    *v = x;
    return letter;
}

int32_t b64_value(char c) {
    const char* const p = c != '\0' ? strchr(kB64Chars, c) : nullptr;
    return p != nullptr ? static_cast<int32_t>(p - kB64Chars) : -1;
}

int32_t b64_encode(const uint8_t* in, int32_t n, char* out) {
    int32_t o = 0;
    for (int32_t i = 0; i < n; i += 3) {
        const uint32_t v = static_cast<uint32_t>(in[i]) << 16 |
                           (i + 1 < n ? static_cast<uint32_t>(in[i + 1]) << 8 : 0u) | (i + 2 < n ? in[i + 2] : 0u);
        out[o++] = kB64Chars[v >> 18 & 63];
        out[o++] = kB64Chars[v >> 12 & 63];
        out[o++] = i + 1 < n ? kB64Chars[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? kB64Chars[v & 63] : '=';
    }
    return o;
}

// Standard base64 of kB64Min+ chars that encodes back to the same text; the bytes in out, their count returned (0 =
// not this kind).
int32_t parse_b64(const char* t, int32_t n, uint8_t* out) {
    if (n < kB64Min || n % 4 != 0 || n / 4 * 3 > kB64BytesMax) {
        return 0;
    }
    const int32_t pad = (t[n - 1] == '=' ? 1 : 0) + (t[n - 2] == '=' ? 1 : 0);
    int32_t o = 0;
    for (int32_t i = 0; i < n; i += 4) {
        int32_t v[4];
        for (int32_t j = 0; j < 4; ++j) {
            const bool padded = i + j >= n - pad;
            v[j] = padded ? 0 : b64_value(t[i + j]);
            if (v[j] < 0 || (padded && t[i + j] != '=')) {
                return 0;
            }
        }
        const uint32_t x = static_cast<uint32_t>(v[0] << 18 | v[1] << 12 | v[2] << 6 | v[3]);
        out[o++] = static_cast<uint8_t>(x >> 16);
        out[o++] = static_cast<uint8_t>(x >> 8);
        out[o++] = static_cast<uint8_t>(x);
    }
    o -= pad;
    static char back[kB64BytesMax / 3 * 4 + 4];
    return b64_encode(out, o, back) == n && memcmp(back, t, static_cast<size_t>(n)) == 0 ? o : 0;
}

uint32_t fnv1a(const char* s, int32_t n) {
    uint32_t h = 2166136261u;
    for (int32_t i = 0; i < n; ++i) {
        h = (h ^ static_cast<uint8_t>(s[i])) * 16777619u;
    }
    return h;
}

// Host: the id of s, given one when new; -1 when the dictionary is full.
int32_t dict_id(const char* s, int32_t n) {
    Dict& d = g_host_dict;
    uint32_t at = fnv1a(s, n) % kDictSlots;
    for (int32_t probe = 0; probe < kDictSlots; ++probe, at = (at + 1) % kDictSlots) {
        const int32_t id = d.slot[at] - 1;
        if (id < 0) {
            if (d.n >= kPackIds) {
                return -1;
            }
            const int32_t fresh = d.n++;
            memcpy(d.str[fresh], s, static_cast<size_t>(n));
            d.str[fresh][n] = '\0';
            d.len[fresh] = static_cast<uint8_t>(n);
            d.slot[at] = static_cast<int16_t>(fresh + 1);
            return fresh;
        }
        if (d.len[id] == n && memcmp(d.str[id], s, static_cast<size_t>(n)) == 0) {
            return id;
        }
    }
    return -1;
}

bool set_has(const PackSet* set, int32_t id) {
    return (set->sent[id / 64] >> (id % 64) & 1u) != 0;
}

void set_flip(PackSet* set, int32_t id) {
    set->sent[id / 64] ^= uint64_t{1} << (id % 64);
}

struct Marks {
    int32_t id[kMarksMax];
    int32_t n;
    uint32_t full; // strings inline because the dictionary was full
};

void put_string(Writer& w, uint8_t delim, const char* t, int32_t n, PackSet* set, Marks& marks) {
    const bool dict = set != nullptr && n >= kPackDictMin && n <= kPackDictLen && marks.n < kMarksMax;
    const int32_t id = dict ? dict_id(t, n) : -1;
    marks.full += dict && id < 0 ? 1u : 0u;
    if (id >= 0 && set_has(set, id)) {
        put_tag(w, delim, kRef, 0);
        put_varint(w, static_cast<uint64_t>(id));
        return;
    }
    if (id >= 0) {
        set_flip(set, id);
        marks.id[marks.n++] = id;
        put_tag(w, delim, kDef, 0);
        put_varint(w, static_cast<uint64_t>(id));
        put_varint(w, static_cast<uint64_t>(n));
        put_chars(w, t, n);
        return;
    }
    put_tag(w, delim, kInline, static_cast<uint8_t>(n <= 6 ? n : 7));
    if (n > 6) {
        put_varint(w, static_cast<uint64_t>(n));
    }
    put_chars(w, t, n);
}

void put_token(Writer& w, uint8_t delim, const char* t, int32_t n, PackSet* set, Marks& marks) {
    int64_t m = 0;
    int32_t k = 0;
    uint64_t hex = 0;
    static uint8_t bytes[kB64BytesMax];
    if (n == 0) {
        put_tag(w, delim, kEmpty, 0);
    } else if (parse_dec(t, n, &m, &k)) {
        put_tag(w, delim, kDec, static_cast<uint8_t>(k));
        put_varint(w, static_cast<uint64_t>(m) << 1 ^ static_cast<uint64_t>(m >> 63));
    } else if (parse_hex(t, n, &hex)) {
        put_tag(w, delim, kHex, 0);
        put_varint(w, hex);
    } else if (const int32_t b = parse_b64(t, n, bytes); b > 0) {
        put_tag(w, delim, kB64, 0);
        put_varint(w, static_cast<uint64_t>(b));
        for (int32_t i = 0; i < b; ++i) {
            put(w, bytes[i]);
        }
    } else {
        put_string(w, delim, t, n, set, marks);
    }
}

} // namespace

int32_t pack_text(const char* text, int32_t len, PackSet* set, bool commit, uint8_t* out, int32_t cap) {
    if (text == nullptr || len < 1 || cap < len + 1) {
        return 0;
    }
    Writer w{out, 0, cap, false};
    Marks marks{};
    put(w, kFormTokens);
    int32_t i = 0;
    for (int32_t tokens = 0; tokens <= len && !w.over; ++tokens) {
        int32_t j = i;
        while (j < len && text[j] != ' ' && text[j] != ';') {
            ++j;
        }
        const uint8_t delim = j >= len ? 0 : text[j] == ' ' ? 1 : 2;
        put_token(w, delim, text + i, j - i, set, marks);
        i = j + 1;
        if (delim == 0) {
            break;
        }
    }
    const bool raw = w.over || w.n >= len + 1;
    if (raw || !commit) {
        for (int32_t m = 0; m < marks.n; ++m) {
            set_flip(set, marks.id[m]); // the defs are not on the wire: unmark them
        }
    }
    if (raw) {
        out[0] = kFormRaw;
        memcpy(out + 1, text, static_cast<size_t>(len));
        w.n = len + 1;
    }
    if (commit) {
        ++g_stats.frames;
        g_stats.raw += raw ? 1u : 0u;
        g_stats.text_bytes += static_cast<uint64_t>(len);
        g_stats.packed_bytes += static_cast<uint64_t>(w.n);
        g_stats.defs += raw ? 0u : static_cast<uint32_t>(marks.n);
        g_stats.dict_full += marks.full;
    }
    return w.n;
}

namespace {

struct Reader {
    const uint8_t* p;
    int32_t n;
    int32_t at;
    bool bad;
};

uint8_t get(Reader& r) {
    if (r.at >= r.n) {
        r.bad = true;
        return 0;
    }
    return r.p[r.at++];
}

uint64_t get_varint(Reader& r) {
    uint64_t v = 0;
    for (int32_t i = 0; i < 10; ++i) {
        const uint8_t b = get(r);
        v |= static_cast<uint64_t>(b & 0x7Fu) << (7 * i);
        if ((b & 0x80u) == 0) {
            return v;
        }
    }
    r.bad = true;
    return 0;
}

struct Text {
    char* p;
    int32_t n;
    int32_t cap; // room for the chars and a NUL
    bool over;
};

void emit(Text& t, const char* s, int32_t n) {
    if (n < 0 || t.n + n >= t.cap) {
        t.over = true;
        return;
    }
    memcpy(t.p + t.n, s, static_cast<size_t>(n));
    t.n += n;
}

void emit_bytes(Text& t, Reader& r, int32_t n) {
    for (int32_t i = 0; i < n && !r.bad && !t.over; ++i) {
        const char c = static_cast<char>(get(r));
        r.bad = r.bad || c < 0x20 || c > 0x7E;
        emit(t, &c, 1);
    }
}

void emit_dec(Text& t, uint64_t zz, int32_t k) {
    const int64_t m = static_cast<int64_t>(zz >> 1) ^ -static_cast<int64_t>(zz & 1u);
    const uint64_t a = m < 0 ? 0u - static_cast<uint64_t>(m) : static_cast<uint64_t>(m);
    char digits[24];
    const int32_t nd = snprintf(digits, sizeof(digits), "%0*llu", k + 1, static_cast<unsigned long long>(a));
    emit(t, "-", m < 0 ? 1 : 0);
    emit(t, digits, nd - k);
    emit(t, ".", k > 0 ? 1 : 0);
    emit(t, digits + nd - k, k);
}

void get_token(Reader& r, Text& t, Kind kind, uint8_t arg) {
    static uint8_t bytes[kB64BytesMax];
    static char b64[kB64BytesMax / 3 * 4 + 4];
    char num[24];
    switch (kind) {
    case kEmpty:
        return;
    case kDec:
        r.bad = r.bad || arg > kDecFracMax;
        emit_dec(t, get_varint(r), arg);
        return;
    case kHex:
        emit(t, num, snprintf(num, sizeof(num), "%llx", static_cast<unsigned long long>(get_varint(r))));
        return;
    case kB64: {
        const uint64_t n = get_varint(r);
        r.bad = r.bad || n == 0 || n > kB64BytesMax;
        for (uint64_t i = 0; i < n && !r.bad; ++i) {
            bytes[i] = get(r);
        }
        emit(t, b64, r.bad ? 0 : b64_encode(bytes, static_cast<int32_t>(n), b64));
        return;
    }
    case kInline:
        emit_bytes(t, r, arg < 7 ? arg : static_cast<int32_t>(get_varint(r) & 0xFFFFu));
        return;
    case kRef: {
        const uint64_t id = get_varint(r);
        const bool ok = id < kPackIds && g_client_dict.len[id] != 0;
        g_stats.undefined += ok ? 0u : 1u;
        r.bad = r.bad || !ok;
        emit(t, ok ? g_client_dict.str[id] : "", ok ? g_client_dict.len[id] : 0);
        return;
    }
    case kDef: {
        const uint64_t id = get_varint(r);
        const uint64_t n = get_varint(r);
        r.bad = r.bad || id >= kPackIds || n < 1 || n > kPackDictLen || r.at + static_cast<int64_t>(n) > r.n;
        if (r.bad) {
            return;
        }
        const int32_t at = t.n;
        emit_bytes(t, r, static_cast<int32_t>(n));
        if (!t.over && !r.bad) {
            memcpy(g_client_dict.str[id], t.p + at, static_cast<size_t>(n));
            g_client_dict.str[id][n] = '\0';
            g_client_dict.len[id] = static_cast<uint8_t>(n);
        }
        return;
    }
    default:
        r.bad = true;
        return;
    }
}

} // namespace

int32_t unpack_text(const uint8_t* in, int32_t n, char* out, int32_t cap) {
    if (in == nullptr || n < 2 || cap < 2) {
        return -1;
    }
    Reader r{in, n, 1, false};
    Text t{out, 0, cap, false};
    if (in[0] == kFormRaw) {
        emit_bytes(t, r, n - 1);
    } else if (in[0] == kFormTokens) {
        bool end = false;
        for (int32_t tokens = 0; tokens < n && !end && !r.bad && !t.over; ++tokens) {
            const uint8_t tag = get(r);
            const uint8_t delim = tag & 3u;
            get_token(r, t, static_cast<Kind>(tag >> 2 & 7u), static_cast<uint8_t>(tag >> 5));
            r.bad = r.bad || delim == 3;
            emit(t, delim == 1 ? " " : ";", delim == 1 || delim == 2 ? 1 : 0);
            end = delim == 0;
        }
        r.bad = r.bad || !end || r.at != n;
    } else {
        r.bad = true;
    }
    if (r.bad || t.over || t.n == 0) {
        ++g_stats.bad_frames;
        out[0] = '\0';
        return -1;
    }
    out[t.n] = '\0';
    ++g_stats.frames; // client: frames unpacked
    g_stats.text_bytes += static_cast<uint64_t>(t.n);
    g_stats.packed_bytes += static_cast<uint64_t>(n);
    return t.n;
}

PackStats pack_window() {
    const PackStats s = g_stats;
    g_stats = PackStats{};
    return s;
}

} // namespace ue::chan
