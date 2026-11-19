#pragma once

// UE chan transport (split S7): the mod's own wire, binary frames in an FString over two engine RPCs.
//
// Frames ride two engine RPCs that carry an FString, both reliable and ordered (design doc section 1):
//   client to host: the client PC's ServerChangeName, received by the ServerChangeName_Implementation detour;
//   host to client: the target PC's ClientMessage stub (type None, lifetime 0), received on the client by the
//   ClientMessage_Implementation detour. Only remote PCs are sent to: on the listen server the host's own PC is
//   local, and ClientMessage on it would run the implementation on the host.
// A frame is the prefix "\x1F" (card 422), one opcode wchar_t, that opcode's fixed number of argument wchar_ts, then the
// terminator, so each opcode has an exact Num. Each argument goes on the wire as value + 1: no payload char may be
// L'\0', because a loaded FString is cut at its first null and the pure-ANSI check of FString serialization stops
// there. Values are kept below kChanArgMax, under the UTF-16 surrogate range, so the UCS-2 path keeps them intact.
// Receivers check the prefix with one memcmp and Num against the registered arg counts, then dispatch by opcode
// through the registered handler tables. Any prefixed frame is swallowed; malformed or unknown ones are dropped
// without logging (friends-only trust). Sends are edges only.
//
// Two opcode kinds have no fixed value count:
//   - a named frame (kChanArgsNamed): the prefix, the opcode, kChanNamedHead header values (the last two are the
//     name lengths L1 and L2, each at most kChanNameMax), then L1 + L2 name chars (printable ASCII, each + 1 like
//     every value), then the terminator: Num is kChanOpAt + 1 + kChanNamedHead + L1 + L2 + 1. chan_parse returns the
//     header in ChanArgs; the receiver reads the chars from the frame (chan_name_chars). The sender builds it.
//   - a text frame (kChanArgsText): the prefix, the opcode, then 1 to kChanTextMax printable ASCII chars (each + 1),
//     then the terminator. chan_parse checks the chars; the receiver reads them with chan_text.
//   - a blob frame (kChanArgsBlob, card 136 run 4): the prefix, the opcode, one length unit (the byte count, 1 to
//     kChanBlobBytes), then the bytes packed little-endian into 15-bit units (ceil(bytes * 8 / 15) units, the last
//     padded with zero bits), each unit + 1 like every value, then the terminator. The same packing rule as the
//     arguments: no unit is 0 and every unit is below the surrogate range. chan_parse checks the length unit against
//     the unit count; the receiver unpacks with chan_blob.
//   - a packed frame (kChanArgsPacked, card 150): the sender builds a text frame and the receiver reads one; on the
//     wire it is a blob frame of the packed bytes (ue/chan_pack.h), packed at the real send and unpacked before the
//     ledger and the handler.
//   - a batch frame (kChanArgsBatch, card 151, host to client only): the prefix, the batch opcode, then rows, then the
//     terminator and one stamp. A row is two length chars (the row's char count n as two 6-bit values, each + 0x20, so
//     a narrow batch stays ANSI), then n chars: a whole frame as it goes on the wire, without its prefix and
//     terminator (so a packed row stays packed). The rows of one batch are all narrow or all wide (a wide row would make
//     the narrow rows UCS-2 too). The ledger counts rows: the stamp carries the first row's seq and the receiver gives
//     row i seq + i, so the seq stays contiguous. The receiver splits the batch and runs each row like a frame of its
//     own, in order.
//
// Here an opcode is a raw wchar_t, and 0 means none; the op set lives with the mod.
//
// Wire string: the parms struct is one FString (TArray<TCHAR>: data, Num including the terminator, Max). The sender
// passes a static buffer it owns. ProcessEvent only reads the parms, and the generated ClientMessage stub copies the
// string into its own parms and frees that copy after ProcessEvent returns (0x0403B5DF), so the engine never frees
// or keeps this buffer.

#include <span>
#include <stddef.h>
#include <stdint.h>

namespace ue::chan {

struct WireString {
    const wchar_t* data;
    int32_t num;
    int32_t max;
};

inline constexpr int32_t kChanArgsNamed = -2; // a header, then name chars (see above)
inline constexpr int32_t kChanArgsText = -3;  // text chars only (see above)
inline constexpr int32_t kChanArgsBlob = -4;  // a length unit, then bytes packed in 15-bit units (see above)
inline constexpr int32_t kChanArgsPacked = -5; // card 150: text at both ends, packed bytes on the wire (see above)
inline constexpr int32_t kChanArgsBatch = -6;  // card 151: rows of other ops in one frame (see above)
inline constexpr int32_t kChanBatchRowHead = 2; // card 151: the length chars before each batch row
inline constexpr wchar_t kChanPrefix[] = L"\x1F"; // card 422: one unit; no game string starts with 0x1F
inline constexpr int32_t kChanPrefixLen = 1;
inline constexpr int32_t kChanOpAt = kChanPrefixLen;
inline constexpr int32_t kChanMaxArgs = 5;
inline constexpr int32_t kChanMaxFrame = kChanOpAt + 1 + kChanMaxArgs + 1;
inline constexpr int32_t kChanNamedHead = 5; // the header values; the last two are L1, L2
inline constexpr int32_t kChanNameMax = 64;  // chars per name
inline constexpr int32_t kChanNamedFrame = kChanOpAt + 1 + kChanNamedHead + 2 * kChanNameMax + 1;
static_assert(kChanNamedHead <= kChanMaxArgs);
inline constexpr uint32_t kChanArgMax = 0x7FFF; // argument values are 0 .. kChanArgMax - 1
inline constexpr int32_t kChanTextMax = 1400;   // text chars per frame
inline constexpr int32_t kChanTextFrame = kChanOpAt + 1 + kChanTextMax + 1;
inline constexpr int32_t kChanBlobUnits = kChanTextMax;                   // the length unit and the data units
inline constexpr int32_t kChanBlobBytes = (kChanBlobUnits - 1) * 15 / 8; // 2623 bytes per frame
inline constexpr int32_t kChanBlobFrame = kChanOpAt + 1 + kChanBlobUnits + 1;
static_assert(kChanBlobFrame <= kChanTextFrame && kChanBlobBytes < static_cast<int32_t>(kChanArgMax));
inline constexpr size_t kChanOpSlots = 96; // card 451: 64 full at Bark; opcodes counted by chan_client_sends (0 also counts out-of-range ones)

struct ChanArgs {
    uint32_t v[kChanMaxArgs]; // the first (arg count of the op) are used
};

// --- Registration ---------------------------------------------------------------------------------------------

using ChanHandler = void (*)(void* pc, const WireString* s, const ChanArgs& args);
// Card 132: the stamp every sent frame carries just before its terminator, added by the send functions and taken off
// by the receiving detours before parsing, so no handler or sender sees it: kChanStampMark, then the seq and the
// sender's ms (its tick count modulo 2^30) as two 15-bit values each, every value + 1 like an argument.
struct ChanStamp {
    uint32_t seq; // per direction (the host: per remote PC), from 1
    uint32_t ms;  // the sender's GetTickCount64() & kChanStampMsMask; card 474: a paced frame's enqueue time
    bool held;    // card 474: the frame waited in the host's pace queue, so ms is earlier than its send
};
// Card 474: the held flag travels as this bit of seq (both stamp forms carry 30 bits of seq; a session never sends
// 2^29 frames on one direction). unstamped() strips it, so no receiver sees it in seq.
inline constexpr uint32_t kChanStampHeld = 1u << 29;
inline constexpr wchar_t kChanStampMark = 0x1E;
inline constexpr int32_t kChanStampLen = 5;
// Card 390: a frame whose chars are all below 0x80 (every text frame) takes the narrow stamp instead: the 15-bit
// values of the stamp above made FString serialization UCS-2, 2 bytes for every char of the frame. The narrow stamp
// is kChanStampNarrowMark, then seq and ms (30 bits each) as ten 6-bit values, each + 0x20, so the frame stays ANSI.
// The receiver looks for the wide mark first; a narrow stamp's chars (0x20 to 0x5F) are never that mark.
inline constexpr wchar_t kChanStampNarrowMark = 0x1D;
inline constexpr int32_t kChanStampNarrowLen = 11;
inline constexpr uint32_t kChanStampMsMask = (1u << 30) - 1;
// When on_recv runs for a frame: before its handler, after it ran, or after none ran (no handler on this side).
enum class ChanRecv : uint8_t { Before, Ran, NoHandler };
struct PaceLive; // card 140, below
struct ChanOps {
    std::span<const int32_t> arg_counts;  // by op; -1 = not a valid op
    std::span<const ChanHandler> host;    // by op; run by ServerChangeName when *client_session is false
    std::span<const ChanHandler> client;  // by op; run by ClientMessage when *client_session is true
    const bool* client_session;
    bool (*is_remote_pc)(const void* pc); // host: pc is a remote client's PC (the only PCs sent to)
    // client: a send to the host was skipped (no PC, no ServerChangeName function, or no opcode); the mod logs it
    void (*send_skipped)(unsigned op, const void* pc, const void* function, int32_t num);
    // Optional (null = none), card 132's ledger. on_send runs for every frame at a send function, just before it
    // goes out, with unsent naming why it does not go (null: it is sent); it returns the stamp the frame carries.
    // on_recv runs for every prefixed frame received, before its handler and after it (phase), whether or not this
    // side has a handler; stamped is false for a frame that carried no stamp; s is the frame without its stamp.
    // Card 474: enq_ms is the GetTickCount64() the frame entered the host's pace queue (0: not queued, sent as built).
    ChanStamp (*on_send)(void* pc, const WireString& s, bool to_host, const char* unsent, uint64_t enq_ms);
    void (*on_recv)(void* pc, wchar_t op, const ChanStamp& stamp, bool stamped, ChanRecv phase, const WireString& s);
    // Optional (null = none), card 139. Host: why a frame with opcode op to remote PC pc is held back (null: it goes).
    // chan_send_client asks it for every frame; a held frame goes to on_send as unsent with this text.
    const char* (*client_hold)(const void* pc, wchar_t op);
    // Optional (null = unpaced), card 140. Host: pc's connection now, read before each paced send; false = no reading.
    bool (*pace_live)(const void* pc, PaceLive* out);
    // Optional (null = all pass), card 332 (client, ClientMessage) and card 348 (host, ServerChangeName). A prefixed
    // frame received on pc: false drops it before on_recv and its handler (the caller counts it), so the ledger's
    // receive sequence is not touched.
    bool (*frame_ok)(const void* pc, wchar_t op);
    // Card 151, host: the batch opcode (its arg count is kChanArgsBatch); 0 = no batching, every frame goes alone.
    wchar_t batch_op;
};
void set_ops(const ChanOps& ops); // once, by the caller before ue::install

// --- Frames ---------------------------------------------------------------------------------------------------

// Game thread only. Prefilled with the prefix; senders write only the opcode, argument and terminator slots.
extern wchar_t g_chan_wire[kChanMaxFrame];

WireString chan_frame(wchar_t op, const ChanArgs& args);
bool chan_prefixed(const WireString* s);
int32_t chan_name_chars(const WireString* s);
int32_t chan_text_chars(const WireString* s);
int32_t chan_text(const WireString* s, char* out, int32_t cap);
WireString chan_text_frame(wchar_t op, const char* text, wchar_t* buf);
// The blob frame for op in buf (kChanBlobFrame wchar_ts, the caller's static buffer); num 0 when n is out of range.
WireString chan_blob_frame(wchar_t op, const uint8_t* bytes, int32_t n, wchar_t* buf);
// A blob frame's bytes in out (cap >= kChanBlobBytes); returns the count, or -1 for a malformed frame.
int32_t chan_blob(const WireString* s, uint8_t* out, int32_t cap);
wchar_t chan_parse(const WireString* s, ChanArgs* args);

// --- Sends ----------------------------------------------------------------------------------------------------

bool chan_send_host(void* pc, WireString parms);
// Card 151: with batching on, the frame waits in pc's pace queue until the tick's flush (chan_flush_all), which sends
// the queued rows in batch frames. batch_start: the frame never joins a batch begun before it (a Digest B frame).
bool chan_send_client(void* pc, const WireString& s, bool batch_start = false);
// Card 294, host: a priority frame sent now, ahead of pc's paced queue (frames queued ahead of it in *queued_ahead).
// Card 151: pc's rows of this tick are flushed first (under the gate), so a priority frame overtakes only rows that
// already waited past a flush.
bool chan_send_client_now(void* pc, const WireString& s, uint32_t* queued_ahead);
// Card 151, host: sends pc's queued rows now (batched, under the gate); rows the gate holds wait as before.
void chan_flush_client(void* pc);
// Card 151, host: the end of the tick: every queue offered a row this tick is flushed as chan_flush_client does.
void chan_flush_all();
bool chan_to_host(void* pc, wchar_t op, uint32_t arg);
// Card 140, host: send pacing (chan.cpp has the gate). Each tick, before any mod step sends, the caller lists the live
// remote PCs; queued frames of PCs not listed are dropped (returned count). chan_pace_drop drops one PC's queue
// (Logout). chan_pace_window returns and resets this window's numbers.
inline constexpr int32_t kChanPaceSlots = 4;
struct PaceLive {
    int64_t pending_bits; // QueuedBits + SendBuffer bits of the PC's connection, now
    int32_t reliable_out; // NumOutRec on the PC's channel after its last RPC
    int32_t net_speed;    // UPlayer::CurrentNetSpeed, bytes per second
    int32_t silent_ms;    // card 391: connection tick time minus the time of the last packet from the client
};
struct PaceConn {
    void* pc;
};
struct PaceStats {
    uint32_t peak_frames;    // queue depth peak, frames
    int64_t peak_bytes;      // queue depth peak, bytes (as UCS-2)
    uint64_t max_wait_ms;    // the longest a frame waited, sent or still queued at the window end (card 290)
    uint32_t paced;          // frames that waited
    int64_t drained_bytes;   // bytes of the frames sent from the FIFO
    int64_t offered_bytes;   // bytes of the frames handed to chan_send_client and not held (sent now or queued)
    int64_t sent_bytes;      // bytes of every frame sent to a client
    uint32_t full_sends;     // frames sent past the gate because the FIFO was full (a fail)
    uint32_t no_slot;        // frames to a PC with no free queue, sent unpaced
    uint32_t no_reading;     // gate checks with no connection reading (sent)
    uint32_t holds_bits;     // drain steps held by the bits gate
    uint32_t alone_sends;    // frames that did not fit and went alone (run 4: at most one per connection per tick)
    uint32_t holds_rel;      // drain steps held by kPaceRelHold reliable bunches out
    uint32_t holds_silent;   // card 391: drain steps held because the client sent nothing for kPaceSilentMs
    int32_t rel_peak;        // the most reliable bunches out seen at a gate check
    int64_t pending_peak;    // the most QueuedBits + SendBuffer bits seen at a gate check
    uint32_t dropped;        // queued frames whose PC went away
    int64_t frame_bytes_max; // the most chan bytes sent between two steps
    uint32_t priority;       // card 294: priority frames sent now, skipping the FIFO
    uint32_t batches;        // card 151: batch frames sent
    uint32_t batched_rows;   // card 151: rows those batch frames carried
};
uint32_t chan_pace_step(const PaceConn* conns, int32_t n, uint64_t now_ms);
uint32_t chan_pace_drop(const void* pc);
// Card 151: rows that waited past a flush (this tick's rows not yet flushed are not a backlog).
uint32_t chan_pace_depth(const void* pc);
PaceStats chan_pace_window();
bool chan_to_client_args(void* pc, wchar_t op, const ChanArgs& args);
bool chan_to_client(void* pc, wchar_t op, uint32_t arg);
// Host: frames sent to remote PCs so far with this opcode (slot 0: opcode 0 or past kChanOpSlots).
uint32_t chan_client_sends(size_t op);
// Both sides, card 136 run 4: serialized FString bytes of every frame sent and received so far, by opcode (slot 0: opcode
// 0 or past kChanOpSlots). A frame is 4 bytes (the length) plus Num times 1 when every unit is below 0x80 (ANSI), else
// times 2 (UCS-2), stamp included.
uint64_t chan_bytes_sent(size_t op);
uint64_t chan_bytes_received(size_t op);

// --- The two receiving detours and the engine functions (resolved by kHooksUe / kCallsUe) ------------------------

using ServerChangeNameFn = void (*)(void* pc, const WireString* name);
using ClientMessageFn = void (*)(void* pc, const WireString* s, uint64_t type, float life_time); // FName by value
extern ServerChangeNameFn g_orig_server_change_name;
extern ClientMessageFn g_client_message;      // APlayerController::ClientMessage, the exported RPC stub
extern ClientMessageFn g_orig_client_message; // APlayerController::ClientMessage_Implementation

void detour_server_change_name(void* pc, const WireString* name);
void detour_client_message(void* pc, const WireString* s, uint64_t type, float life_time);

} // namespace ue::chan
