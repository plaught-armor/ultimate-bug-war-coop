# Card 058: Final rehearsal on the playtest build

Status: sent 2026-11-04 (senior). Playtest gate: the last card before 8 pm.

Read the card 055 Report (its three sessions and the `playtest.sh` options) and `docs/PLAYTEST.md`. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Cards 057, 054 and 053 landed after card 055's rehearsal. Each was proven only by its own scripted run, under its own `UBWCOOP_*` test flag. The user will run the build without any test flag, through `tools/playtest.sh menu`. This card runs that exact build once more, the way card 055 did, and adds the new features in the order a player meets them.

## Do

1. **Build and deploy once.** This is the build the user plays. After this card, no code change goes in before 8 pm unless it fixes a fault found here.
2. **One session**, through `tools/playtest.sh menu` exactly as card 055 ran it: host first, the client with `SECOND=1`, `WINDOWED=1 RES=640x360`, side by side, against `127.0.0.1`. Set no `UBWCOOP_*` variable by hand. Drive it with the card 055 driver approach (real keys); the test flags of cards 050, 053, 054 and 057 stay off.
   - Host and join through the F8 panel, then go through the movies and the brief.
   - **Weapons (057).** The client takes a weapon, presses **2**, and fires at a bug. Then the host takes the tactical support case and presses **2** on its own player. This is the one switch still untested (card 057 watch item).
   - **Interactions (054).** The client walks to the zipline and the satchel site and presses Use at each. Log what each screen shows. A zipline ride that jerks back and forth is a finding.
   - **Checkpoint (053).** The host's player dies; RESTART CHECKPOINT. The client's list must match the host's. Then one objective must complete on both screens, with no duplicate item.
   - **Leave and rejoin.** The client quits and rejoins through the panel.
3. **Check the logs** with `python3 -I tools/logsum.py <tag>`: no ERROR/FATAL, and every WARN explained. Also check the per-session wall times against card 055's: join to possess about 2.9 s.
4. **Fix only what breaks.** Use a narrow inline fix, a rebuild, and a rerun of only the part that broke. Anything bigger is written up as a finding for the user, not fixed.
5. **Update `docs/PLAYTEST.md`.** Remove any watch item this run settles, and add anything a player must know.

## Done when

- One clean session covers every bullet in step 2, with rc 0 on both games and no new dump.
- Shots of both screens are taken at: the client holding the rifle, the host holding the laser rifle, the zipline, the client panel after the checkpoint, and the rejoin.
- The report states the deployed build hash (the `deployed:` line), so the user plays exactly that build.

## Report

**Deployed build: `a4d3f58896432b28`** (`deployed a4d3f58896432b28` in `logs/card058e-host.run`; the second game used the same deployed DLL; `build/dwmapi.dll` and the game's `dwmapi.dll` both hash to it). Play this build.

### Faults found and fixed (each a narrow fix, rebuild, rerun)

Earlier sessions of this card voided four builds:
- `a3be594e91929180`: client weapon count mismatch.
- `9fefc128a0744cdc`: dead look.
- `e9bd684470a8725b`: minimap crash.
- `22fb73ca24a6fd80` (session D): FATAL when the host shot a shooting-range target. `detour_overkill_effect` (`mod/src/hooks.cpp`) passed the health component's owner to `c39_health_text`, which reads +0x688 as a pointer; on a range target that field is a double. The fix calls the game's original for any owner that is not an NPC. Session E shot both ranges (10/10 targets, then the tactical range) with no crash.

### Session E (the final session, build a4d3)

`tools/playtest.sh menu`, host first, client with `SECOND=1`, `WINDOWED=1 RES=640x360`, 127.0.0.1, no `UBWCOOP_*` variable, real keys.

| Step | Result | Shots (`logs/card058-shots/`) |
|---|---|---|
| Host via F8 panel, join via panel | Pass. Join to possess **5.2 s** (592385581 to 592390829 ms), over card 055's 2.9 s. | `e2-client-join.png` |
| Client takes weapon, 2, fires | Pass: E at the case, 2 draws the Morita, fires on both screens. | `E-client-rifle.png`, `e2-client-fire.png` |
| Host takes tactical case, 2 (laser) | **Not reached.** The LaserMorita pickup and the tactical support case were already consumed by the time the host got there. | none |
| Client zipline, satchel site | **Not reached** (user chose to skip). | none |
| Tactical support training, shooting range | Completed; lists matched on both screens, no duplicate item. | `e2-client-obj.png`, `E-client-objdone.png` |
| Host death | **Not reached.** A frag grenade at the host's feet did not kill it. | `e2-h-gren.png` |
| RESTART CHECKPOINT (host, from the pause menu while alive) | Pass. Host snapshot 16 objectives and 50 journal frames; client rebuilt to 16. Both lists show ASSAULT COURSE, "Complete regulation physical training". No objective completed after the checkpoint (the remaining one is the full assault course). | `e2-h-rcp.png`, `e2-h-after-rcp.png`, `e2-c-after-rcp.png`, `e2-h-obj-rcp.png`, **`e2-c-obj-rcp.png`** |
| Client quit and rejoin via panel | Pass. Pause QUIT, YES, hangar, J, Enter. Join to possess **3.9 s** (593527106 to 593530980 ms). | `e2-c-quitconfirm.png`, `e2-c-title.png`, **`e2-c-rejoin.png`** |
| Close | Both games quit from the hangar (Esc, CONFIRM). | `e2-c-exitdlg.png`, `e2-h-exitdlg.png` |

Exit codes: `umu-run rc 0` in `logs/card058e-host.run`, `card058e-client.run` and `card058e-client2.run`. Crash dumps: 23 before and after (no new dump).

### Logs

`python3 -I tools/logsum.py card058e` (host and client2 logs copied to `logs/card058e-{host,client}.log`) and `card058e1` (the first client, `logs/card058e1-client.log`):
- ERROR/FATAL 0 in all three.
- WARN: one, on the host. `WARN card049: host journal full (128 frames): later frames are sent live but not recorded` at 592800566. The long pre-checkpoint run (two ranges) filled the 128-frame objective journal. Later frames still went live to the connected client. RESTART CHECKPOINT rebuilds the journal (50 frames afterwards), and the rejoin came after that, so it got the full list. Risk: a client that joins late in a long stretch with no checkpoint may miss objective frames beyond 128.

### Operator mishaps (not mod faults)

- The first client (`LOG_TAG=client`) quit 2 s after "join armed". Escape had not closed the hangar QUIT dialog, so the Enter meant for the join confirmed the quit. The game exited cleanly (rc 0, no dump). It was relaunched as `client2`.
- During the rejoin, keys typed in another window while the client had focus landed in the join box ("my sore", `Invalid address`). The box keeps its last text, so the next J and Enter failed the same way. Retyping the address joined at once.

### Findings for the user

The first four come from scripted driving (blind navigation by position reads and fixed mouse moves). The client may not have been looking at the right place, so treat them as unconfirmed until a person sees them tonight.


- The shooting range targets show only on the host's screen.
- The client's minimap shows no icons.
- Before the checkpoint, the client's assault course text once read "Approach the start line" while the host's read "Complete regulation physical training". After the checkpoint they matched.
- The host's health dropped to 80 at the range; cause unknown.
- The join box does not reset to the saved address after an invalid entry.
- `tools/run_pair.sh` line 115 still hard-codes GE-Proton11-11; `playtest.sh` uses GE-Proton (11-12 here). Not touched (no code change before 8 pm).

### Not reached without test flags

The host's laser switch (card 057 watch item), the client's zipline and satchel site (card 054), and the host's death followed by a completion after the checkpoint (card 053). `docs/PLAYTEST.md` keeps them as watch items for the playtest.

### PLAYTEST.md

Updated: GE-Proton11-12; client pickup steps; host laser watch item kept; keys (E, 4, R, Tab); quitting from the hangar; keep hands off focused windows; join box keeps the last text; TRAITOR on self-kill; range targets host-only and the crash fix; client minimap; tactical range flow; checkpoint list match confirmed.

## Senior review (2026-11-04): accepted. Play build a4d3f58896432b28

I checked these myself:
- **Build.** `build/dwmapi.dll` and the game's `dwmapi.dll` both hash to `a4d3f58896432b28`. All three `.run` files end `umu-run rc 0`. fnlen gives 1172 functions, 0 over 60 lines.
- **`detour_overkill_effect`.** It now reads the owner as a character only when the owner is an NPC (`is_a(owner, g_npc_class())`). Everything else goes straight to the original. The first cut was by symptom (a double at +0x688), but the fix is by class, which is right. Range targets lose nothing, because the gib relay is for NPCs only.
- **`sync_client_minimap_origin`.** It does on the client what `OnPossess` does on the server only. It guards a null pawn and a null minimap, and it uses the same game function. That fixes the null+0x1B8 read in `GetMinimapLocation`. The playtest would have hit that crash at the assault course markers, so this card paid for itself.

What is not reached (the host's laser switch, the zipline and satchel site, a completion after a death checkpoint) stays as PLAYTEST watch items. Each was proven before under its card's test flag on an earlier build, and card 059 reruns those routes on this build.
