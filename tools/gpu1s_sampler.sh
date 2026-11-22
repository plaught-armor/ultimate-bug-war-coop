#!/bin/bash
# Card 573 workstream C: 1 s card1 GPU sampler with a continuous other-job gate (lane 2).
# Usage: tools/gpu1s_sampler.sh <out file> [max seconds, default 1800], started just before run_pair.sh.
# Each line: "<uptime ms> <gpu_busy_percent> <other-job gfx %> <top other comm>:<its %>"
#   uptime ms = game log ms. other-job gfx % = drm-engine-gfx ns delta since the previous line, summed over every
#   process on card1 (pdev 0000:03:00.0) except the lane-2 game processes (comm GameThread, cmdline with game2),
#   deduped by drm-client-id, divided by the wall time between the two reads. The first line's % is 0 (no delta).
# A window is void for GPU sizing when any of its lines has other-job gfx over 5 %.
# Stops after max seconds, or 60 s after the lane-2 games were seen and are gone.
set -u
out=${1:?out file}
max_s=${2:-1800}
pdev=0000:03:00.0
busy_file=/sys/class/drm/card1/device/gpu_busy_percent
declare -A prev
prev_ns=0
: > "$out"
seen=0; gone_s=0
game_pids() {
  for p in $(pgrep -x GameThread); do
    case $(tr '\0' ' ' < /proc/$p/cmdline 2>/dev/null) in *game2*) echo "$p" ;; esac
  done
}
for i in $(seq 1 "$max_s"); do
  games=" $(game_pids | tr '\n' ' ') "
  if [ "$games" != "  " ]; then seen=1; gone_s=0; elif [ $seen = 1 ]; then gone_s=$((gone_s + 1)); fi
  [ $gone_s -ge 60 ] && break
  t_ns=$(date +%s%N)
  declare -A cur=()
  declare -A comm_of=()
  while read -r key comm ns; do
    cur[$key]=$ns; comm_of[$key]=$comm
  done < <(
    for d in /proc/[0-9]*; do
      p=${d#/proc/}
      case $games in *" $p "*) continue ;; esac
      c=$(cat "$d/comm" 2>/dev/null) || continue
      cat "$d"/fdinfo/* 2>/dev/null | awk -v pd=$pdev -v c="$c" \
        '/^drm-pdev:/ {d=$2} /^drm-client-id:/ {id=$2} /^drm-engine-gfx:/ {if (d==pd) print id, c, $2}'
    done | sort -u -k1,1
  )
  total=0; top=none; top_ns=0
  for k in "${!cur[@]}"; do
    d=$(( ${cur[$k]} - ${prev[$k]:-${cur[$k]}} ))
    [ $d -lt 0 ] && d=0
    total=$((total + d))
    [ $d -gt $top_ns ] && { top_ns=$d; top=${comm_of[$k]}; }
  done
  wall=$(( t_ns - prev_ns ))
  if [ $prev_ns -eq 0 ]; then pct=0; tpct=0; else pct=$(( total * 100 / wall )); tpct=$(( top_ns * 100 / wall )); fi
  prev=(); for k in "${!cur[@]}"; do prev[$k]=${cur[$k]}; done
  prev_ns=$t_ns
  echo "$(awk '{printf "%d", $1*1000}' /proc/uptime) $(cat $busy_file) $pct $top:$tpct" >> "$out"
  unset cur comm_of
  sleep 0.1  # the /proc fdinfo scan takes about 0.9 s, so a line lands about every 1 s
done
echo "sampler done $(date +%T) lines $(wc -l < "$out")" >&2
