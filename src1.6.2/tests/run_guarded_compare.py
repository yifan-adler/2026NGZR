#!/usr/bin/env python3
"""Run paired official cases with the same SDK and fixed inputs."""

import argparse
import hashlib
import importlib.util
import json
import os
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True


def load_runner(path):
    spec = importlib.util.spec_from_file_location("rdfw_baseline_runner", str(path))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.run_case


def base_score(result):
    goals = result["final_goals"]
    constraints = result["credited_constraints"]
    if goals is None or constraints is None:
        return None
    log = Path(result["output"]) / "server.log"
    actions = re.findall(r"^\s*\[([A-Za-z_]+)(?:\s[^|]*)?\|", log.read_text(
        encoding="utf-8", errors="replace"), re.M)
    cost = sum(4 if action.lower() == "move" else
               1 if action.lower() == "sense" else 2
               for action in actions)
    return 40 * goals + (20 * constraints if goals else 0) - cost


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path,
                        help="Path to src1.1.2 (x)/tools/baseline.py")
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--current", required=True, type=Path)
    parser.add_argument("--words", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--seed-library", type=Path)
    parser.add_argument("--seed", default="20260924")
    parser.add_argument("--baseline-mode", choices=("off", "guarded"), default="off")
    parser.add_argument("--current-mode", choices=("off", "guarded"), default="guarded")
    parser.add_argument("--modes", nargs="+", choices=("it", "nt"), default=["it"])
    parser.add_argument("--case-dir", action="append", default=[],
                        help="STAGE:DIRECTORY; load every XML file in name order")
    parser.add_argument("--auto-stage-dir", action="append", default=[], type=Path,
                        help="Load XML files and infer Stage 2 from env flags")
    parser.add_argument("cases", nargs="*", help="STAGE:PATH")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    assets = output / "assets"
    assets.mkdir()
    (assets / "words.txt").write_bytes(args.words.read_bytes().replace(b"\r\n", b"\n"))
    if args.seed_library:
        os.environ["LD_PRELOAD"] = str(args.seed_library.resolve())
        os.environ["RDFW_TEST_SEED"] = args.seed
    run_case = load_runner(args.runner.resolve())
    cases = list(args.cases)
    for raw in args.case_dir:
        stage_text, directory_text = raw.split(":", 1)
        cases.extend(stage_text + ":" + str(path) for path in
                     sorted(Path(directory_text).glob("*.xml")))
    for directory in args.auto_stage_dir:
        for path in sorted(directory.glob("*.xml")):
            # The official SDK accepts legacy XML that ElementTree rejects.
            env = re.search(r"<env\s+([^>]+)>", path.read_text(
                encoding="utf-8"))
            if not env:
                raise ValueError("missing env flags in " + str(path))
            flags = dict(re.findall(r'(mis|err|ans)="(on|off)"', env.group(1)))
            stage = 1 if all(flags.get(key) == "off" for key in
                             ("mis", "err", "ans")) else 2
            cases.append(str(stage) + ":" + str(path))
    if not cases:
        parser.error("at least one case or --case-dir is required")
    rows = []
    for raw in cases:
        stage_text, case_text = raw.split(":", 1)
        stage = int(stage_text)
        case = Path(case_text).resolve()
        for mode in args.modes:
            pair = {}
            for label, executable, group_mode in (
                    ("baseline", args.baseline, args.baseline_mode),
                    ("current", args.current, args.current_mode)):
                os.environ["RDFW_TASK_GROUP_MODE"] = group_mode
                run_dir = output / (case.stem + "-s" + str(stage) + "-" + mode + "-" + label)
                result = run_case(args.sdk.resolve(), assets, executable.resolve(),
                                  case, stage, mode, run_dir, 5000, None)
                result["output"] = str(run_dir)
                server_log = (run_dir / "server.log").read_text(
                    encoding="utf-8", errors="replace")
                if args.seed_library and "[RDFW_TEST_SEED] " + args.seed not in server_log:
                    raise RuntimeError("server did not confirm fixed seed in " + str(run_dir))
                pair[label] = {
                    "status": result["status"],
                    "base": base_score(result),
                    "official": result["raw_score"],
                    "goals": result["final_goals"],
                    "constraints": result["credited_constraints"],
                    "actions": result["actions"],
                    "action_sequence": re.findall(
                        r"^\s*\[([A-Za-z_]+(?:\s+[^|]*?)?)\|",
                        (run_dir / "server.log").read_text(
                            encoding="utf-8", errors="replace"), re.M),
                    "platform_seconds": result["platform_seconds"],
                    "timed_out": result["platform_timed_out"],
                    "seed_confirmed": bool(args.seed_library),
                    "guarded_log": re.findall(r"\[GuardedDecision\][^\n]*",
                        (run_dir / "client.log").read_text(
                            encoding="utf-8", errors="replace")),
                }
            row = {"case": str(case), "case_sha256": hashlib.sha256(
                       case.read_bytes()).hexdigest(), "stage": stage, "mode": mode,
                   "baseline": pair["baseline"], "current": pair["current"]}
            rows.append(row)
            (output / "results.json").write_text(json.dumps(
                rows, ensure_ascii=False, indent=2), encoding="utf-8")
            print(case.stem, stage, mode, pair["baseline"]["base"],
                  pair["current"]["base"], pair["current"]["guarded_log"],
                  flush=True)
    return int(any(row["baseline"]["status"] != "ok" or
                   row["current"]["status"] != "ok" for row in rows))


if __name__ == "__main__":
    raise SystemExit(main())
