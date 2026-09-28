#!/usr/bin/env python3
"""Generate deterministic Stage 1 goal-ordering challenges for 1.6.1."""

import argparse
from pathlib import Path


CASES = {
    "01-two-rooms-four-goals": [
        ("open", "cupboard", 2),
        ("close", "closet", 3),
        ("close", "refrigerator", 2),
        ("open", "microwave", 3),
    ],
    "02-two-rooms-five-goals": [
        ("open", "cupboard", 2),
        ("close", "closet", 3),
        ("open", "refrigerator", 2),
        ("close", "microwave", 3),
        ("open", "door", 2),
    ],
    "03-two-rooms-six-goals": [
        ("open", "cupboard", 2),
        ("close", "closet", 3),
        ("open", "refrigerator", 2),
        ("close", "microwave", 3),
        ("open", "door", 2),
        ("close", "washmachine", 3),
    ],
    "04-three-rooms-six-goals": [
        ("open", "cupboard", 2),
        ("close", "closet", 3),
        ("open", "refrigerator", 4),
        ("close", "microwave", 2),
        ("open", "door", 3),
        ("close", "washmachine", 4),
    ],
}


def render(goals):
    info = ["(hold 0) (plate 0) (at 0 1)",
            "(sort 1 human) (size 1 big) (at 1 1)"]
    tasks = []
    natural = []
    for object_id, (verb, kind, location) in enumerate(goals, 2):
        state = "closed" if verb == "open" else "opened"
        info.append("(sort {0} {1}) (size {0} big) (type {0} container) "
                    "({2} {0}) (at {0} {3})".format(
                        object_id, kind, state, location))
        tasks.append("(:task ({0} X) (:cond (sort X {1})))".format(
            verb, kind))
        natural.append("{} the {}.".format(verb.capitalize(), kind))
    return "\n".join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<test>',
        '<env mis="off" err="off" ans="off">',
        '<info>', *info, '</info>',
        '<mis></mis><err><r></r><w></w></err><extra></extra>',
        '</env>',
        '<instr>', '(:ins', *tasks, ')', '</instr>',
        '<nl>', *natural, '</nl>',
        '</test>', ''
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).parent / "fixtures" /
                                "combinatorial_gain")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name, goals in CASES.items():
        path = args.output / (name + ".xml")
        path.write_text(render(goals), encoding="utf-8")
        print(path)


if __name__ == "__main__":
    main()
