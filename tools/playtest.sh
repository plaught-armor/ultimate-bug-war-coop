#!/usr/bin/env bash
# Launch one instance of the UBW co-op mod for a two-machine LAN playtest (Linux + Proton).
#
# Usage:
#   tools/playtest.sh menu            # normal game; host or join from the co-op panel at the main menu
#   tools/playtest.sh host            # this machine hosts FED_C1M1 on UDP 7777
#   tools/playtest.sh client <ip>     # join the host at <ip> (port 7777)
#
# Env (optional):
#   GAME_DIR     extracted game folder holding StarshipTroopers_UBW/ (default: $REPO/game)
#   PREFIX       Proton wine prefix (default: $REPO/prefix)
#   DLL          mod DLL to deploy (default: $REPO/build/dwmapi.dll)
#   MAP          host map (default: /Game/Maps/FED/FED_C1M1)
#   WINDOWED=1   run in a window instead of full screen (1280x720 unless RES is set)
#   RES=WxH      window size with WINDOWED=1, e.g. RES=640x360
#   POS=X,Y      window position with WINDOWED=1, e.g. POS=660,40
#   LOG_TAG      mod log name (default: the role); two games on one machine need different tags
#   PORT=N       host listen port (default 7777), e.g. PORT=47000
#   SECOND=1     second game on this machine, sharing the prefix with the first: skips the
#                "already running" check and launches with PROTON_VERB=run (as tools/run_pair.sh does)
#
# No test switches are set: no weapon grant, no auto input, no kill switches.
# menu: both players use this role. The host presses H on the panel and starts a mission from the menu;
# the client presses J, types the host's address and presses Enter (see docs/PLAYTEST.md).
# host/client: start the host first and wait until it is in the mission; then start the client.
# The mod log is written next to the exe as UBWCoop-<role>-YYYYMMDD-HHMMSS.log, one per launch (card 130
# item 16); it is copied to $REPO/logs/playtest-<role>-YYYYMMDD-HHMMSS.log when the game exits.
set -u -o pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
ROLE=${1:?usage: playtest.sh menu | host | client <ip>}
GAME_DIR=${GAME_DIR:-$REPO/game}
PREFIX=${PREFIX:-$REPO/prefix}
DLL=${DLL:-$REPO/build/dwmapi.dll}
MAP=${MAP:-/Game/Maps/FED/FED_C1M1}
BIN=$GAME_DIR/StarshipTroopers_UBW/Binaries/Win64
EXE=StarshipTroopers_UBW/Binaries/Win64/StarshipTroopers_UBW-Win64-Shipping.exe

travel=""
case $ROLE in
    menu) extra=() ;;
    host) travel="$MAP?listen"; extra=() ;;
    client)
        ip=${2:?usage: playtest.sh client <host ip>}
        [[ $ip =~ ^[0-9]{1,3}(\.[0-9]{1,3}){3}(:[0-9]{1,5})?$ ]] || { echo "ABORT: not an IPv4 address[:port]: $ip" >&2; exit 2; }
        [[ $ip == *:* ]] || ip="$ip:7777"
        travel="$ip"; extra=(PROTON_VERB=run UBWCOOP_CLIENT_GAMEONLY=1 UBWCOOP_TRAVEL_DELAY_MS=15000) ;;
    *) echo "ABORT: role must be menu, host or client" >&2; exit 2 ;;
esac

[[ -f $GAME_DIR/$EXE ]] || { echo "ABORT: game exe not found under $GAME_DIR" >&2; exit 3; }
LOG_TAG=${LOG_TAG:-$ROLE}
[[ $LOG_TAG =~ ^[A-Za-z0-9_-]{1,32}$ ]] || { echo "ABORT: LOG_TAG must be 1-32 of [A-Za-z0-9_-]" >&2; exit 2; }
second=()
if [[ ${SECOND:-0} == 1 ]]; then
    pgrep -f 'Win64-Shipping\.exe' >/dev/null || { echo "ABORT: SECOND=1 but no game is running" >&2; exit 4; }
    second=(PROTON_VERB=run)
elif pgrep -f 'Win64-Shipping\.exe' >/dev/null; then
    echo "ABORT: a game instance is already running (SECOND=1 starts a second one on this prefix)" >&2
    exit 4
fi
if [[ ${SECOND:-0} == 1 ]]; then
    echo "second game: using the deployed $(sha256sum "$BIN/dwmapi.dll" | cut -c1-16)"
elif [[ -f $DLL ]]; then
    if command -v llvm-readobj >/dev/null; then
        n=$(llvm-readobj --coff-exports "$DLL" | grep -c Name:)
        [[ $n == 84 ]] || { echo "ABORT: $DLL has $n exports (want 84)" >&2; exit 5; }
    else
        echo "note: llvm-readobj missing; export count not checked"
    fi
    cp "$DLL" "$BIN/dwmapi.dll"
    echo "deployed $(sha256sum "$BIN/dwmapi.dll" | cut -c1-16)"
elif [[ ! -f $BIN/dwmapi.dll ]]; then
    echo "ABORT: no mod DLL at $DLL and none deployed in $BIN" >&2
    exit 5
fi

travel_env=()
[[ -n $travel ]] && travel_env=(UBWCOOP_TRAVEL="$travel")
win=()
if [[ ${WINDOWED:-0} == 1 ]]; then
    res=${RES:-1280x720}
    [[ $res =~ ^([0-9]{3,4})x([0-9]{3,4})$ ]] || { echo "ABORT: RES must be WxH, e.g. 640x360" >&2; exit 2; }
    win=(-windowed "-ResX=${BASH_REMATCH[1]}" "-ResY=${BASH_REMATCH[2]}")
    if [[ -n ${POS:-} ]]; then
        [[ $POS =~ ^([0-9]{1,5}),([0-9]{1,5})$ ]] || { echo "ABORT: POS must be X,Y, e.g. 660,40" >&2; exit 2; }
        win+=("-WinX=${BASH_REMATCH[1]}" "-WinY=${BASH_REMATCH[2]}")
    fi
fi

if [[ -n ${PORT:-} ]]; then
    [[ $PORT =~ ^[0-9]{1,5}$ ]] && (( PORT >= 1024 && PORT <= 65535 )) || { echo "ABORT: PORT must be 1024-65535" >&2; exit 2; }
    win+=("-port=$PORT")
fi

launch_marker=$(date +%s)
cd "$GAME_DIR" || exit 3
env WINEPREFIX="$PREFIX" GAMEID=umu-0 PROTONPATH=GE-Proton WINEDLLOVERRIDES="dwmapi=n,b" \
    UBWCOOP_LOG_TAG="$LOG_TAG" "${travel_env[@]}" "${extra[@]}" "${second[@]}" \
    umu-run "$EXE" "${win[@]}"
rc=$?

# umu-run may return while the game still runs; wait for the game to exit.
while pgrep -f 'Win64-Shipping\.exe' >/dev/null; do sleep 2; done
mkdir -p "$REPO/logs"
# This launch's log: the newest for the tag, written since the launch (older launches keep theirs).
log=$(find "$BIN" -maxdepth 1 -name "UBWCoop-$LOG_TAG-[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9].log" -newermt "@$launch_marker" 2>/dev/null | sort | tail -1)
if [[ -n $log ]]; then
    out=$REPO/logs/playtest-${log##*/UBWCoop-}
    cp "$log" "$out" && echo "log: $out"
else
    echo "WARN: no UBWCoop-$LOG_TAG-*.log written since the launch"
fi
echo "umu-run rc $rc"
