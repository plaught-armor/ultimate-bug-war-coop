#pragma once
// Card 150: the packed row codec, one encoder and one decoder for every kChanArgsPacked op. Senders still build a
// text frame (chan_text_frame) and receivers still read one (chan_text): the transport packs the text into blob bytes
// at the real send and unpacks it before the ledger and the handler on the receiving side.
//
// Packed bytes: byte 0 is the form, 0 raw (the text's ASCII bytes follow, the fallback that always fits) or 1 tokens.
// Token form: the text split at ' ' and ';', each token one tag byte then its value:
//   tag bits 0-1: the delimiter after the token: 0 end of text, 1 ' ', 2 ';'
//   tag bits 2-4: kind: 0 empty, 1 decimal, 2 hex, 3 base64, 4 inline string, 5 dictionary ref, 6 dictionary def
//   tag bits 5-7: decimal: the fraction digit count (0..6); inline: the length (0..6), 7 = a varint length follows
//   decimal: the zigzag varint of the digits as one integer; only text that formats back to itself (no leading
//            zeros, no "-0", at most 18 digits) takes this kind
//   hex:     the varint value; only lower-case hex with a letter and no leading zero, at most 16 digits
//   base64:  the varint byte count, then the bytes; only standard base64 of 16+ chars that encodes back to itself
//   inline:  the chars; dictionary ref: the varint id; dictionary def: the varint id, the varint length, the chars
// The host's dictionary (strings of kPackDictMin..kPackDictLen chars) gives each string one id for the process; each
// connection has a set of the ids it has been sent (cleared with its pace queue slot), and a def is marked only when
// its frame is really sent, in wire order, so a ref always follows its def on that connection. A def names its id,
// so the client overwrites that slot and never needs a reset; redefining is always safe. A full dictionary sends the
// string inline. Nothing is dropped.

#include <stdint.h>

namespace ue::chan {

inline constexpr int32_t kPackIds = 4096;      // dictionary ids
inline constexpr int32_t kPackDictMin = 3;     // shorter strings go inline
inline constexpr int32_t kPackDictLen = 127;   // longer strings go inline
inline constexpr int32_t kPackSetWords = kPackIds / 64;

struct PackSet {
    uint64_t sent[kPackSetWords]; // ids this connection has been sent a def for
};

struct PackStats {
    uint32_t frames;       // frames packed and sent
    uint32_t raw;          // of them, sent in the raw form (tokens did not fit or were not smaller)
    uint64_t text_bytes;   // their text chars
    uint64_t packed_bytes; // their packed bytes
    uint32_t defs;         // dictionary defs sent
    uint32_t dict_full;    // strings sent inline because the dictionary was full
    uint32_t bad_frames;   // client: packed frames that did not unpack (dropped, a fail)
    uint32_t undefined;    // client: refs to an id never defined (a fail)
};

// Host: text (len chars) packed into out (cap bytes, >= len + 1). set null = no dictionary (inline strings). With
// commit, the defs written are marked in *set; without, *set is left as it was (a size probe). Returns the byte count.
int32_t pack_text(const char* text, int32_t len, PackSet* set, bool commit, uint8_t* out, int32_t cap);
// Client: packed bytes back to text in out (cap > the text); returns its length, or -1 for malformed bytes.
int32_t unpack_text(const uint8_t* in, int32_t n, char* out, int32_t cap);
PackStats pack_window(); // this window's numbers, then reset

} // namespace ue::chan
