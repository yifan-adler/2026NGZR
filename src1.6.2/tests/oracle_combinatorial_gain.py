#!/usr/bin/env python3
"""Exhaustively score all task orders in deterministic container-route cases."""

import argparse
import itertools
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path


def parse_case(path):
    root = ET.parse(str(path)).getroot()
    info = root.findtext("env/info")
    instructions = root.findtext("instr")
    locations = {int(obj): int(place) for obj, place in
                 re.findall(r"\(at\s+(\d+)\s+(\d+)\)", info)}
    sorts = {kind: int(obj) for obj, kind in
             re.findall(r"\(sort\s+(\d+)\s+(\w+)\)", info)}
    goals = [(verb.capitalize(), sorts[kind], locations[sorts[kind]])
             for verb, kind in re.findall(
                 r"\(:task\s+\((open|close) X\)\s+\(:cond\s+\(sort X (\w+)\)\)\)",
                 instructions)]
    assert len(goals) in (4, 5, 6) and len(set(goals)) == len(goals)
    for verb, obj, _ in goals:
        state = "closed" if verb == "Open" else "opened"
        assert re.search(r"\(" + state + r"\s+" + str(obj) + r"\)", info)
    return locations[0], goals


def route(start, goals):
    at = start
    actions = []
    for verb, obj, place in goals:
        if at != place:
            actions.append("Move " + str(place))
            at = place
        actions.append(verb + " " + str(obj))
    moves = sum(action.startswith("Move ") for action in actions)
    return 40 * len(goals) - 2 * len(goals) - 4 * moves, actions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-dir", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = json.loads(args.results.read_text(encoding="utf-8"))
    output = []
    for fixture in sorted(args.fixture_dir.glob("*.xml")):
        start, goals = parse_case(fixture)
        all_routes = [route(start, order) for order in itertools.permutations(goals)]
        optimum = max(score for score, _ in all_routes)
        evaluated = [row for row in rows if Path(row["case"]).name == fixture.name]
        assert len(evaluated) == 2
        for row in evaluated:
            assert row["stage"] == 1 and row["mode"] in ("it", "nt")
            for label in ("baseline", "current"):
                actual = row[label]
                assert (actual["base"], actual["action_sequence"]) in all_routes, (
                    fixture.name, row["mode"], label)
        output.append({
            "case": fixture.name,
            "goals": len(goals),
            "order_count": len(all_routes),
            "optimal_base": optimum,
            "optimal_routes": [actions for score, actions in all_routes
                               if score == optimum],
            "official_results": {row["mode"]: {
                "baseline_base": row["baseline"]["base"],
                "current_base": row["current"]["base"],
                "current_is_optimal": row["current"]["base"] == optimum,
            } for row in evaluated},
        })
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2),
                           encoding="utf-8")
    for item in output:
        print(item["case"], "orders", item["order_count"],
              "optimum", item["optimal_base"],
              "IT", item["official_results"]["it"]["baseline_base"],
              "->", item["official_results"]["it"]["current_base"])


if __name__ == "__main__":
    main()
