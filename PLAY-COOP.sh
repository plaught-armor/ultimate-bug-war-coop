#!/usr/bin/env bash
# Double-click this to start the game with the co-op mod. Nothing else to do.
# Finds the game, in this order:
#   1. ./game (unpacked here before)
#   2. the folder above this one (this folder was dropped into the game folder)
#   3. an installed GOG game (Heroic, Lutris, ~/Games)
#   4. the GOG installer files (setup_...exe plus its -1.bin and -2.bin) in this folder,
#      the folder above or ~/Downloads: unpacked once into ./game with innoextract.
# The mod works only on GOG 1.2.0 (build 90228).
set -u
cd "$(dirname "$(readlink -f "$0")")" || exit 1
EXE=StarshipTroopers_UBW/Binaries/Win64/StarshipTroopers_UBW-Win64-Shipping.exe
WANT=ada7f1ad04cec4cf   # sha256 prefix of the GOG 1.2.0 (90228) game exe
SETUP='setup_starship_troopers_ultimate_bug_war_1.2.0_(64bit)_(90228).exe'

say() {   # show a message in a window if possible, else on the terminal
    command -v kdialog >/dev/null && { kdialog --msgbox "$1"; return; }
    command -v zenity >/dev/null && { zenity --info --text="$1"; return; }
    echo "$1"
}

note() {  # a short notice that does not wait for a click
    command -v notify-send >/dev/null && notify-send "UBW Co-op" "$1"
    echo "$1"
}

find_installed() {   # prints the game folder of an installed game, or nothing
    [[ -f game/$EXE ]] && { echo "$PWD/game"; return; }
    [[ -f ../$EXE ]] && { (cd .. && pwd); return; }
    local hit
    hit=$(find "$HOME/Games" "$HOME/.local/share/Steam" "$HOME/.config/heroic" \
               "$HOME/.local/share/lutris" -maxdepth 6 -path "*/$EXE" -print -quit 2>/dev/null)
    [[ -n $hit ]] && echo "${hit%/"$EXE"}"
}

unpack_installer() {   # unpacks the GOG installer into ./game; returns 1 if none found
    local setup
    setup=$(find . .. "$HOME/Downloads" -maxdepth 3 -name "$SETUP" -print -quit 2>/dev/null)
    [[ -n $setup ]] || return 1
    command -v innoextract >/dev/null || { say "Found the GOG installer. To unpack it, install innoextract once: sudo pacman -S innoextract"; exit 3; }
    note "Unpacking the game from the GOG installer. This takes a few minutes, once."
    rm -rf game.part
    innoextract -s -d game.part "$setup" || { say "Unpacking the GOG installer failed. Are its -1.bin and -2.bin files next to it?"; rm -rf game.part; exit 3; }
    mv -T game.part game || { say "Could not move the unpacked game into place: remove the old game folder and try again."; exit 3; }
}

REPO_API=https://api.github.com/repos/plaught-armor/ultimate-bug-war-coop/releases/latest

# Asks to update when GitHub has a newer release than build/RELEASE.txt. Any failure (no network, no curl,
# odd reply) skips the check and starts the installed version. UBW_NO_UPDATE=1 skips it too.
check_update() {
    [[ -z ${UBW_NO_UPDATE:-} && -f build/RELEASE.txt ]] || return 0
    command -v curl >/dev/null && command -v unzip >/dev/null || return 0
    local mine latest json url tmp
    mine=$(tr -d '[:space:]' < build/RELEASE.txt)
    json=$(curl -fsSL --max-time 5 "$REPO_API" 2>/dev/null) || return 0
    latest=$(grep -o '"tag_name": *"b[0-9]*"' <<<"$json" | grep -o 'b[0-9]*' | head -1)
    url=$(grep -o '"browser_download_url": *"[^"]*\.zip"' <<<"$json" | grep -o 'https://[^"]*' | head -1)
    [[ $mine =~ ^b[0-9]+$ && $latest =~ ^b[0-9]+$ && -n $url ]] || return 0
    if (( ${latest#b} > ${mine#b} )); then
        ask "A new UBW Co-op version is out: $latest (you have $mine). Both players need the same version. Update now?" \
            || return 0
    else
        # Launchers up to b366 move a fixed file list without ubwperf.dll, then start this script.
        # The player already agreed to that update, so fetch the rest without asking again.
        [[ $latest == "$mine" && ! -f build/ubwperf.dll ]] || return 0
    fi
    note "Downloading $latest."
    tmp=$(mktemp -d "$PWD/.update.XXXXXX") || return 0
    if curl -fsSL --max-time 120 -o "$tmp/u.zip" "$url" && unzip -q "$tmp/u.zip" -d "$tmp" \
        && [[ -f $tmp/UBW-Coop/build/dwmapi.dll && -f $tmp/UBW-Coop/build/ubwperf.dll && -f $tmp/UBW-Coop/PLAY-COOP.sh ]]; then
        # mv, not cp: this running script keeps reading its old file, and the new one starts fresh below.
        # Every file of the zip, so a file added by a later release arrives too. RELEASE.txt is written last:
        # a failed move leaves the old tag and the next start tries again.
        local f
        while IFS= read -r -d '' f; do
            [[ $f == build/RELEASE.txt ]] && continue
            mkdir -p "$(dirname "$f")" && mv -f "$tmp/UBW-Coop/$f" "$f" && continue
            rm -rf "$tmp"
            say "The update to $latest could not replace $f. Unzip the new release by hand."
            return 0
        done < <(find "$tmp/UBW-Coop" -type f -printf '%P\0')
        echo "$latest" > build/RELEASE.txt   # also when the zip has none, so it does not ask again
        rm -rf "$tmp"
        note "Updated to $latest."
        UBW_NO_UPDATE=1 exec ./PLAY-COOP.sh "$@"
    fi
    rm -rf "$tmp"
    say "The update download failed. Starting the installed version $mine."
}

ask() {   # yes/no question in a window if possible, else on the terminal; no answer means no
    command -v kdialog >/dev/null && { kdialog --yesno "$1"; return; }
    command -v zenity >/dev/null && { zenity --question --text="$1"; return; }
    [[ -t 0 ]] || return 1
    local r; read -r -p "$1 [y/N] " r; [[ $r == [yY]* ]]
}

check_update "$@"
command -v umu-run >/dev/null || { say "umu-run is missing. Install it once: sudo pacman -S umu-launcher"; exit 3; }
if [[ -z ${GAME_DIR:-} ]]; then
    GAME_DIR=$(find_installed)
    if [[ -z $GAME_DIR ]]; then
        unpack_installer || { say "Could not find the game. Put the GOG installer files next to PLAY-COOP.sh, or install the game from GOG (Heroic works)."; exit 3; }
        GAME_DIR=$PWD/game
    fi
fi
have=$(sha256sum "$GAME_DIR/$EXE" | cut -c1-16)
[[ $have == "$WANT" ]] || { say "Wrong game version in $GAME_DIR. The mod needs GOG version 1.2.0 (build 90228)."; exit 3; }
export GAME_DIR
export PORT=${PORT:-47000}   # host listen port; the router forwards UDP 47000 to the host
exec tools/playtest.sh menu
