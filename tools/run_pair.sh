#!/usr/bin/env bash
# Unattended host + client test run of the UBW co-op mod.
#
# Usage: tools/run_pair.sh <tag> [seconds]
#   <tag>     log name prefix, e.g. card014 -> logs/card014-{host,client}.{log,stdout}
#   [seconds] hard wall-time cap for the whole run (default 180)
#
# Env (optional):
#   CPUS, NICE, FPS_CAP   low footprint (user 2026-11-13: benchmarks run in other projects). The whole run
#                     (both games, driver) is pinned to CPUS (default 4-7,12-15: four cores with their
#                     siblings) at NICE (default 10), and both games are capped at FPS_CAP frames per second
#                     (default 60; the frame gate needs 50) through DXVK_FRAME_RATE / VKD3D_FRAME_RATE.
#                     CPUS=all, NICE=0, FPS_CAP=0 turn each off.
#   DEPLOY=1          copy build/dwmapi.dll into the game first (verifies 84 exports, KERNEL32-only)
#   SOUND=1           keep game audio audible (default: route both instances to a
#                     PulseAudio null sink; -nosound is NOT used because it crashed the
#                     host in USTConversationSubsystem::StartStage, card 014 run 1)
#   CLIENT_EXTRA_ENV  extra "K=V K=V" pairs for the client only (e.g. UBWCOOP_AUTOINPUT=1)
#   CLIENT2=1         card 209: a second client in the same lane (three games: host, client, client2). It starts
#                     STAGGER s after the first client, so it joins second and holds host roster slot 2. Its logs are
#                     logs/<tag>-client2.{log,stdout}, its window is right of the client's (x 1320, the lane's row),
#                     and it shares the lane's game copy, wine prefix (host and client already share it), null sink,
#                     CPU pinning, nice and FPS_CAP. A SCENARIO gets the roles host,client,client2. Not with SOLO=1 or
#                     MENU=1. Memory: today's pairs peaked at 5.5 to 8.8 GB in the 12G scope (card 209 harness
#                     notes); three games need about 1.5 times that.
#   CLIENT2_EXTRA_ENV extra "K=V K=V" pairs for client2 only
#   HOST_EXTRA_ENV    extra "K=V K=V" pairs for the host only
#   GAME_ARGS         extra game command line words for every instance (default none), e.g. -nullrhi (card 209
#                     headless probe 1)
#   WRAP              command words put before umu-run for every instance (default none), e.g.
#                     "gamescope --headless -w 640 -h 360 --" (card 209 headless probe 2)
#   GOD=0             no card 132 test god mode (default on: UBWCOOP_GOD=1 both sides); set it for down, revive
#                     (cards 043, 044, 109) and death tests
#   SKIP_MOVIES       1 (default) sets UBWCOOP_TEST_SKIP=1: card 135 test skip of every cinematic (startup movies,
#                     briefs, cutscenes, level sequences) by the skip path; 0 leaves them to play as in the play build
#   DONE_PATTERN      regex; the run ends early once the client log matches it and both
#                     logs are quiet for 5 s (default: 'rot\[19\]')
#   SOLO=1            host only, no client; DONE_PATTERN is matched on the host log
#                     (the host's mission-brief "PRESS ANY KEY" pause: set UBWCOOP_AUTODROPSHIP=1 in
#                     HOST_EXTRA_ENV; injected keys do not release it)
#   MENU=1            card 052: neither instance gets UBWCOOP_TRAVEL, UBWCOOP_CLIENT_GAMEONLY or
#                     UBWCOOP_TRAVEL_DELAY_MS; both stay at the main menu, where the co-op panel (driven
#                     by UBWCOOP_C52_TEST in *_EXTRA_ENV) hosts or joins. The client launches only after the
#                     host's "card052: test (host|play): phase 1 starts" row; with none in 300 s (or the host
#                     gone) the run ends unplayed, reason menu-gate, exit status 6
#   STAGGER           seconds between the host and client launches (default 5; it was 25 before
#                     2026-11-04, and card 057's 7 runs connected every time at 5). The host listens about
#                     25 s after launch; the client travels about 19 s after its launch plus its 15 s
#                     UBWCOOP_TRAVEL_DELAY_MS. If a slow machine's client connects too early, use STAGGER=25.
#   SCENARIO=<file>   card 063: both games get UBWCOOP_CMD=1 and tools/drive.py runs the scenario (no xdotool,
#                     no window focus; its output goes to logs/<tag>.drive). The run ends when the runner exits,
#                     after up to 30 s for the games to quit on their own (DONE_PATTERN is not used).
#   HOST_DELAY_MS     UBWCOOP_TRAVEL_DELAY_MS for the host (default 1000; 5000 before 2026-11-05 16:35): ms from the first frontend world to the
#                     listen travel.
#   CLIENT_DELAY_MS   the same for the client (default 8000; 15000 before 2026-11-05 16:35, runs L2d and L2f). The client must travel after the host listens: the
#                     host listens about HOST_DELAY_MS + 3 s after its frontend world, and the client's frontend world
#                     comes about STAGGER s after the host's.
#   LANE              1 (default) or 2. Lane 2 is a second, fully separate pair that can run at the same time as
#                     lane 1: game copy game2/, wine prefix prefix2/ (own wineserver, Saved dir and crash dumps),
#                     listen port 7778, windows on the row below lane 1, its own null sink. game2/ and prefix2/
#                     are btrfs reflink copies of game/ and prefix/. Lane 2 needs its own DEPLOY=1 after each
#                     build (the runner prints the DLL hash it runs). MENU=1 routes (card 052 panel, which types
#                     127.0.0.1:7777) are lane 1 only. Each lane only checks for and kills its own games.
#   FAST=1            card 090: adds UBWCOOP_SKIP_FLIGHT=1 to the host env, so the host's transport dropship skips
#                     its FED_C1M1 spline flight (about 40 s) right after the autodropship unpause. Needs
#                     UBWCOOP_AUTODROPSHIP=1 on the host (else the mod logs a WARN config: line and leaves it off).
#
# Ends the run by killing the prefix's wineserver (the mod log is unbuffered WriteFile,
# so nothing is lost), then copies logs and reports new crash dumps.
set -u -o pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
LANE=${LANE:-1}
case $LANE in
    1) sfx=""; PORT=7777; WINY=40 ;;
    2) sfx=2; PORT=7778; WINY=420 ;;
    *) echo "ABORT: LANE must be 1 or 2" >&2; exit 2 ;;
esac
GAME=$REPO/game$sfx
PREFIX=$REPO/prefix$sfx
BIN=$GAME/StarshipTroopers_UBW/Binaries/Win64
EXE=StarshipTroopers_UBW/Binaries/Win64/StarshipTroopers_UBW-Win64-Shipping.exe
CRASH=$PREFIX/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Crashes
# This lane's game processes only (the Windows-side command line holds the full path through the game dir).
LANE_PAT="game${sfx}[\\/]StarshipTroopers_UBW[\\/]Binaries[\\/]Win64[\\/]StarshipTroopers_UBW-Win64-Shipping\.exe"
PORT_ARGS=()
[[ $LANE == 2 ]] && PORT_ARGS=(-port=$PORT)
TAG=${1:?usage: run_pair.sh <tag> [seconds]}
CAP=${2:-180}
DONE_PATTERN=${DONE_PATTERN:-'rot\[19\]'}
SOLO=${SOLO:-0}
CLIENT2=${CLIENT2:-0}
STAGGER=${STAGGER:-5}
CPUS=${CPUS:-4-7,12-15}
NICE=${NICE:-10}
FPS_CAP=${FPS_CAP:-60}
if [[ -z ${UBW_PINNED:-} && $CPUS != all ]]; then
    UBW_PINNED=1 exec taskset -c "$CPUS" nice -n "$NICE" "$0" "$@"
fi
echo "footprint: cpus $(taskset -cp $$ | sed 's/.*: //'), nice $(nice), fps cap $FPS_CAP"
[[ $STAGGER =~ ^[0-9]{1,3}$ ]] || { echo "ABORT: STAGGER must be whole seconds" >&2; exit 2; }
SCENARIO=${SCENARIO:-}
cmd_env=()
if [[ -n $SCENARIO ]]; then
    [[ -f $SCENARIO ]] || { echo "ABORT: no scenario file $SCENARIO" >&2; exit 2; }
    SCENARIO=$(realpath "$SCENARIO")
    cmd_env=(UBWCOOP_CMD=1)
fi
HOST_DELAY_MS=${HOST_DELAY_MS:-1000}
CLIENT_DELAY_MS=${CLIENT_DELAY_MS:-8000}
[[ $HOST_DELAY_MS =~ ^[0-9]{1,6}$ && $CLIENT_DELAY_MS =~ ^[0-9]{1,6}$ ]] || { echo "ABORT: HOST_DELAY_MS and CLIENT_DELAY_MS must be whole ms" >&2; exit 2; }
host_travel=(UBWCOOP_TRAVEL='/Game/Maps/FED/FED_C1M1?listen' UBWCOOP_TRAVEL_DELAY_MS=$HOST_DELAY_MS)
client_travel=(UBWCOOP_CLIENT_GAMEONLY=1 UBWCOOP_TRAVEL="127.0.0.1:$PORT" UBWCOOP_TRAVEL_DELAY_MS=$CLIENT_DELAY_MS)
if [[ $CLIENT2 == 1 ]] && [[ $SOLO == 1 || ${MENU:-0} == 1 ]]; then
    echo "ABORT: CLIENT2=1 runs with neither SOLO=1 nor MENU=1" >&2; exit 2
fi
if [[ ${MENU:-0} == 1 ]]; then
    [[ $LANE == 1 ]] || { echo "ABORT: MENU=1 runs on lane 1 only" >&2; exit 2; }
    host_travel=()
    client_travel=()
fi
DONE_LOG=client
[[ $SOLO == 1 ]] && DONE_LOG=host
want_sound=${SOUND:-0}
SOUND_ARGS=()
null_mod=""
if [[ $want_sound != 1 ]]; then
    null_mod=$(pactl load-module module-null-sink sink_name=ubwcoop_null$sfx \
        sink_properties=device.description=ubwcoop_null$sfx) || { echo "ABORT: null sink" >&2; exit 5; }
    export PULSE_SINK=ubwcoop_null$sfx
    trap 'pactl unload-module "$null_mod"' EXIT
fi

if pgrep -f "$LANE_PAT" >/dev/null; then
    echo "ABORT: a lane $LANE game instance is already running" >&2
    exit 2
fi

if [[ ${DEPLOY:-0} == 1 ]]; then
    n=$(llvm-readobj --coff-exports "$REPO/build/dwmapi.dll" | grep -c Name:)
    imps=$(llvm-readobj --coff-imports "$REPO/build/dwmapi.dll" | grep -E '^\s*Name:' | awk '{print $2}' | sort -u | tr '\n' ' ')
    if [[ $n != 84 || $imps != "KERNEL32.dll " ]]; then
        echo "ABORT: build/dwmapi.dll has $n exports, imports: $imps" >&2
        exit 3
    fi
    cp "$REPO/build/dwmapi.dll" "$BIN/dwmapi.dll"
    echo "deployed: $(sha256sum "$BIN/dwmapi.dll" | cut -c1-16)"
fi
echo "lane $LANE: dll $(sha256sum "$BIN/dwmapi.dll" | cut -c1-16), build $(sha256sum "$REPO/build/dwmapi.dll" | cut -c1-16), port $PORT"

# One mod log per launch (card 130 item 16): UBWCoop-<tag>-YYYYMMDD-HHMMSS.log. Old ones go first, so the newest
# per tag is this run's.
mod_log() { ls "$BIN"/UBWCoop-"$1"-[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9].log 2>/dev/null | sort | tail -1; }
# Older mod logs (a user's play log among them) move to logs/win64-old/, never deleted.
mkdir -p "$REPO/logs/win64-old"
for f in "$BIN"/UBWCoop-*.log; do [[ -e $f ]] && mv -n "$f" "$REPO/logs/win64-old/"; done
rm -f "$BIN"/UBWCoop-*.cmd "$BIN"/UBWCoop-*.state "$BIN"/UBWCoop-*.state.tmp
crash_before=$(ls "$CRASH" 2>/dev/null | sort)

common=(WINEPREFIX=$PREFIX GAMEID=umu-0 PROTONPATH=GE-Proton WINEDLLOVERRIDES="dwmapi=n,b"
        DXVK_FRAME_RATE="$FPS_CAP" VKD3D_FRAME_RATE="$FPS_CAP"
        UBWCOOP_TEST_SKIP="${SKIP_MOVIES:-1}" UBWCOOP_GOD="${GOD:-1}" "${cmd_env[@]}")
cd "$GAME" || exit 4

host_fast=()
[[ ${FAST:-0} == 1 ]] && host_fast=(UBWCOOP_SKIP_FLIGHT=1)
game_args=()
wrap=()
[[ -n ${GAME_ARGS:-} ]] && read -r -a game_args <<<"$GAME_ARGS"
[[ -n ${WRAP:-} ]] && read -r -a wrap <<<"$WRAP"

# MENU=1 focus (card 319 run 3, 2026-11-14): the card 052 panel driver sends keys with SendInput, which needs its window
# in the foreground, and its own SetForegroundWindow fails under Wine on Wayland. A held driver logs "card052: test:
# game window not in the foreground" and, once input resumes, "card052: test: foreground back"; while a side's last
# such row is the hold, its window is activated from here (xdotool, at most every 3 s per side). The window is the one
# whose process carries this lane's WINEPREFIX and the side's UBWCOOP_LOG_TAG.
side_window() {
    local w p env_text
    for w in $(xdotool search --name "Starship" 2>/dev/null); do
        p=$(xdotool getwindowpid "$w" 2>/dev/null) || continue
        env_text=$(tr '\0' '\n' <"/proc/$p/environ" 2>/dev/null) || continue
        grep -qx "UBWCOOP_LOG_TAG=$1" <<<"$env_text" && grep -q "^WINEPREFIX=$PREFIX/" <<<"$env_text" && { echo "$w"; return 0; }
    done
    return 1
}
declare -A focus_at=()
menu_focus() {
    local side last w
    for side in host client; do
        last=$(grep -oE "card052: test: (game window not in the foreground|foreground back)" "$(mod_log "$side")" 2>/dev/null | tail -1)
        [[ $last == *"not in the foreground"* ]] || continue
        ((SECONDS - ${focus_at[$side]:-0} >= 3)) || continue
        focus_at[$side]=$SECONDS
        if w=$(side_window "$side"); then
            # --sync waits for the window manager; timeout keeps a refused activation from hanging the run.
            timeout 5 xdotool windowactivate --sync "$w" >/dev/null 2>&1 &&
                echo "menu focus: t=${SECONDS}s $side window $w activated (card052 input held)" ||
                echo "WARN menu focus: t=${SECONDS}s xdotool windowactivate $w failed or timed out for $side"
        else
            echo "WARN menu focus: t=${SECONDS}s no $side window found"
        fi
    done
}
if [[ ${MENU:-0} == 1 ]] && ! command -v xdotool >/dev/null; then
    echo "ABORT: MENU=1 needs xdotool (card052 driver focus)" >&2; exit 2
fi

# shellcheck disable=SC2086
env "${common[@]}" UBWCOOP_LOG_TAG=host "${host_travel[@]}" "${host_fast[@]}" ${HOST_EXTRA_ENV:-} \
    "${wrap[@]}" umu-run "$EXE" -windowed -ResX=640 -ResY=360 -WinX=0 -WinY=$WINY "${PORT_ARGS[@]}" "${SOUND_ARGS[@]}" "${game_args[@]}" \
    >"$REPO/logs/$TAG-host.stdout" 2>&1 &
host_pid=$!
client_pid=""
menu_gate_fail="" # MENU=1: set when the client gate ends without the host's first mission; the run ends unplayed
if [[ $SOLO != 1 ]]; then
    sleep "$STAGGER"
    # MENU=1: the client panel types its join about 8.5 s after its menu world, with no retry, so it launches only once
    # the host's driver reached its first mission world (the host listens there). Run 3 of card 319: the host was held
    # for the foreground, the client joined first and timed out (ConnectionTimeout 20 s after the join arm).
    if [[ ${MENU:-0} == 1 ]]; then
        for ((g = 0; g < 300; g++)); do
            grep -qE "card052: test (host|play): phase 1 starts" "$(mod_log host)" 2>/dev/null && break
            kill -0 "$host_pid" 2>/dev/null || break
            menu_focus
            sleep 1
        done
        if grep -qE "card052: test (host|play): phase 1 starts" "$(mod_log host)" 2>/dev/null; then
            echo "menu: host in its first mission after $g s of the client gate; client launches"
        else
            # A client that joins a host still in the menu times out, so the run would be void by construction: it
            # ends here through the normal cleanup below, with no client launch and no scenario start.
            menu_gate_fail="menu-gate (host not in its first mission after $g s)"
            echo "WARN menu: host not in its first mission after $g s of the client gate; the run ends, no client launch"
        fi
    fi
    if [[ -z $menu_gate_fail ]]; then
        # shellcheck disable=SC2086
        env "${common[@]}" PROTON_VERB=run UBWCOOP_LOG_TAG=client "${client_travel[@]}" ${CLIENT_EXTRA_ENV:-} \
            "${wrap[@]}" umu-run "$EXE" -windowed -ResX=640 -ResY=360 -WinX=660 -WinY=$WINY "${SOUND_ARGS[@]}" "${game_args[@]}" \
            >"$REPO/logs/$TAG-client.stdout" 2>&1 &
        client_pid=$!
    fi
fi
client2_pid=""
if [[ $CLIENT2 == 1 && -z $menu_gate_fail ]]; then
    sleep "$STAGGER"
    # shellcheck disable=SC2086
    env "${common[@]}" PROTON_VERB=run UBWCOOP_LOG_TAG=client2 "${client_travel[@]}" ${CLIENT2_EXTRA_ENV:-} \
        "${wrap[@]}" umu-run "$EXE" -windowed -ResX=640 -ResY=360 -WinX=1320 -WinY=$WINY "${SOUND_ARGS[@]}" "${game_args[@]}" \
        >"$REPO/logs/$TAG-client2.stdout" 2>&1 &
    client2_pid=$!
fi

drive_pid=""
if [[ -n $SCENARIO && -z $menu_gate_fail ]]; then
    roles=host,client
    [[ $SOLO == 1 ]] && roles=host
    [[ $CLIENT2 == 1 ]] && roles=host,client,client2
    python3 -I "$REPO/tools/drive.py" "$SCENARIO" --bin "$BIN" --roles "$roles" --shot-prefix "$TAG" >"$REPO/logs/$TAG.drive" 2>&1 &
    drive_pid=$!
fi

# Liveness: every game's mod log must exist within 90 s of launch.
start=$SECONDS
reason=cap
drive_rc=none
loop_cap=$CAP
[[ -n $menu_gate_fail ]] && { reason=$menu_gate_fail; loop_cap=0; }
for ((i = 0; i < loop_cap; i++)); do
    if ! kill -0 "$host_pid" 2>/dev/null && { [[ -z $client_pid ]] || ! kill -0 "$client_pid" 2>/dev/null; } &&
        { [[ -z $client2_pid ]] || ! kill -0 "$client2_pid" 2>/dev/null; }; then
        reason=exited; break
    fi
    if ((SECONDS - start > 90)) && { [[ ! -s $(mod_log host) ]] ||
        { [[ $SOLO != 1 ]] && [[ ! -s $(mod_log client) ]]; } ||
        { [[ $CLIENT2 == 1 ]] && [[ ! -s $(mod_log client2) ]]; }; }; then
        reason=no-log; break
    fi
    # A game that crashed sits in UE's ReportCrash (120 s under Wine) and then the "Fatal error" dialog until killed,
    # while the scenario's waits time out one by one: end the run now (card 319 run 1 host crash, 2026-11-14).
    crashed=
    for side in host client client2; do
        [[ $side == client && $SOLO == 1 ]] && continue
        [[ $side == client2 && $CLIENT2 != 1 ]] && continue
        grep -qF "CRASH ue: ReportCrash" "$(mod_log "$side")" 2>/dev/null && crashed+="$side "
    done
    if [[ -n $crashed ]]; then
        echo "game crashed (${crashed% }): ending the run now"
        reason="crash (${crashed% })"; break
    fi
    [[ ${MENU:-0} == 1 ]] && menu_focus
    if [[ -n $drive_pid ]]; then
        if ! kill -0 "$drive_pid" 2>/dev/null; then
            wait "$drive_pid"; drive_rc=$?; reason=scenario
            for ((j = 0; j < 30; j++)); do
                kill -0 "$host_pid" 2>/dev/null || { [[ -n $client_pid ]] && kill -0 "$client_pid" 2>/dev/null; } ||
                    { [[ -n $client2_pid ]] && kill -0 "$client2_pid" 2>/dev/null; } || break
                sleep 1
            done
            break
        fi
    elif grep -qE "$DONE_PATTERN" "$(mod_log "$DONE_LOG")" 2>/dev/null; then
        sleep 5; reason=done; break
    fi
    sleep 1
done

# The newest GE-Proton, the one umu-run picks with PROTONPATH=GE-Proton.
WINESERVER=$(ls -d "$HOME"/.local/share/Steam/compatibilitytools.d/GE-Proton*-x86_64 2>/dev/null | sort -V | tail -1)/files/bin/wineserver
[[ -x $WINESERVER ]] || echo "WARN: no GE-Proton wineserver found; falling back to SIGKILL"
if [[ -n $drive_pid ]] && kill -0 "$drive_pid" 2>/dev/null; then
    kill "$drive_pid"; wait "$drive_pid" 2>/dev/null; drive_rc=killed
fi
WINEPREFIX=$PREFIX "$WINESERVER" -k >/dev/null 2>&1
for ((i = 0; i < 15; i++)); do
    pgrep -f "$LANE_PAT" >/dev/null || break
    sleep 1
done
if pgrep -f "$LANE_PAT" >/dev/null; then
    echo "WARN: wineserver -k left lane $LANE game processes; sending SIGKILL"
    pkill -KILL -f "$LANE_PAT"
fi
wait "$host_pid" 2>/dev/null; host_rc=$?
client_rc=none
[[ -n $client_pid ]] && { wait "$client_pid" 2>/dev/null; client_rc=$?; }
client2_rc=none
[[ -n $client2_pid ]] && { wait "$client2_pid" 2>/dev/null; client2_rc=$?; }
sides=(host client)
[[ $CLIENT2 == 1 ]] && sides=(host client client2)

for side in "${sides[@]}"; do
    f=$(mod_log "$side")
    [[ -n $f ]] && cp "$f" "$REPO/logs/$TAG-$side.log"
done
crash_new=$(comm -13 <(echo "$crash_before") <(ls "$CRASH" 2>/dev/null | sort))

rc2=""
[[ $CLIENT2 == 1 ]] && rc2=", client2 rc $client2_rc"
echo "run $TAG: ended by $reason after $((SECONDS - start)) s; host rc $host_rc, client rc $client_rc$rc2"
for side in "${sides[@]}"; do
    f=$REPO/logs/$TAG-$side.log
    [[ -f $f ]] || { echo "$side: NO LOG"; continue; }
    echo "$side: $(wc -l <"$f") lines, FATAL/ERROR $(grep -cE 'FATAL|ERROR' "$f"), WARN $(grep -c WARN "$f")," \
        "map loaded $(grep -c 'map loaded' "$f"), detach $(grep -c detach "$f")"
done
echo "new crash dumps: ${crash_new:-none}"
[[ -n $SCENARIO ]] && echo "scenario $(basename "$SCENARIO"): drive rc $drive_rc ($REPO/logs/$TAG.drive)"
[[ -z $menu_gate_fail ]] || exit 6
