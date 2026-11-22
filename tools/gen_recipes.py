#!/usr/bin/env python3
"""Writes tools/recipes/<step>.txt: per-step scenario lines that `objective do` runs instead of a kind routine.

C1M1 tutorial steps whose kind does not name what completes them (TacSupport_SO3 is kind other with actor[0]
BP_EncounterManager_C; SO4-SO7 are kind marker but need a tac call, a tac pickup or a flare; ShootingRange_SO3 is
a 10-target count). soak1m1b366a capped TacSupport_SO3 ("routine not built") and left 11 of 18 rows open.
Each recipe is the proven c335-range-load slice for that step, without its `objective expect` (objective do has
already expected the step and read `objective now`). The slice ends on the step's own `objective wait`.

usage: python3 -I tools/gen_recipes.py
"""
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "scenarios", "c335-range-load.txt")
OUT = os.path.join(ROOT, "recipes")
# step id -> (first line, last line) of SRC, 1-based and inclusive. The last line is the step's `objective wait`
# (TacSupport_SO6 also keeps the two client copy waits for the flare and the bombing run projectile).
SLICES = {
    "TacSupport_SO3": (194, 239),
    "TacSupport_SO4": (240, 250),
    "TacSupport_SO41": (252, 255),
    "TacSupport_SO5": (257, 263),
    "TacSupport_SO6": (265, 273),
    "TacSupport_SO7": (274, 274),
    "ShootingRange_SO2": (291, 294),
    "ShootingRange_SO3": (297, 1000),
}
BB = "BP_AI_BrainBug_C"
CLEAR = "host clear 20000 kill max-left=99"
STAGE = 'try host wait log "card049: host t=\\d+ CompleteSubObjective\\(BrainBug_O1, {}\\)" timeout=20'
# Hand-written recipes (step id -> lines). Each ends on the step's own `objective wait` (BrainBug_SO7 completes only
# by cascade from BrainBug_O1, so it waits on BrainBug_O1; `objective do` then reads SO7 as complete).
# FED_C4M2 Brain Bug stages (kind kill, actor[0] BP_AI_BrainBug_C): the c502-c4m2.txt boss block (lines 758-781),
# without its `unless BrainBug_O1` guards. c502b280a completed SO3, SO5 and O1 with it. One damage step per stage;
# the clears end each stage's encounter (SO4 and SO6 are encounter stages between them).
# FED_C5M2 Finale_SO1 (kind count, no actor; count = live assassins of waves C_6 and C_8, card 247 row F1):
# c247b311a completed it (3/3, host 26996641) with clears at the finale stand as the assassins arrived, about
# one every 15 s. Six clear-and-wait rounds give it 90 s.
LITERAL = {
    "BrainBug_SO3": [
        f'try host wait log "card098: host spawn {BB} " timeout=30',
        CLEAR,
        "client ammo fill",
        f"try host warp client {BB} 900",
        f"try client look actor {BB} 0 bounds hit=BP_AI_BrainBug",
        "try client key LeftMouseButton 5000",
        CLEAR,
        f"try host kill actor {BB} 30",
        "host objective wait BrainBug_SO3 15",
    ],
    "BrainBug_SO5": [
        CLEAR,
        STAGE.format("BrainBug_SO4"),
        CLEAR,
        f"try host kill actor {BB} 40",
        "host objective wait BrainBug_SO5 15",
    ],
    "BrainBug_SO7": [
        CLEAR,
        STAGE.format("BrainBug_SO6"),
        CLEAR,
        f"try host kill actor {BB}",
        "host objective wait BrainBug_O1 20",
    ],
    "Finale_SO1": [x for _ in range(6) for x in (CLEAR, "try host objective wait Finale_SO1 15")]
    + ["host objective wait Finale_SO1 30"],
}
# FED_C3M1 Resort_SO2 (kind marker, actor ActorReference_C_9): the level script counts satchel pickups and completes
# it at satchelCount == 3 (card 242 row 8). soak3m1b366c ran the presence routine at the marker for 309 s with no
# Complete row; it completed only when the Resort_SO3 satchel routine took the third pickup (host 97077404). The
# client takes each live pickup the way drive.py satchel_take does; `try` because a pickup taken earlier is gone.
TAKE = [
    "try host warp client BP_SatchelItemPickup_C 100 live",
    "try client look actor BP_SatchelItemPickup_C 0 bounds live focus=BP_SatchelItemPickup_C",
    "try client key E",
]
LITERAL["Resort_SO2"] = [x for _ in range(3) for x in TAKE] + ["host objective wait Resort_SO2 15"]
WAIT_ON = {"BrainBug_SO7": "BrainBug_O1"}


def main():
    with open(SRC, encoding="ascii") as f:
        lines = f.read().splitlines()
    os.makedirs(OUT, exist_ok=True)
    for step, (a, b) in SLICES.items():
        body = lines[a - 1:b]
        last = body[-1] if step != "TacSupport_SO6" else body[-3]
        if not last.startswith(f"host objective wait {step} "):
            sys.exit(f"{step}: slice {a}-{b} does not end on its objective wait: {last[:80]}")
        if any(x.startswith("host objective expect ") for x in body):
            sys.exit(f"{step}: slice {a}-{b} holds an objective expect")
        # drive.py load() counts "# ---" and "# ===" lines as group and stop marks; a recipe has neither.
        body = [x for x in body if not x.startswith(("# ---", "# ==="))]
        with open(os.path.join(OUT, f"{step}.txt"), "w", encoding="ascii") as f:
            f.write(f"# {step}: c335-range-load.txt lines {a}-{b}, written by tools/gen_recipes.py. Do not edit.\n")
            f.write("\n".join(body) + "\n")
        print(f"{step}: {len(body)} lines")
    for step, body in LITERAL.items():
        if not body[-1].startswith(f"host objective wait {WAIT_ON.get(step, step)} "):
            sys.exit(f"{step}: hand-written recipe does not end on its objective wait: {body[-1][:80]}")
        with open(os.path.join(OUT, f"{step}.txt"), "w", encoding="ascii") as f:
            f.write(f"# {step}: hand-written in tools/gen_recipes.py LITERAL. Do not edit.\n")
            f.write("\n".join(body) + "\n")
        print(f"{step}: {len(body)} lines (hand-written)")


if __name__ == "__main__":
    main()
