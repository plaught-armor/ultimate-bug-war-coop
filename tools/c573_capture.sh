#!/bin/bash
# Card 573 window capture (lane 2). Usage: tools/c573_capture.sh <out dir> [release file], started just before
# run_pair.sh. The release file (e.g. logs/<tag>.release) is what the scenario's `host wait file` step waits for.
# Trigger: the SECOND host `card573: cmd console 't.MaxFPS 60' sent` row in a host log newer than this script's start
# (row 1 is the early cap send; row 2 is the scenario's quiet hold or combat point).
# Then: 2 s settle; then attempts at a clean 20 s window, started until 77 s after the trigger. An attempt starts
#   - /proc task stat snaps for both lane-2 game processes and perf record -F 499 of each whole process (all threads,
#     <out>/{host,client}-gt.data, scripted to .txt),
#   and reads card1 other-job gfx (drm-engine-gfx ns deltas from every process except the two lane-2 games, deduped
#   by drm-client-id) about every 1-2 s, one line per read in <out>/window.gpu1s: "<uptime ms> <busy %> <other %>"
#   (uptime ms = game log ms). A read at 30 % or more aborts the attempt (perf stopped, data discarded, logged in
#   other-gpu.txt); the next attempt starts at once. The first attempt with 20 s of reads all under 30 % is the
#   window: end snaps, then the release file is written (gate met). No clean window by the deadline: no release file
#   (gate unmet), so the scenario's wait times out and the run is void.
# BRSTACK=1: record with -j any,u (AMD LBRv2, 16 newest user branches; plain -b records empty stacks at
#   perf_event_paranoid 2) and script the brstack field too, for
#   tools/perf_frame.py --callers (card 573 caller attribution; the shipping exe has no frame pointers).
set -u
br=(); brf=""
[ "${BRSTACK:-0}" = 1 ] && { br=(-j any,u); brf=",brstack"; }
cd /path/to/ultimate-bug-war-coop
out=${1:?out dir}
release=${2:-}
mkdir -p "$out"
since=$(date '+%F %T')
row="card573: .*console 't\.MaxFPS 60' sent"
hlog=""
for i in $(seq 1 600); do
  hlog=$(find game2/StarshipTroopers_UBW/Binaries/Win64 -maxdepth 1 -name 'UBWCoop-host-*.log' -newermt "$since" 2>/dev/null | sort | tail -1)
  [ -n "$hlog" ] && [ "$(grep -cE "$row" "$hlog")" -ge 2 ] && break
  sleep 1
done
[ -n "$hlog" ] && [ "$(grep -cE "$row" "$hlog")" -ge 2 ] || { echo "no second console row"; exit 1; }
echo "trigger $(date +%T.%N) $(grep -E "$row" "$hlog" | tail -1)" > "$out/trigger.txt"
host=""; client=""
for p in $(pgrep -x GameThread); do
  c=$(tr '\0' ' ' < /proc/$p/cmdline); case $c in *game2*) ;; *) continue ;; esac
  if [[ $c == *-port=* ]]; then host=$p; else client=$p; fi
done
[ -n "$host" ] && [ -n "$client" ] || { echo "pids missing: host '$host' client '$client'"; exit 1; }
gt_tid() { for t in /proc/$1/task/*; do [ "$(cat "$t/comm" 2>/dev/null)" = GameThread ] && { basename "$t"; return; }; done; }
hgt=$(gt_tid "$host"); cgt=$(gt_tid "$client")
[ -n "$hgt" ] && [ -n "$cgt" ] || { echo "GameThread tid missing: host '$hgt' client '$cgt'"; exit 1; }
echo "host $host gt $hgt client $client gt $cgt log $hlog" > "$out/pids.txt"
# Sum of drm-engine-gfx ns on card1 (pdev 0000:03:00.0) per client id, all processes except the two games.
other_gfx() {
  for d in /proc/[0-9]*; do
    p=${d#/proc/}; [ "$p" = "$host" ] || [ "$p" = "$client" ] && continue
    cat "$d"/fdinfo/* 2>/dev/null | awk '/^drm-pdev:/ {pd=$2} /^drm-client-id:/ {id=$2} /^drm-engine-gfx:/ {if (pd=="0000:03:00.0") print id, $2}'
  done | sort -u -k1,1 | awk '{s+=$2} END {printf "%d", s}'
}
snap() { for t in /proc/$2/task/*; do echo "$(basename "$t") $(cat "$t/stat" 2>/dev/null)"; done > "$out/$1"; }
sleep 2
t_end=$(( $(date +%s) + 75 ))  # last attempt starts by trigger + 77 s, ends by about + 100 s
: > "$out/other-gpu.txt"; gate=unmet
for attempt in $(seq 1 20); do
  [ "$(date +%s)" -lt "$t_end" ] || break
  : > "$out/window.gpu1s"
  snap host-quiet-stat-start.txt "$host"; snap client-quiet-stat-start.txt "$client"; date +%T.%N > "$out/quiet-start.time"
  perf record -q "${br[@]}" -F 499 -p "$host" -o "$out/host-gt.data" 2> "$out/host-gt.err" & hp=$!
  perf record -q "${br[@]}" -F 499 -p "$client" -o "$out/client-gt.data" 2> "$out/client-gt.err" & cp=$!
  w0=$(date +%s%N); a=$(other_gfx); s0=$(date +%s%N); dirty=0
  for i in $(seq 1 40); do
    sleep 0.5
    b=$(other_gfx); s1=$(date +%s%N)
    pct=$(( (b - a) * 100 / (s1 - s0) )); a=$b; s0=$s1
    echo "$(awk '{printf "%d", $1*1000}' /proc/uptime) $(cat /sys/class/drm/card1/device/gpu_busy_percent) $pct" >> "$out/window.gpu1s"
    [ "$pct" -ge 30 ] && { dirty=1; break; }
    [ $(( (s1 - w0) / 1000000000 )) -ge 20 ] && break
  done
  kill -TERM "$hp" "$cp" 2>/dev/null; wait "$hp"; hrc=$?; wait "$cp"; crc=$?
  if [ $dirty = 0 ] && [ $(( (s0 - w0) / 1000000000 )) -ge 20 ]; then
    snap host-quiet-stat-end.txt "$host"; snap client-quiet-stat-end.txt "$client"; date +%T.%N > "$out/quiet-end.time"
    gate=met; break
  fi
  echo "$(date +%T) attempt $attempt aborted: other-job gfx $pct % after $(( (s0 - w0) / 1000000000 )) s" >> "$out/other-gpu.txt"
done
echo "gate $gate after $attempt attempt(s)" >> "$out/other-gpu.txt"
[ $gate = met ] && [ -n "$release" ] && date +%T.%N > "$release"
[ $gate = met ] || { echo "quiet done $(date +%T) gate unmet after $attempt attempt(s)"; exit 1; }
perf script -i "$out/host-gt.data" -F "comm,tid,ip,dso$brf" > "$out/host-gt.txt" 2>> "$out/host-gt.err"
perf script -i "$out/client-gt.data" -F "comm,tid,ip,dso$brf" > "$out/client-gt.txt" 2>> "$out/client-gt.err"
echo "quiet done $(date +%T) gate $gate attempt $attempt perf rc $hrc/$crc samples $(wc -l < "$out/host-gt.txt")/$(wc -l < "$out/client-gt.txt") gpu1s $(wc -l < "$out/window.gpu1s") mean $(awk '{s+=$2} END {printf "%.0f", s/NR}' "$out/window.gpu1s")"
