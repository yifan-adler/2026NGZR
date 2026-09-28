#!/usr/bin/env python3
"""Independent exhaustive action oracle for the three-goal route fixture."""

import argparse
import itertools
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path


def facts(info):
    locations = {int(obj): int(place) for obj, place in
                 re.findall(r"\(at\s+(\d+)\s+(\d+)\)", info)}
    sorts = {kind: int(obj) for obj, kind in
             re.findall(r"\(sort\s+(\d+)\s+(\w+)\)", info)}
    return locations, sorts


def routes(fixture):
    root = ET.parse(str(fixture)).getroot()
    info = root.findtext("env/info")
    locations, sorts = facts(info)
    goals = [(action.capitalize() if action != "pickup" else "PickUp",
              sorts[kind]) for action, kind in re.findall(
                  r"\(:task\s+\((\w+) X\)\s+\(:cond\s+\(sort X (\w+)\)\)\)",
                  root.findtext("instr"))]
    assert len(goals) == 3 and len(set(goals)) == 3
    scores = []
    for order in itertools.permutations(goals):
        at = locations[0]
        actions = []
        cost = 0
        for action, obj in order:
            place = locations[obj]
            if at != place:
                actions.append("Move " + str(place))
                cost += 4
                at = place
            actions.append(action + " " + str(obj))
            cost += 2
        scores.append((40 * len(goals) - cost, actions))
    return scores


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    args = parser.parse_args()
    scores = routes(args.fixture)
    optimum = max(score for score, _ in scores)
    rows = [row for row in json.loads(args.results.read_text(encoding="utf-8"))
            if Path(row["case"]).name == args.fixture.name]
    assert len(rows) == 2
    for row in rows:
        current = row["current"]
        assert (current["base"], current["action_sequence"]) in scores
        assert current["base"] == optimum
    print(json.dumps({"permutations": len(scores), "optimal_base": optimum,
                      "optimal_routes": [actions for score, actions in scores
                                         if score == optimum]}, indent=2))


if __name__ == "__main__":
    main()
