#pragma once

// The UBW op set of the chan (split S7). The transport is ue/chan.h; the handlers and the two dispatch tables are
// in ubw/chan_ops.cpp.
//
// Card 049: ChanOp::Objective (kChanArgsNamed in kChanArgs) is the one opcode without a fixed count. Its frame is
// the prefix, the opcode, kChanObjectiveHead header values (objective kind, flags, count, name lengths L1 and L2,
// each L at most kChanNameMax), then L1 + L2 name chars (printable ASCII, each + 1 like every value), then the
// terminator: Num is kChanOpAt + 1 + kChanObjectiveHead + L1 + L2 + 1. chan_parse returns the header in ChanArgs;
// the receiver reads the chars from the frame (chan_name_chars). Host to client only, built by c49_frame.
// Card 050: the text opcodes (kChanArgsText) carry no fixed values: the prefix, the opcode, then 1 to kChanTextMax
// printable ASCII chars (each + 1), then the terminator. UseName (client to host) is the name of the placed actor the
// client's interactor targets, sent just before its Use 1; Weapons (host to client) is the server copy's weapon list
// (see c50_text). chan_parse checks the chars; the receiver reads them with chan_text.
// Card 057: Equip (client to host, text since card 312) is "<client slot>[ <definition class path>]", the slot the
// client's switch key asked for and the class in it; the host equips its copy's slot of that class
// (c57_receive_equip).
// Card 141: Switch (client to host, 1 value) is a switch request the Equip frame cannot carry: the underslung toggle
// or next/previous, packed by c141_pack; the host replays it on the server copy (c141_receive_switch).
// Card 143: Reload (host to client, text) follows a host LoadFromSlot in place: "<health> <armour>;<weapons>;<ammo>",
// the server copy's restored state (c50_text and c72_map_text formats); the client rejoins and sends the same text
// back as Restore (client to host, text) at the first host ping after the reconnect; the host applies it to the new
// server copy (restore_c143.cpp).
//
// Card 133: HoldDone (client to host, text) is "name x y z": the client's own timed (hold) interaction on a
// card 054 relay class completed on that placed actor; the host runs the completion on its copy (hold.cpp). Card 418:
// host to client, the same text is a satchel site whose fuse started on the host; the client arms its copy.
// Card 125: TurretAim (client to host, 2 values) is the camera yaw and pitch of the turret the client drives, each in
// tenths of a degree + kC125AimBias (c125_host_aim).
//
// Card 132: Digest (host to client, text) is one frame of the host's world state digest round (digest_c132.cpp);
// DigestQuery (client to host, text) is "D<round> <slot> ...", the slots whose sums differ (the host sends their
// entries to that client), or "Q<round> <key> ...", the host-only keys of a round, logged by the host.
// LedgerAck (host to client, 1 value) is the last client seq the host received, modulo kChanArgMax, sent in its
// ping (ledger_c132.cpp). Every frame of every op carries the ledger stamp (ue/chan.h ChanStamp).
// Hit (host to client, text) is one tick's NPC hits, each "<guid hex> <x> <y> <z> <nx> <ny> <nz> <damage> <wound>
// <bone> <hit bone>;", applied by the client's own USTGoreComponent::OnDamage (hit_c132.cpp).
// Pose (host to client, text) is a corpse's settled physics pose, "P <guid> <snap> <part> <parts> <bodies> <flags>
// <base64>" chunked to kChanTextMax, or "R <guid>" when the corpse is gone (pose_c132.cpp).
// Decal (host to client, text) is the gore decal relay: M/T table rows, D spawns, E releases, B bursts, R round
// markers and the J journal start, each ended by ';' (decal_c132.cpp).
// GibPart (host to client, text) is the gib part relay: S spawn and launch rows, each ended by ';' (gib_c137.cpp);
// the join replay also sends its M, R rows as text. Card 519: GibPose (host to client, blob) carries the live M
// in-flight samples, R rest rows and G taken-back rows in binary (gib_c137.cpp, gib_pose_put).
//
// Card 166: Marker (host to client, text) is a server copy's OnDamageCaused or OnDeathCaused for that client's
// marker, "<D|K> <guid> <damages> <counts> <values> <tags>" (marker_c166.cpp).
//
// Card 135: IntroStart (host to client, text "<map> <bFromSave 0|1>") is the host leaving its press-to-start
// screen; the client calls GameStarted(bFromSave) on its own HUD once per map (ubw/intro_c135.cpp).
// Card 397: TravelIds (host to client, text "<map> <chapter> <level>") goes out before a travel with clients: the
// IDs the client's own loading screen copy shows on that map (render_c337.cpp).
// Card 320: IntroDone (client to host, 1 value, unused 0) is the client leaving its own press-to-start screen (or
// giving up on it); the host's title-screen hold waits for one per connected client (c320_receive_done).
// Card 208: BugForm (host to client, 3 values) is a bug pawn's equipped form: v0, v1 = the pawn's NetGUID (card 209,
// 14 bits each), v2 = the form index in its FormsInventory (c208_receive).
// Card 209: Down (host to client, 5 values) is {pawn NetGUID low, high, life state, bleedout seconds, host slot};
// Warp (host to client, 5 values) is the target pawn's NetGUID {low, high}, then the host's spot minus the target's
// location per axis (card 461), each uu + kC46OffsetBias.
// Card 260: EndLevel (host to client, 1 value, unused 0) is the host's USTGameInstance::EndLevel; the client runs
// its own once per map (c260_receive).
// Card 181: Round (client to host, 1 value, unused 0) is one round of the client's own soldier weapon (one
// USTWeaponFireAbility::Fire that made a projectile); the host copy fires one Fire per Round (c181_receive_round).
//
// Card 455: HurtSound (host to client, text "<sound path> <volume> <pitch>") is a 2D hurt sound the player
// Blueprint played for a server copy on the host (skipped there by card 160); the client plays it.
// Card 475: Score (host to one client, text) is the host's FedScore at a join replay: "R <tag>=<sum>/<count>;..."
// rows of its score history summed per tag (tag "-": the untagged rest), then "E <total> <rows>" (score_c475.cpp).
// Card 459: MechFuel (host to one client, blob of 8 bytes: Fuel, MaximumFuel as floats) is the current fuel of the
// mech that client pilots; the client writes both into its own mech's STPlayerMechAttributeSet (c459_receive).
//
// Card 532: NpcTarget (host to client, blob) carries rows of (NPC NetGUID, target NetGUID << 2 | HasLineOfSight) as
// two varints: each change of an NPC's primary target or its line of sight (target_c532.cpp).
// Card 561: CamFade (host to one client, blob of kC561FadeBytes) is a camera fade a pawn graph called on the host for
// that client's own pawn (skipped on the host's screen); the client runs it on its own camera manager (c561_receive).
// Card 601: DropshipBoard (host to client, text) is the placed name of a transport dropship whose held OnInteraction
// (card 595) the host just ran; the client runs its own copy's original so its level script outro chain plays.
// Card 602: SwitchPress (host to client, text) is the placed name of a switch whose OnInteraction broadcast
// OnPressSuccessful on the host; the client runs its own copy's OnInteraction so its level script's bound graph runs.
// Card 151: Batch (host to client) carries rows of other ops in one frame, one ledger stamp (ue/chan.h).
//
// (kChanObjectiveHead is now ue::chan::kChanNamedHead.)

#include "ue/chan.h"

#include <stddef.h>
#include <stdint.h>

namespace ubw::chan {

enum class ChanOp : wchar_t { None, Fire, Ping, Pong, Gib, Anim, Down, Use, Warp, Objective, UseName, Weapons, Equip, Activate, Ammo, AmmoSet,
                              AmmoMirror, ObjectiveActors, GrenadeItem, GrenadeSpawn, GrenadeExplode,
                              MechEnter, MechLeave, DoorOp, CollisionOp, WorldOp, TacSupport, TacCharge, Difficulty, TurretMount,
                              TurretLeave, TurretAim, HoldDone, Digest, DigestQuery, LedgerAck, Hit, Pose,
                              Decal, Dropship, Fx, GibPart, Clock, Switch, Reload, Restore, WarpTo, HitClaim, Marker, BugForm, IntroStart, EndLevel, Round, ScriptWrite, IntroDone, Radio, TravelIds, HurtSound, MechFuel, Score, PeerWeapons, Batch, Stamina, CombatLevel, Bark, Role, GibPose, NpcTarget, CamFade, DropshipBoard, SwitchPress, LevelAudio, HordeZone, Count };
inline constexpr int32_t kChanArgsNamed = ue::chan::kChanArgsNamed; // card 049: a header, then name chars (see above)
inline constexpr int32_t kChanArgsText = ue::chan::kChanArgsText;   // card 050: text chars only (see above)
inline constexpr int32_t kChanArgsBlob = ue::chan::kChanArgsBlob;   // card 136 run 4: bytes in 15-bit units
inline constexpr int32_t kChanArgsPacked = ue::chan::kChanArgsPacked; // card 150: text rows packed on the wire
inline constexpr int32_t kChanArgsBatch = ue::chan::kChanArgsBatch;   // card 151: rows of other ops in one frame
// Argument count per opcode; -1 = not a valid opcode.
inline constexpr int32_t kChanArgs[] = {-1, 1, 1, 1, 3, 3, 5, 1, 5, kChanArgsNamed, kChanArgsText, kChanArgsText, kChanArgsText, kChanArgsText,
                                       kChanArgsText, kChanArgsText, kChanArgsText, kChanArgsText, kChanArgsText,
                                       kChanArgsPacked, kChanArgsPacked, kChanArgsText, kChanArgsText,
                                       kChanArgsText, kChanArgsText, kChanArgsText, 1, kChanArgsText, kChanArgsText,
                                       kChanArgsText, kChanArgsText, 2, kChanArgsText, kChanArgsText, kChanArgsText,
                                       1, kChanArgsPacked, kChanArgsPacked, kChanArgsPacked, kChanArgsText, kChanArgsBlob,
                                       kChanArgsPacked, kChanArgsText, 1, kChanArgsText, kChanArgsText, kChanArgsText,
                                       kChanArgsText, kChanArgsText, 3, kChanArgsText, 1, 1, kChanArgsText, 1, kChanArgsText,
                                       kChanArgsText, kChanArgsText, kChanArgsBlob, kChanArgsText, kChanArgsText,
                                       kChanArgsBatch, 1, 1, kChanArgsText, 1, kChanArgsBlob, kChanArgsBlob, kChanArgsBlob, kChanArgsText, kChanArgsText, kChanArgsText, kChanArgsText}; // card 606: HordeZone; card 604: LevelAudio; card 602: SwitchPress; card 601: DropshipBoard; card 561: CamFade; card 532: NpcTarget; card 519: GibPose; card 490: Stamina carries the host setting (0 or 1); card 321: CombatLevel 0/1/2; card 451: Bark; card 502: Role
static_assert(sizeof(kChanArgs) / sizeof(kChanArgs[0]) == static_cast<size_t>(ChanOp::Count));
static_assert(static_cast<size_t>(ChanOp::Count) <= ue::chan::kChanOpSlots);
inline constexpr size_t kChanOpCount = static_cast<size_t>(ChanOp::Count);

// The host and client handler tables, defined in ubw/chan_ops.cpp; ubw::init passes them to ue::chan::set_ops.
const ue::chan::ChanOps& chan_ops();

// Typed wrappers over the ue::chan transport, so call sites keep their spelling. chan_parse has the same parameters
// as ue::chan::chan_parse, so an unqualified call from a scope that also sees ue::chan (by ADL on WireString) is
// ambiguous: qualify it as ubw::chan::chan_parse.
inline bool chan_to_host(void* pc, ChanOp op, uint32_t arg) {
    return ue::chan::chan_to_host(pc, static_cast<wchar_t>(op), arg);
}
inline bool chan_to_client(void* pc, ChanOp op, uint32_t arg) {
    return ue::chan::chan_to_client(pc, static_cast<wchar_t>(op), arg);
}
inline bool chan_to_client_args(void* pc, ChanOp op, const ue::chan::ChanArgs& args) {
    return ue::chan::chan_to_client_args(pc, static_cast<wchar_t>(op), args);
}
inline ue::chan::WireString chan_frame(ChanOp op, const ue::chan::ChanArgs& args) {
    return ue::chan::chan_frame(static_cast<wchar_t>(op), args);
}
inline ue::chan::WireString chan_text_frame(ChanOp op, const char* text, wchar_t* buf) {
    return ue::chan::chan_text_frame(static_cast<wchar_t>(op), text, buf);
}
inline ChanOp chan_parse(const ue::chan::WireString* s, ue::chan::ChanArgs* args) {
    return static_cast<ChanOp>(ue::chan::chan_parse(s, args));
}

} // namespace ubw::chan
