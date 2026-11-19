#!/usr/bin/env bash
# Build the drag-and-drop mod package: dist/UBW-Coop-<date>-<time>.zip
#
# The zip holds one folder, UBW-Coop/, with:
#   PLAY-COOP.sh, HOW-TO-PLAY.txt, tools/playtest.sh, build/dwmapi.dll
# No game files. The player unzips it anywhere (or into the game folder) and
# double-clicks PLAY-COOP.sh.
set -eu -o pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
DLL=$REPO/build/dwmapi.dll
[[ -f $DLL ]] || { echo "ABORT: no $DLL" >&2; exit 1; }
hash=$(sha256sum "$DLL" | cut -c1-16)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

pkg=$stage/UBW-Coop
mkdir -p "$pkg/tools" "$pkg/build"
install -m 755 "$REPO/PLAY-COOP.sh" "$pkg/PLAY-COOP.sh"
install -m 644 "$REPO/HOW-TO-PLAY.txt" "$pkg/HOW-TO-PLAY.txt"
install -m 755 "$REPO/tools/playtest.sh" "$pkg/tools/playtest.sh"
install -m 644 "$DLL" "$pkg/build/dwmapi.dll"
echo "$hash" > "$pkg/build/VERSION.txt"

mkdir -p "$REPO/dist"
out=$REPO/dist/UBW-Coop-$(date +%Y%m%d-%H%M).zip
rm -f "$out"
(cd "$stage" && zip -qr -X "$out" UBW-Coop)
echo "package: $out"
unzip -Z1 "$out"
