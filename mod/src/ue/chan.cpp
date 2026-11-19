#include "ue/chan.h"

#include "ue/chan_pack.h"
#include "ue/reflect.h"

#include <string.h>
#include <windows.h> // card 474: GetTickCount64 for the pace queue's enqueue time

namespace ue::chan {
namespace {

ChanOps g_ops{};                 // written once by set_ops before any hook is enabled
uint64_t g_change_name_fname = 0; // FName value (not a game pointer); 0 = not built yet
uint32_t g_client_sends[kChanOpSlots] = {}; // host: frames sent to remote PCs, by opcode (all slots past the last: 0)

uint64_t g_bytes_sent[kChanOpSlots] = {};     // both sides: serialized bytes of the frames sent, by opcode
uint64_t g_bytes_received[kChanOpSlots] = {}; // both sides: serialized bytes of the prefixed frames received, by opcode

size_t op_slot(const WireString& s) {
    const size_t op = s.num > kChanOpAt ? static_cast<size_t>(s.data[kChanOpAt]) : 0;
    return op < kChanOpSlots ? op : 0;
}

void count_client_send(const WireString& s) {
    ++g_client_sends[op_slot(s)];
}

// FString serialization (FArchive << FString): an int32 length, then Num chars, ANSI (1 byte) when all are below
// 0x80, else UCS-2 (2 bytes).
bool wire_wide(const WireString& s) {
    bool wide = false;
    for (int32_t i = 0; i < s.num && !wide; ++i) {
        wide = static_cast<uint32_t>(s.data[i]) >= 0x80u;
    }
    return wide;
}

uint64_t wire_bytes(const WireString& s) {
    return 4u + static_cast<uint64_t>(s.num > 0 ? s.num : 0) * (wire_wide(s) ? 2u : 1u);
}

// The registered arg count of op, or -1 when op is outside the table.
int32_t arg_count(wchar_t op) {
    return static_cast<size_t>(op) < g_ops.arg_counts.size() ? g_ops.arg_counts[op] : -1;
}

// Runs the handler of op in table when it exists and this is the table's side; false when none ran.
bool dispatch(std::span<const ChanHandler> table, bool side_ok, wchar_t op, void* pc, const WireString* s,
              const ChanArgs& args) {
    if (op == 0 || !side_ok || static_cast<size_t>(op) >= table.size() || table[op] == nullptr) {
        return false;
    }
    table[op](pc, s, args);
    return true;
}

bool client_session() {
    return g_ops.client_session != nullptr && *g_ops.client_session;
}

constexpr int32_t kChanStampedFrame = kChanTextFrame + kChanStampNarrowLen; // the longest frame (text), stamped
static_assert(kChanStampNarrowLen >= kChanStampLen);
static_assert(kChanNamedFrame <= kChanTextFrame && kChanMaxFrame <= kChanTextFrame);
wchar_t g_send_buf[kChanStampedFrame]; // game thread: the frame being sent, with its stamp
wchar_t g_recv_buf[kChanStampedFrame]; // game thread: the frame being received, without its stamp

// The ledger's stamp for s; unsent names why s does not go out (null: it goes). Card 474: enq_ms is the pace queue's
// enqueue time of s (0: not queued).
ChanStamp ledger_send(void* pc, const WireString& s, bool to_host, const char* unsent, uint64_t enq_ms = 0) {
    return g_ops.on_send != nullptr ? g_ops.on_send(pc, s, to_host, unsent, enq_ms) : ChanStamp{};
}

// The chars the stamp adds to s: the narrow form when s is all below 0x80 (card 390), else the wide form.
int32_t stamp_len(const WireString& s) {
    return wire_wide(s) ? kChanStampLen : kChanStampNarrowLen;
}

// s with `stamp` before its terminator, in g_send_buf. s itself when it has no terminator slot or does not fit.
WireString stamped(const WireString& s, const ChanStamp& stamp) {
    const int32_t len = stamp_len(s);
    if (s.num <= kChanOpAt || s.num + len > kChanStampedFrame || s.data[s.num - 1] != L'\0') {
        return s;
    }
    const int32_t body = s.num - 1;
    memcpy(g_send_buf, s.data, static_cast<size_t>(body) * sizeof(wchar_t));
    const uint32_t seq = (stamp.seq & ~kChanStampHeld) | (stamp.held ? kChanStampHeld : 0u); // card 474
    if (len == kChanStampLen) {
        const uint32_t v[kChanStampLen - 1] = {seq >> 15, seq, stamp.ms >> 15, stamp.ms};
        g_send_buf[body] = kChanStampMark;
        for (int32_t i = 0; i < kChanStampLen - 1; ++i) {
            g_send_buf[body + 1 + i] = static_cast<wchar_t>((v[i] & 0x7FFFu) + 1);
        }
    } else {
        const uint64_t bits = (static_cast<uint64_t>(seq & kChanStampMsMask) << 30) | (stamp.ms & kChanStampMsMask);
        g_send_buf[body] = kChanStampNarrowMark;
        for (int32_t i = 0; i < kChanStampNarrowLen - 1; ++i) { // the highest 6 bits first
            g_send_buf[body + 1 + i] = static_cast<wchar_t>(((bits >> (54 - 6 * i)) & 0x3Fu) + 0x20u);
        }
    }
    const int32_t num = s.num + len;
    g_send_buf[num - 1] = L'\0';
    return WireString{g_send_buf, num, num};
}

// The stamp of a narrow-stamped frame whose mark is at `at`; false when a stamp char is out of range.
bool narrow_stamp(const WireString* s, int32_t at, ChanStamp& stamp) {
    uint64_t bits = 0;
    for (int32_t i = 0; i < kChanStampNarrowLen - 1; ++i) {
        const uint32_t c = static_cast<uint32_t>(s->data[at + 1 + i]);
        if (c < 0x20u || c > 0x5Fu) {
            return false;
        }
        bits = (bits << 6) | (c - 0x20u);
    }
    stamp = ChanStamp{static_cast<uint32_t>(bits >> 30) & kChanStampMsMask, static_cast<uint32_t>(bits) & kChanStampMsMask,
                      false};
    return true;
}

// A received frame without its stamp (in g_recv_buf), and the stamp; the frame itself, stamped false, without one.
WireString unstamped(const WireString* s, ChanStamp& stamp, bool& has) {
    const bool whole = s->num <= kChanStampedFrame && s->num > 0 && s->data[s->num - 1] == L'\0';
    int32_t at = s->num - 1 - kChanStampLen; // the wide mark
    has = whole && at > kChanOpAt && s->data[at] == kChanStampMark;
    if (has) {
        uint32_t v[kChanStampLen - 1];
        for (int32_t i = 0; i < kChanStampLen - 1; ++i) {
            v[i] = (static_cast<uint32_t>(s->data[at + 1 + i]) - 1) & 0x7FFFu;
        }
        stamp = ChanStamp{(v[0] << 15) | v[1], (v[2] << 15) | v[3], false};
    } else {
        at = s->num - 1 - kChanStampNarrowLen; // card 390: the narrow mark
        has = whole && at > kChanOpAt && s->data[at] == kChanStampNarrowMark && narrow_stamp(s, at, stamp);
    }
    if (!has) {
        stamp = ChanStamp{};
        return *s;
    }
    stamp.held = (stamp.seq & kChanStampHeld) != 0; // card 474
    stamp.seq &= ~kChanStampHeld;
    memcpy(g_recv_buf, s->data, static_cast<size_t>(at) * sizeof(wchar_t));
    g_recv_buf[at] = L'\0';
    return WireString{g_recv_buf, at + 1, at + 1};
}

// Card 150: a packed op's frame as the text frame it was built from (in g_unpack_frame); any other frame as it is.
// A packed frame that does not unpack stays as it is: it then fails chan_parse and runs no handler (counted).
WireString unpacked(const WireString& s) {
    static uint8_t bytes[kChanBlobBytes];
    static char text[kChanTextMax + 1];
    static wchar_t g_unpack_frame[kChanTextFrame];
    const wchar_t op = s.num > kChanOpAt ? s.data[kChanOpAt] : L'\0';
    if (arg_count(op) != kChanArgsPacked) {
        return s;
    }
    const int32_t n = chan_blob(&s, bytes, kChanBlobBytes);
    const int32_t len = n > 0 ? unpack_text(bytes, n, text, kChanTextMax + 1) : -1;
    const WireString t = len > 0 ? chan_text_frame(op, text, g_unpack_frame) : WireString{g_unpack_frame, 0, 0};
    return t.num > 0 ? t : s;
}

// Parses and dispatches one received frame (unstamped, unpacked), with the ledger around the handler (ChanOps::frame_ok
// may drop it first: card 332 on a client, card 348 on the host).
void receive_row(std::span<const ChanHandler> table, bool side_ok, void* pc, const WireString& s, const ChanStamp& stamp,
                 bool has) {
    ChanArgs args{};
    const wchar_t op = chan_parse(&s, &args);
    const wchar_t seen = s.num > kChanOpAt ? s.data[kChanOpAt] : L'\0';
    if (g_ops.frame_ok != nullptr && !g_ops.frame_ok(pc, op != 0 ? op : seen)) {
        return;
    }
    if (g_ops.on_recv != nullptr) {
        g_ops.on_recv(pc, op != 0 ? op : seen, stamp, has, ChanRecv::Before, s);
    }
    const bool ran = dispatch(table, side_ok, op, pc, &s, args);
    if (g_ops.on_recv != nullptr) {
        g_ops.on_recv(pc, op != 0 ? op : seen, stamp, has, ran ? ChanRecv::Ran : ChanRecv::NoHandler, s);
    }
}

// Card 151: the rows of an unstamped batch frame b, each run as a frame of its own, in order; row i gets the stamp's
// seq + i. A malformed rest goes to receive_row as it is: it fails chan_parse and the ledger counts it (no handler).
// Each row's bytes move from the batch op's received count to the row op's.
void receive_batch(std::span<const ChanHandler> table, bool side_ok, void* pc, const WireString& b,
                   const ChanStamp& stamp, bool has, uint64_t char_bytes) {
    static wchar_t row[kChanTextFrame + 2];
    const int32_t end = b.num - 1; // the terminator
    int32_t at = kChanOpAt + 1;
    for (uint32_t i = 0; i < static_cast<uint32_t>(kChanTextFrame) && at < end; ++i) {
        const uint32_t hi = at + 1 < end ? static_cast<uint32_t>(b.data[at]) - 0x20u : 0x40u;
        const uint32_t lo = at + 1 < end ? static_cast<uint32_t>(b.data[at + 1]) - 0x20u : 0x40u;
        const int32_t n = hi < 0x40u && lo < 0x40u ? static_cast<int32_t>((hi << 6) | lo) : 0;
        ChanStamp r = stamp;
        r.seq = has ? stamp.seq + i : 0;
        if (n <= 0 || at + kChanBatchRowHead + n > end) {
            receive_row(table, side_ok, pc, b, r, has);
            return;
        }
        row[0] = b.data[0];
        memcpy(row + 1, b.data + at + kChanBatchRowHead, static_cast<size_t>(n) * sizeof(wchar_t));
        row[n + 1] = L'\0';
        const WireString f{row, n + 2, n + 2};
        g_bytes_received[op_slot(f)] += static_cast<uint64_t>(n) * char_bytes;
        g_bytes_received[op_slot(b)] -= static_cast<uint64_t>(n) * char_bytes;
        receive_row(table, side_ok, pc, unpacked(f), r, has);
        at += kChanBatchRowHead + n;
    }
}

void receive(std::span<const ChanHandler> table, bool side_ok, void* pc, const WireString* raw) {
    g_bytes_received[op_slot(*raw)] += wire_bytes(*raw);
    ChanStamp stamp{};
    bool has = false;
    const WireString s = unstamped(raw, stamp, has);
    if (arg_count(s.num > kChanOpAt ? s.data[kChanOpAt] : L'\0') == kChanArgsBatch) {
        receive_batch(table, side_ok, pc, s, stamp, has, wire_wide(*raw) ? 2u : 1u);
        return;
    }
    receive_row(table, side_ok, pc, unpacked(s), stamp, has);
}

} // namespace

ServerChangeNameFn g_orig_server_change_name = nullptr;
ClientMessageFn g_client_message = nullptr;
ClientMessageFn g_orig_client_message = nullptr;
wchar_t g_chan_wire[kChanMaxFrame] = {0x1F};
static_assert(kChanPrefixLen == 1);

void set_ops(const ChanOps& ops) {
    g_ops = ops;
}

// The frame for op with its fixed arguments, in the static wire buffer.
WireString chan_frame(wchar_t op, const ChanArgs& args) {
    const int32_t named = arg_count(op);
    const int32_t n = named > 0 ? named : 0; // a named or text op is built by its sender, not here
    g_chan_wire[kChanOpAt] = op;
    for (int32_t i = 0; i < n && i < kChanMaxArgs; ++i) {
        g_chan_wire[kChanOpAt + 1 + i] = static_cast<wchar_t>(args.v[i] % kChanArgMax + 1);
    }
    const int32_t num = kChanOpAt + 1 + n + 1;
    g_chan_wire[num - 1] = L'\0';
    return WireString{g_chan_wire, num, num};
}

// Client: one frame on the local PC's ServerChangeName.
bool chan_send_host(void* pc, WireString parms) {
    const uint64_t name = ue::reflect::fname_cached(g_change_name_fname, L"ServerChangeName");
    void* const function = (pc != nullptr && name != 0) ? ue::reflect::g_find_function_checked(pc, name) : nullptr;
    if (function == nullptr || parms.num <= kChanOpAt) {
        ledger_send(pc, parms, true, pc == nullptr ? "no PC" : (function == nullptr ? "no ServerChangeName" : "empty"));
        if (g_ops.send_skipped != nullptr) {
            g_ops.send_skipped(parms.num > kChanOpAt ? static_cast<unsigned>(parms.data[kChanOpAt]) : 0u, pc,
                               function, parms.num);
        }
        return false;
    }
    const auto process_event = reinterpret_cast<ue::reflect::ProcessEventFn>(
        ue::reflect::read_ptr(ue::reflect::read_ptr(pc, 0), ue::reflect::kVtProcessEvent));
    WireString out = stamped(parms, ledger_send(pc, parms, true, nullptr));
    g_bytes_sent[op_slot(out)] += wire_bytes(out);
    process_event(pc, function, &out);
    return true;
}

bool chan_to_host(void* pc, wchar_t op, uint32_t arg) {
    return chan_send_host(pc, chan_frame(op, ChanArgs{{arg}}));
}

// --- Card 140: send pacing on the host's one send point ----------------------------------------------------------
// Reliable ClientMessage bursts (a grenade volley, a nuke's gibs, the join replay) filled QueuedBits in one frame and
// starved the engine's actor replication (IsNetReady); a burst past the channel's 511 reliable bunches closes the
// connection (reason 65 RPCReliableBufferOverflow, c140a run 1, when a full FIFO was flushed at once). Each client
// connection has a FIFO, and a frame goes out only while the engine has headroom for it, read live before each send
// (ChanOps::pace_live, run 2 design):
//   QueuedBits + SendBuffer bits + the frame's bits <= 0 (run 3: only into the engine's headroom; run 2's positive
//   ceiling of one tick of CurrentNetSpeed starved actor replication, IsNetReady false on every fight frame), and the
//   PC channel's reliable bunches out < kPaceRelHold (half of the 511 limit; the direct guard against reason 65).
// Else the frame waits in the FIFO, in order; chan_pace_step drains each tick, oldest first, under the same gate. A
// frame that does not fit goes alone, whatever its size, when QueuedBits + SendBuffer is at most 0 and nothing else
// has gone to that connection this tick (run 4): at most one oversized frame per tick. The engine clamps QueuedBits
// at -2 ticks of CurrentNetSpeed (it depends on DeltaTime), so run 3's "alone only past one 30 Hz tick" rule left
// frames between the two sizes waiting forever.
// A full FIFO is never flushed and nothing is dropped: the push sends only the oldest frames it needs room for, in
// order and past the gate, each counted as full_sends (a run fail, logged with its count). A frame's stamp and ledger
// row are written at its real send, so the ledger order is the wire order.
namespace {

constexpr int32_t kPaceRelHold = 256;   // reliable bunches outstanding on the PC channel (limit 511)
// Card 391: a client that has sent nothing for this long is not reading its socket (a load hitch of 0.7 to 2.5 s at
// every map start). Frames sent into that silence overflowed its receive buffer: 70 packets lost, then resent in
// one host frame, 61 kB (`c391b78`). The frames wait in the FIFO instead and go when the client is heard again.
constexpr int32_t kPaceSilentMs = 100;
constexpr int32_t kPaceChars = 1048576; // per connection, 2 MB as wchar_t; c140a peak 1048006 bytes
constexpr uint32_t kPaceEntries = 8192; // per connection; c140a peak 3381 frames

struct PaceEntry {
    int32_t at;   // first wchar_t in chars
    int32_t num;
    int32_t span; // num plus the ring tail skipped before it
    uint64_t enq_ms;
    bool waited;  // card 151: still queued after a drain (its stamp carries enq_ms, held); always so unbatched
    bool starts;  // card 151: begins a batch of its own (chan_send_client batch_start)
};
struct PaceQueue {
    void* pc; // null = free; compared and passed to ClientMessage only while chan_pace_step lists it live
    uint32_t head;  // oldest entry
    uint32_t count;
    int32_t write;  // next free wchar_t
    int32_t used;   // wchar_ts in use, skipped tails included
    bool sent_tick; // a frame went to this connection since the last chan_pace_step (one oversized frame per tick)
    uint32_t fresh; // card 151: the newest entries pushed since the last drain (not yet waited)
    PackSet pack;   // card 150: the dictionary ids this connection has been sent
    PaceEntry e[kPaceEntries];
    wchar_t chars[kPaceChars];
};
enum class PaceGate : uint8_t { Go, Bits, Reliable, Silent };

PaceQueue g_pace[kChanPaceSlots]{}; // game thread
PaceStats g_pace_stats{};           // game thread, this window
uint64_t g_pace_now_ms = 0;         // the last step's time
int64_t g_pace_frame_bytes = 0;     // chan bytes sent since the last step, all connections

PaceQueue* pace_queue(void* pc) {
    PaceQueue* free_q = nullptr;
    for (PaceQueue& q : g_pace) {
        if (q.pc == pc) {
            return &q;
        }
        free_q = free_q == nullptr && q.pc == nullptr ? &q : free_q;
    }
    if (free_q != nullptr) { // field by field: a PaceQueue temporary would put 2 MB on the stack
        free_q->pc = pc;
        free_q->head = 0;
        free_q->count = 0;
        free_q->write = 0;
        free_q->used = 0;
        free_q->sent_tick = false;
        free_q->fresh = 0;
        free_q->pack = PackSet{}; // a new connection (or the same one after a drop) gets every def again
    }
    return free_q;
}

// The live engine gate for one frame of `bits` to pc. first: nothing has gone to pc this tick. No reading (no
// connection yet): go, counted.
PaceGate pace_gate(const void* pc, int64_t bits, bool first) {
    PaceLive live{};
    if (g_ops.pace_live == nullptr || !g_ops.pace_live(pc, &live)) {
        ++g_pace_stats.no_reading;
        return PaceGate::Go;
    }
    g_pace_stats.rel_peak = live.reliable_out > g_pace_stats.rel_peak ? live.reliable_out : g_pace_stats.rel_peak;
    g_pace_stats.pending_peak =
        live.pending_bits > g_pace_stats.pending_peak ? live.pending_bits : g_pace_stats.pending_peak;
    if (live.reliable_out >= kPaceRelHold) {
        return PaceGate::Reliable;
    }
    if (live.silent_ms >= kPaceSilentMs) {
        return PaceGate::Silent;
    }
    const bool fits = live.pending_bits + bits <= 0;     // run 3: only into the engine's headroom
    const bool alone = !fits && first && live.pending_bits <= 0; // run 4: the one oversized frame this tick
    g_pace_stats.alone_sends += alone ? 1u : 0u;
    return fits || alone ? PaceGate::Go : PaceGate::Bits;
}

// Card 150: the frame as it goes on the wire, in buf: a packed op's text frame packed into a blob frame (with commit,
// its dictionary defs are marked in *set; set null = no dictionary); any other frame as it is.
WireString wire_form(PackSet* set, const WireString& s, bool commit, wchar_t* buf) {
    static char text[kChanTextMax + 1];
    static uint8_t bytes[kChanBlobBytes];
    const wchar_t op = s.num > kChanOpAt ? s.data[kChanOpAt] : L'\0';
    const int32_t len = arg_count(op) == kChanArgsPacked ? chan_text(&s, text, kChanTextMax + 1) : -1;
    const int32_t n = len > 0 ? pack_text(text, len, set, commit, bytes, kChanBlobBytes) : 0;
    const WireString w = n > 0 ? chan_blob_frame(op, bytes, n, buf) : WireString{buf, 0, 0};
    return w.num > 0 ? w : s;
}

// The bits s takes on the wire with its stamp (packed ops packed, nothing marked).
int64_t frame_bits(PackSet* set, const WireString& s) {
    static wchar_t buf[kChanBlobFrame];
    const WireString w = wire_form(set, s, false, buf);
    // Card 390: a wide frame takes the wide stamp (2 bytes per char), a narrow one the narrow stamp (1 byte each).
    const int32_t stamp = wire_wide(w) ? kChanStampLen * 2 : kChanStampNarrowLen;
    return static_cast<int64_t>(wire_bytes(w) + static_cast<uint64_t>(stamp)) * 8;
}

// Card 474: enq_ms is the pace queue's enqueue time of s (0: s was not queued); the stamp carries it.
void send_now(void* pc, PackSet* set, const WireString& s, uint64_t enq_ms = 0) {
    static wchar_t buf[kChanBlobFrame];
    count_client_send(s);
    const ChanStamp stamp = ledger_send(pc, s, false, nullptr, enq_ms); // the ledger sees the text the sender built
    const WireString out = stamped(wire_form(set, s, true, buf), stamp);
    const uint64_t bytes = wire_bytes(out);
    g_bytes_sent[op_slot(out)] += bytes;
    g_pace_stats.sent_bytes += static_cast<int64_t>(bytes);
    g_pace_frame_bytes += static_cast<int64_t>(bytes);
    g_client_message(pc, &out, 0, 0.0f); // FName None, lifetime 0
}

// Takes the head entry off q (its chars stay valid until the next push) and counts its wait. Returns the enqueue time
// its stamp carries: 0 when it never waited past a drain (card 151).
uint64_t pace_pop(PaceQueue& q, WireString& s) {
    const PaceEntry& e = q.e[q.head];
    const uint64_t wait = g_pace_now_ms > e.enq_ms ? g_pace_now_ms - e.enq_ms : 0;
    g_pace_stats.max_wait_ms = wait > g_pace_stats.max_wait_ms ? wait : g_pace_stats.max_wait_ms;
    s = WireString{q.chars + e.at, e.num, e.num};
    const uint64_t enq_ms = e.waited ? e.enq_ms : 0;
    q.used -= e.span;
    q.head = (q.head + 1) % kPaceEntries;
    --q.count;
    q.fresh = q.fresh < q.count ? q.fresh : q.count;
    if (q.count == 0) {
        q.write = 0;
        q.used = 0;
    }
    g_pace_stats.drained_bytes += static_cast<int64_t>(wire_bytes(s));
    return enq_ms;
}

void pace_pop_send(PaceQueue& q) {
    WireString s{};
    const uint64_t enq_ms = pace_pop(q, s);
    q.sent_tick = true;
    send_now(q.pc, &q.pack, s, enq_ms); // s stays valid: nothing is pushed while it is sent
}

// Card 151: the bits of a frame of `chars` chars (terminator included) on the wire with its stamp; as frame_bits.
int64_t chars_bits(int32_t chars, bool wide) {
    const int32_t stamp = wide ? kChanStampLen : kChanStampNarrowLen;
    return static_cast<int64_t>(4 + static_cast<int64_t>(chars + stamp) * (wide ? 2 : 1)) * 8;
}

wchar_t g_batch_buf[kChanTextFrame + kChanBatchRowHead]; // game thread: the batch being built (one row may overrun)

// Card 151: sends the batch built in g_batch_buf (rows of row_chars chars in all, the first row's op first_op, stamp
// first): one row goes as its own frame, more as a batch frame. The row chars' bytes count to their ops already.
void batch_out(PaceQueue& q, int32_t num, int32_t rows, int32_t row_chars, size_t first_op, const ChanStamp& first,
               bool wide) {
    size_t slot = static_cast<size_t>(g_ops.batch_op);
    if (rows == 1) { // the row without its length chars: the frame it was
        memmove(g_batch_buf + kChanOpAt, g_batch_buf + kChanOpAt + 1 + kChanBatchRowHead,
                static_cast<size_t>(row_chars) * sizeof(wchar_t));
        num = kChanOpAt + row_chars;
        slot = first_op;
    } else {
        ++g_client_sends[slot];
        ++g_pace_stats.batches;
        g_pace_stats.batched_rows += static_cast<uint32_t>(rows);
    }
    g_batch_buf[num] = L'\0';
    const WireString out = stamped(WireString{g_batch_buf, num + 1, num + 1}, first);
    const uint64_t bytes = wire_bytes(out);
    g_bytes_sent[slot] += bytes - static_cast<uint64_t>(row_chars) * (wide ? 2u : 1u);
    g_pace_stats.sent_bytes += static_cast<int64_t>(bytes);
    g_pace_frame_bytes += static_cast<int64_t>(bytes);
    q.sent_tick = true;
    g_client_message(q.pc, &out, 0, 0.0f); // FName None, lifetime 0
}

// Card 151: one batch of q's head rows: the rows that share the first row's width (a wide row would double a narrow
// one), fit one frame, start no batch of their own (past the first) and pass the gate with the batch so far. Each row
// is ledgered and packed (dictionary defs committed) only once it is in. Go when a row went; else the first row's hold.
PaceGate batch_send(PaceQueue& q) {
    static wchar_t form[kChanBlobFrame];
    g_batch_buf[0] = kChanPrefix[0];
    g_batch_buf[kChanOpAt] = g_ops.batch_op;
    int32_t num = kChanOpAt + 1;
    int32_t rows = 0;
    int32_t row_chars = 0;
    size_t first_op = 0;
    ChanStamp first{};
    bool wide = false;
    for (uint32_t i = 0; i < kPaceEntries && q.count != 0; ++i) {
        const PaceEntry& e = q.e[q.head];
        const WireString w = wire_form(&q.pack, WireString{q.chars + e.at, e.num, e.num}, false, form);
        const int32_t n = w.num - kChanOpAt - 1; // the op and body: no prefix, no terminator
        const bool w_wide = wire_wide(w);
        const int32_t chars = rows == 0 ? w.num : num + kChanBatchRowHead + n + 1;
        if (rows != 0 && (w_wide != wide || e.starts || chars > kChanTextFrame)) {
            break;
        }
        const PaceGate g = pace_gate(q.pc, chars_bits(chars, w_wide), !q.sent_tick);
        if ((g != PaceGate::Go || n <= 0) && rows != 0) {
            break;
        }
        if (g != PaceGate::Go || n <= 0) {
            if (g == PaceGate::Go) { // no row chars (no terminator): goes as it is
                pace_pop_send(q);
            }
            return g;
        }
        WireString s{};
        const uint64_t enq_ms = pace_pop(q, s); // s stays valid: nothing is pushed while the batch is built
        count_client_send(s);
        const ChanStamp stamp = ledger_send(q.pc, s, false, nullptr, enq_ms); // the ledger sees the text
        const WireString c = wire_form(&q.pack, s, true, form);
        g_batch_buf[num] = static_cast<wchar_t>(((n >> 6) & 0x3F) + 0x20);
        g_batch_buf[num + 1] = static_cast<wchar_t>((n & 0x3F) + 0x20);
        memcpy(g_batch_buf + num + kChanBatchRowHead, c.data + kChanOpAt, static_cast<size_t>(n) * sizeof(wchar_t));
        g_bytes_sent[op_slot(c)] += static_cast<uint64_t>(n) * (w_wide ? 2u : 1u);
        first = rows == 0 ? stamp : first;
        first_op = rows == 0 ? op_slot(c) : first_op;
        wide = w_wide;
        num += kChanBatchRowHead + n;
        row_chars += n;
        ++rows;
    }
    batch_out(q, num, rows, row_chars, first_op, first, wide);
    return PaceGate::Go;
}

bool pace_push(PaceQueue& q, const WireString& s, bool starts) {
    if (s.num <= 0 || s.num > kPaceChars || q.count >= kPaceEntries) {
        return false;
    }
    const int32_t skip = q.write + s.num > kPaceChars ? kPaceChars - q.write : 0;
    if (q.used + skip + s.num > kPaceChars) {
        return false;
    }
    const int32_t at = skip != 0 ? 0 : q.write;
    memcpy(q.chars + at, s.data, static_cast<size_t>(s.num) * sizeof(wchar_t));
    // Card 474: the enqueue time is read now, not the last step's g_pace_now_ms (0 before the first step, and up to a
    // host frame early after it): the frame's stamp carries it, so the client's frame age covers the wait here.
    const bool batching = g_ops.batch_op != 0; // card 151: a batched row waits only once a drain leaves it queued
    q.e[(q.head + q.count) % kPaceEntries] = PaceEntry{at, s.num, skip + s.num, GetTickCount64(), !batching, starts};
    ++q.count;
    q.fresh += batching ? 1u : 0u;
    q.write = at + s.num;
    q.used += skip + s.num;
    g_pace_stats.peak_frames = q.count > g_pace_stats.peak_frames ? q.count : g_pace_stats.peak_frames;
    const int64_t bytes = static_cast<int64_t>(q.used) * 2;
    g_pace_stats.peak_bytes = bytes > g_pace_stats.peak_bytes ? bytes : g_pace_stats.peak_bytes;
    g_pace_stats.paced += batching ? 0u : 1u;
    return true;
}

// The head frame alone, when the gate lets it go.
PaceGate head_send(PaceQueue& q) {
    const PaceEntry& e = q.e[q.head];
    const PaceGate g = pace_gate(q.pc, frame_bits(&q.pack, WireString{q.chars + e.at, e.num, e.num}), !q.sent_tick);
    if (g == PaceGate::Go) {
        pace_pop_send(q);
    }
    return g;
}

// Card 151: the rows a drain leaves queued have waited: their stamps carry the enqueue time (held), counted paced.
void mark_waited(PaceQueue& q) {
    const uint32_t fresh = q.fresh < q.count ? q.fresh : q.count;
    for (uint32_t i = q.count - fresh; i < q.count; ++i) {
        q.e[(q.head + i) % kPaceEntries].waited = true;
    }
    g_pace_stats.paced += fresh;
    q.fresh = 0;
}

// Sends queued frames oldest first while the gate allows; the first hold ends the step (counted by its reason).
// Card 151: with batching, each send is one batch of head rows.
void pace_drain(PaceQueue& q) {
    PaceGate g = PaceGate::Go;
    for (uint32_t i = 0; i < kPaceEntries && q.count != 0 && g == PaceGate::Go; ++i) {
        g = g_ops.batch_op != 0 ? batch_send(q) : head_send(q);
    }
    g_pace_stats.holds_bits += g == PaceGate::Bits ? 1u : 0u;
    g_pace_stats.holds_rel += g == PaceGate::Reliable ? 1u : 0u;
    g_pace_stats.holds_silent += g == PaceGate::Silent ? 1u : 0u;
    mark_waited(q);
}

// The remote and hold checks every host send passes; false = not sent (the ledger has the reason).
bool client_send_allowed(void* pc, const WireString& s) {
    if (pc == nullptr || g_ops.is_remote_pc == nullptr || !g_ops.is_remote_pc(pc)) {
        ledger_send(pc, s, false, pc == nullptr ? "no PC" : "not a remote PC");
        return false;
    }
    const wchar_t op = s.num > kChanOpAt ? s.data[kChanOpAt] : L'\0';
    const char* const held = g_ops.client_hold != nullptr ? g_ops.client_hold(pc, op) : nullptr;
    if (held != nullptr) {
        ledger_send(pc, s, false, held);
        return false;
    }
    return true;
}

} // namespace

// Host: a frame on a remote PC's ClientMessage; the paced host send point (card 294 priority frames go through
// chan_send_client_now). False (nothing sent) for any other PC. True means sent now or queued in order behind its
// connection's earlier frames (card 140).
bool chan_send_client(void* pc, const WireString& s, bool batch_start) {
    if (!client_send_allowed(pc, s)) {
        return false;
    }
    PaceQueue* const q = pace_queue(pc);
    const int64_t bits = frame_bits(q != nullptr ? &q->pack : nullptr, s);
    g_pace_stats.offered_bytes += bits / 8;
    if (q == nullptr) {
        ++g_pace_stats.no_slot;
        send_now(pc, nullptr, s);
        return true;
    }
    // Card 151: with batching every frame is queued; chan_flush_all sends this tick's rows at the tick end.
    if (g_ops.batch_op == 0 && q->count == 0 && pace_gate(pc, bits, !q->sent_tick) == PaceGate::Go) {
        q->sent_tick = true;
        send_now(pc, &q->pack, s);
        return true;
    }
    for (uint32_t i = 0; i < kPaceEntries && !pace_push(*q, s, batch_start); ++i) {
        if (q->count == 0) { // cannot be queued at all (an empty frame): goes now, counted as a full send
            ++g_pace_stats.full_sends;
            q->sent_tick = true;
            send_now(pc, &q->pack, s);
            return true;
        }
        ++g_pace_stats.full_sends; // full FIFO: the oldest frame goes past the gate, in order, to make room
        pace_pop_send(*q);
    }
    return true;
}

// Card 294: a priority frame skips the pace FIFO and goes now, past the gate, ahead of the frames already queued for
// pc (their count in *queued_ahead). Same remote and hold checks as chan_send_client. An op is always sent this way or
// never, so its own frames never reorder among themselves.
bool chan_send_client_now(void* pc, const WireString& s, uint32_t* queued_ahead) {
    *queued_ahead = 0;
    if (!client_send_allowed(pc, s)) {
        return false;
    }
    PaceQueue* const q = pace_queue(pc);
    g_pace_stats.offered_bytes += frame_bits(q != nullptr ? &q->pack : nullptr, s) / 8;
    ++g_pace_stats.priority;
    if (q != nullptr && g_ops.batch_op != 0) { // card 151: this tick's earlier rows go first, as unbatched they had
        pace_drain(*q);
    }
    *queued_ahead = q != nullptr ? q->count : 0;
    if (q != nullptr) {
        q->sent_tick = true;
    }
    send_now(pc, q != nullptr ? &q->pack : nullptr, s);
    return true;
}

uint32_t chan_pace_step(const PaceConn* conns, int32_t n, uint64_t now_ms) {
    g_pace_now_ms = now_ms;
    g_pace_stats.frame_bytes_max =
        g_pace_frame_bytes > g_pace_stats.frame_bytes_max ? g_pace_frame_bytes : g_pace_stats.frame_bytes_max;
    g_pace_frame_bytes = 0;
    uint32_t dropped = 0;
    for (PaceQueue& q : g_pace) {
        bool live = false;
        for (int32_t i = 0; q.pc != nullptr && i < n && !live; ++i) {
            live = conns[i].pc == q.pc;
        }
        if (q.pc != nullptr && !live) {
            dropped += q.count; // the PC is gone (logout, map change): its frames have no receiver
            q.pc = nullptr;
            q.count = 0;
            q.fresh = 0;
            continue;
        }
        if (live) {
            q.sent_tick = false; // a new tick: one oversized frame may go again
            pace_drain(q);
        }
    }
    g_pace_stats.dropped += dropped;
    return dropped;
}

uint32_t chan_pace_drop(const void* pc) {
    for (PaceQueue& q : g_pace) {
        if (q.pc == pc && pc != nullptr) {
            const uint32_t n = q.count;
            q.pc = nullptr;
            q.count = 0;
            q.fresh = 0;
            g_pace_stats.dropped += n;
            return n;
        }
    }
    return 0;
}

uint32_t chan_pace_depth(const void* pc) {
    for (const PaceQueue& q : g_pace) {
        if (q.pc == pc && pc != nullptr) {
            return q.count - (q.fresh < q.count ? q.fresh : q.count);
        }
    }
    return 0;
}

void chan_flush_client(void* pc) {
    for (PaceQueue& q : g_pace) {
        if (q.pc == pc && pc != nullptr && q.count != 0) {
            pace_drain(q);
        }
    }
}

void chan_flush_all() {
    for (PaceQueue& q : g_pace) {
        if (q.pc != nullptr && q.fresh != 0) {
            pace_drain(q);
        }
    }
}

// Card 290: the window's longest wait includes the age of each connection's oldest frame still queued, so a queue that
// never drains (the reliable hold with no acks) shows its wait instead of 0.
PaceStats chan_pace_window() {
    for (const PaceQueue& q : g_pace) {
        if (q.pc != nullptr && q.count != 0 && g_pace_now_ms > q.e[q.head].enq_ms) {
            const uint64_t age = g_pace_now_ms - q.e[q.head].enq_ms;
            g_pace_stats.max_wait_ms = age > g_pace_stats.max_wait_ms ? age : g_pace_stats.max_wait_ms;
        }
    }
    const PaceStats s = g_pace_stats;
    g_pace_stats = PaceStats{};
    return s;
}

// Host: one frame on a remote PC's ClientMessage. Returns false (nothing sent) for any other PC.
bool chan_to_client_args(void* pc, wchar_t op, const ChanArgs& args) {
    return chan_send_client(pc, chan_frame(op, args));
}

bool chan_to_client(void* pc, wchar_t op, uint32_t arg) {
    return chan_to_client_args(pc, op, ChanArgs{{arg}});
}

uint32_t chan_client_sends(size_t op) {
    return op < kChanOpSlots ? g_client_sends[op] : 0;
}

uint64_t chan_bytes_sent(size_t op) {
    return op < kChanOpSlots ? g_bytes_sent[op] : 0;
}

uint64_t chan_bytes_received(size_t op) {
    return op < kChanOpSlots ? g_bytes_received[op] : 0;
}

// The data unit count a blob of n bytes packs into.
int32_t blob_units(int32_t n) {
    return (n * 8 + 14) / 15;
}

WireString chan_blob_frame(wchar_t op, const uint8_t* bytes, int32_t n, wchar_t* buf) {
    if (n < 1 || n > kChanBlobBytes) {
        return WireString{buf, 0, 0};
    }
    memcpy(buf, kChanPrefix, kChanPrefixLen * sizeof(wchar_t));
    buf[kChanOpAt] = op;
    buf[kChanOpAt + 1] = static_cast<wchar_t>(n + 1);
    const int32_t units = blob_units(n);
    uint32_t acc = 0; // bits not yet written, low first
    int32_t have = 0;
    int32_t u = 0;
    for (int32_t i = 0; i < n; ++i) {
        acc |= static_cast<uint32_t>(bytes[i]) << have;
        have += 8;
        if (have >= 15) {
            buf[kChanOpAt + 2 + u++] = static_cast<wchar_t>((acc & 0x7FFFu) + 1);
            acc >>= 15;
            have -= 15;
        }
    }
    if (have > 0) {
        buf[kChanOpAt + 2 + u++] = static_cast<wchar_t>((acc & 0x7FFFu) + 1);
    }
    const int32_t num = kChanOpAt + 2 + units + 1;
    buf[num - 1] = L'\0';
    return u == units ? WireString{buf, num, num} : WireString{buf, 0, 0};
}

// The byte count of a well-formed blob frame, or -1.
int32_t chan_blob_bytes(const WireString* s) {
    if (s->num < kChanOpAt + 4 || s->data[s->num - 1] != L'\0') {
        return -1;
    }
    const int32_t n = static_cast<int32_t>(s->data[kChanOpAt + 1]) - 1;
    if (n < 1 || n > kChanBlobBytes || s->num != kChanOpAt + 2 + blob_units(n) + 1) {
        return -1;
    }
    for (int32_t i = kChanOpAt + 1; i < s->num - 1; ++i) {
        if (s->data[i] == 0 || static_cast<uint32_t>(s->data[i]) > 0x8000u) {
            return -1;
        }
    }
    return n;
}

int32_t chan_blob(const WireString* s, uint8_t* out, int32_t cap) {
    const int32_t n = chan_blob_bytes(s);
    if (n < 0 || n > cap) {
        return -1;
    }
    uint32_t acc = 0;
    int32_t have = 0;
    int32_t u = kChanOpAt + 2;
    for (int32_t i = 0; i < n; ++i) {
        if (have < 8) {
            acc |= (static_cast<uint32_t>(s->data[u++]) - 1u) << have;
            have += 15;
        }
        out[i] = static_cast<uint8_t>(acc & 0xFFu);
        acc >>= 8;
        have -= 8;
    }
    return n;
}

bool chan_prefixed(const WireString* s) {
    return s != nullptr && s->data != nullptr && s->num > kChanPrefixLen &&
           memcmp(s->data, kChanPrefix, kChanPrefixLen * sizeof(wchar_t)) == 0;
}

// A named frame's name char count L1 + L2 (header values 3 and 4, sent + 1), or -1 when either is out of range.
int32_t chan_name_chars(const WireString* s) {
    const int32_t l1 = static_cast<int32_t>(s->data[kChanOpAt + 1 + 3]) - 1;
    const int32_t l2 = static_cast<int32_t>(s->data[kChanOpAt + 1 + 4]) - 1;
    return (l1 < 0 || l1 > kChanNameMax || l2 < 0 || l2 > kChanNameMax) ? -1 : l1 + l2;
}

// The char count of a text frame, or -1 when a char is not printable ASCII + 1, the count is out of range or the
// terminator is missing.
int32_t chan_text_chars(const WireString* s) {
    const int32_t chars = s->num - kChanOpAt - 2;
    if (chars < 1 || chars > kChanTextMax || s->data[s->num - 1] != L'\0') {
        return -1;
    }
    for (int32_t i = 0; i < chars; ++i) {
        const wchar_t c = s->data[kChanOpAt + 1 + i];
        if (c < 0x21 || c > 0x7F) { // printable ASCII 0x20 .. 0x7E, each + 1
            return -1;
        }
    }
    return chars;
}

// A text frame's chars as a NUL-terminated string in out (cap > kChanTextMax). Returns the length; the frame was
// checked by chan_parse.
int32_t chan_text(const WireString* s, char* out, int32_t cap) {
    const int32_t chars = chan_text_chars(s);
    if (chars < 0 || chars >= cap) {
        out[0] = '\0';
        return -1;
    }
    for (int32_t i = 0; i < chars; ++i) {
        out[i] = static_cast<char>(s->data[kChanOpAt + 1 + i] - 1);
    }
    out[chars] = '\0';
    return chars;
}

// The text frame for op in buf (kChanTextFrame wchar_ts, the caller's static buffer). num 0 when the text is empty,
// longer than kChanTextMax or not printable ASCII.
WireString chan_text_frame(wchar_t op, const char* text, wchar_t* buf) {
    const size_t len = strlen(text);
    if (len == 0 || len > static_cast<size_t>(kChanTextMax)) {
        return WireString{buf, 0, 0};
    }
    memcpy(buf, kChanPrefix, kChanPrefixLen * sizeof(wchar_t));
    buf[kChanOpAt] = op;
    for (size_t i = 0; i < len; ++i) {
        const char c = text[i];
        if (c < 0x20 || c > 0x7E) {
            return WireString{buf, 0, 0};
        }
        buf[kChanOpAt + 1 + i] = static_cast<wchar_t>(c + 1);
    }
    const int32_t num = kChanOpAt + 1 + static_cast<int32_t>(len) + 1;
    buf[num - 1] = L'\0';
    return WireString{buf, num, num};
}

// The opcode of a well-formed prefixed frame, with its arguments in *args; 0 for anything else.
wchar_t chan_parse(const WireString* s, ChanArgs* args) {
    const wchar_t op = s->num > kChanOpAt ? s->data[kChanOpAt] : L'\0';
    if (op == 0 || static_cast<size_t>(op) >= g_ops.arg_counts.size()) {
        return 0;
    }
    if (arg_count(op) == kChanArgsText || arg_count(op) == kChanArgsPacked) { // packed: the unpacked text frame
        *args = ChanArgs{};
        return chan_text_chars(s) > 0 ? op : 0;
    }
    if (arg_count(op) == kChanArgsBlob) {
        *args = ChanArgs{};
        return chan_blob_bytes(s) > 0 ? op : 0;
    }
    const bool named = arg_count(op) == kChanArgsNamed;
    const int32_t n = named ? kChanNamedHead : arg_count(op);
    const int32_t chars = named && s->num > kChanOpAt + kChanNamedHead ? chan_name_chars(s) : 0;
    if (n < 0 || n > kChanMaxArgs || chars < 0 || s->num != kChanOpAt + 1 + n + chars + 1 ||
        s->data[s->num - 1] != L'\0') {
        return 0;
    }
    *args = ChanArgs{};
    for (int32_t i = 0; i < n; ++i) {
        const wchar_t a = s->data[kChanOpAt + 1 + i];
        if (a == 0) {
            return 0;
        }
        args->v[i] = static_cast<uint32_t>(a) - 1;
    }
    return op;
}

// Host side: a frame from the client that owns pc. Any prefixed frame is swallowed.
void detour_server_change_name(void* pc, const WireString* name) {
    if (!chan_prefixed(name)) {
        g_orig_server_change_name(pc, name);
        return;
    }
    receive(g_ops.host, !client_session(), pc, name);
}

// Client side: a frame from the host on our PC. Any prefixed frame is swallowed.
void detour_client_message(void* pc, const WireString* s, uint64_t type, float life_time) {
    if (!chan_prefixed(s)) {
        g_orig_client_message(pc, s, type, life_time);
        return;
    }
    receive(g_ops.client, client_session(), pc, s);
}

} // namespace ue::chan
