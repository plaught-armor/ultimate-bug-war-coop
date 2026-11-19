#pragma once

#include "ubw/flags.h"

// Game-side start-up. Functions are located by RVA from offsets.generated.h (PDB publics).

namespace ubw {

// Stores the dllmain flags, hands the engine steps and the channel ops to ue/, then calls ue::install with
// the game hook set. The card flags and their install gates are read once every table row is resolved.
// Logs every step; on any failure logs FATAL/ERROR and installs nothing more.
// Must not run under the loader lock (MinHook suspends threads).
// Arms the one-shot auto-travel from `travel` (no travel when !travel.enabled).
// `client_game_only` (UBWCOOP_CLIENT_GAMEONLY) enables the card 009 client input-mode fallback.
// `autoinput` (UBWCOOP_AUTOINPUT) enables the card 014 scripted input (autoinput.h).
// `auto_dropship` (UBWCOOP_AUTODROPSHIP) enables the card 015 host dropship landing.
// `grant` (UBWCOOP_GRANT_WEAPON) enables the card 021 test loadout.
// `kill_trooper` (UBWCOOP_KILL_TROOPER) enables the card 025 NPC list and test trooper kill.
// `dump_sprite_anims` (UBWCOOP_DUMP_SPRITE_ANIMS) enables the card 027 sprite animation dump and the card 029
// sprite set summary.
// `audit_029` (UBWCOOP_AUDIT_029) enables the card 029 weapon, mech and turret audit.
bool init(const ue::config::Travel& travel, bool client_game_only, const config::AutoInput& autoinput,
          bool auto_dropship, const config::GrantWeapon& grant, bool kill_trooper, bool dump_sprite_anims,
          bool audit_029);

// Card 155: the session-end summary line (crash_line, safe at DLL detach).
void playlog_session_end();

// Card 332: the teardown row if a teardown is still open at DLL detach (the play build has no quit command).
void sync_detach();

} // namespace ubw
