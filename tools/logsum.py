"""Summarize the mod logs of one test run: counts, log gaps and a merged timeline.

Usage: python3 -I tools/logsum.py TAG [--grep REGEX] [--gap SECONDS] [--max N] [--all]

Reads logs/TAG-host.log, logs/TAG-client.log and logs/TAG-client2.log (any may be missing, e.g. a solo run;
client2 only with run_pair.sh CLIENT2=1).
Each mod log line looks like "[ 577628422 ms][T  412] text"; both instances on one
machine share the millisecond clock, so the logs merge into one timeline.

Prints, in order:
  1. Per side: line count, time span, ERROR/FATAL and WARN counts.
  2. Per side: the 12 most frequent line prefixes ("card049:", "hooks:", ...).
  3. Per side: the 8 longest gaps between consecutive lines over --gap (default 1.0 s),
     with the line before and after each. The mod log is an event log, so a gap is a
     hint only (a map load is a normal gap), not a frame hitch.
  4. The merged timeline, times in seconds from the first line of either log, limited
     to key lines: travel, map loads, listen, connect and failures, errors and warnings,
     the newest cards' lines (not the 'hooks: ... hooked at' install lines), and anything
     matching --grep. --all prints every line.
     At most --max lines (default 300).
"""
import argparse
import collections
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
LINE = re.compile(r'^\[\s*(\d+) ms\]\[T\s*\d+\] (.*)$')
KEY = re.compile(r'map loaded|UEngine|UGameEngine|UWorld|travel|Browse|LoadMap|[Ll]isten|'
                 r'[Cc]onnect|[Ff]ail|ERROR|FATAL|WARN|detach|possess|card05\d:')
MAX_LINES = 2_000_000
GAP_SHOW = 8


def read_log(path: pathlib.Path) -> list[tuple[int, str]]:
    """Return (ms, text) for every timestamped line; lines without a stamp are skipped."""
    rows = []
    with path.open(encoding='utf-8', errors='replace') as f:
        for i, raw in enumerate(f):
            if i >= MAX_LINES:
                print(f'WARN: {path.name} truncated at {MAX_LINES} lines', file=sys.stderr)
                break
            m = LINE.match(raw.rstrip('\n'))
            if m:
                rows.append((int(m.group(1)), m.group(2)))
    return rows


def prefix(text: str) -> str:
    m = re.match(r'^([A-Za-z0-9_ ]{1,30}):', text)
    return m.group(1) if m else '(none)'


def print_counts(side: str, rows: list[tuple[int, str]]) -> None:
    span = (rows[-1][0] - rows[0][0]) / 1000.0
    errs = sum(1 for _, t in rows if 'ERROR' in t or 'FATAL' in t)
    warns = sum(1 for _, t in rows if 'WARN' in t)
    print(f'{side}: {len(rows)} lines over {span:.1f} s, ERROR/FATAL {errs}, WARN {warns}')
    top = collections.Counter(prefix(t) for _, t in rows).most_common(12)
    print('  prefixes: ' + ', '.join(f'{p} {n}' for p, n in top))


def print_gaps(side: str, rows: list[tuple[int, str]], gap_s: float, t0: int) -> None:
    """Print the longest gaps over gap_s, longest first, at most GAP_SHOW of them.

    The mod log is an event log, not a per-frame log, so a gap is only a hint: look at
    what the game was doing (a load, a travel) before calling it a hitch."""
    limit = int(gap_s * 1000)
    gaps = [(rows[i][0] - rows[i - 1][0], i) for i in range(1, len(rows))
            if rows[i][0] - rows[i - 1][0] > limit]
    gaps.sort(reverse=True)
    print(f'  {side}: {len(gaps)} gaps over {gap_s} s; longest {min(len(gaps), GAP_SHOW)}:')
    for dt, i in gaps[:GAP_SHOW]:
        at = (rows[i - 1][0] - t0) / 1000.0
        print(f'  {side} gap {dt / 1000.0:.2f} s at {at:.2f}')
        print(f'    before: {rows[i - 1][1][:150]}')
        print(f'    after:  {rows[i][1][:150]}')


def print_timeline(sides: dict, t0: int, extra: re.Pattern | None, show_all: bool,
                   max_lines: int) -> None:
    merged = sorted((ms, side, text) for side, rows in sides.items() for ms, text in rows)
    shown = 0
    for ms, side, text in merged:
        key = KEY.search(text) and not text.startswith('hooks:')
        keep = show_all or key or (extra is not None and extra.search(text))
        if not keep:
            continue
        if shown >= max_lines:
            print(f'... stopped at --max {max_lines}')
            break
        shown += 1
        print(f'{(ms - t0) / 1000.0:8.2f} {side:7} {text[:200]}')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('tag')
    ap.add_argument('--grep', help='extra regex for timeline lines')
    ap.add_argument('--gap', type=float, default=1.0, help='report gaps longer than this (s)')
    ap.add_argument('--max', type=int, default=300, help='timeline line cap')
    ap.add_argument('--all', action='store_true', help='timeline prints every line')
    args = ap.parse_args()
    sides = {}
    for side in ('host', 'client', 'client2'):
        path = REPO / 'logs' / f'{args.tag}-{side}.log'
        if path.is_file():
            rows = read_log(path)
            if rows:
                sides[side] = rows
    if not sides:
        print(f'no logs/{args.tag}-host.log or -client.log or -client2.log with timestamped lines', file=sys.stderr)
        return 1
    t0 = min(rows[0][0] for rows in sides.values())
    print('== counts')
    for side, rows in sides.items():
        print_counts(side, rows)
    print(f'== gaps over {args.gap} s')
    for side, rows in sides.items():
        print_gaps(side, rows, args.gap, t0)
    print('== timeline (s from first line)')
    extra = re.compile(args.grep) if args.grep else None
    print_timeline(sides, t0, extra, args.all, args.max)
    return 0


if __name__ == '__main__':
    sys.exit(main())
