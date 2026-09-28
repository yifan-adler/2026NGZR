#!/usr/bin/env python3
"""Summarize official paired results produced by run_guarded_compare.py."""

import argparse
import collections
import hashlib
import json
import re
import shutil
import statistics
import zipfile
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(rows):
    groups = collections.defaultdict(list)
    for row in rows:
        groups[(row["stage"], row["mode"])].append(row)
    result = []
    for (stage, mode), group in sorted(groups.items()):
        deltas = [round((r["current"]["platform_seconds"] -
                         r["baseline"]["platform_seconds"]) * 1000, 1)
                  for r in group if r["current"]["platform_seconds"] is not None and
                  r["baseline"]["platform_seconds"] is not None]
        outcomes = collections.Counter()
        for row in group:
            for message in row["current"].get("guarded_log", []):
                found = re.search(r"outcome=([^\s]+)", message)
                if found:
                    outcomes[found.group(1)] += 1
        result.append({
            "stage": stage, "mode": mode, "cases": len(group),
            "baseline_base": sum(r["baseline"]["base"] for r in group),
            "current_base": sum(r["current"]["base"] for r in group),
            "baseline_official": sum(r["baseline"]["official"] for r in group),
            "current_official": sum(r["current"]["official"] for r in group),
            "base_gains": sum(r["current"]["base"] > r["baseline"]["base"]
                              for r in group),
            "base_losses": sum(r["current"]["base"] < r["baseline"]["base"]
                               for r in group),
            "changed_actions": sum(r["current"]["action_sequence"] !=
                                   r["baseline"]["action_sequence"] for r in group),
            "timeouts": sum(r["current"]["timed_out"] for r in group),
            "mean_time_delta_ms": round(statistics.mean(deltas), 2) if deltas else None,
            "outcomes": dict(outcomes),
        })
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", action="append", required=True,
                        help="NAME:RESULTS_JSON")
    parser.add_argument("--baseline-binary", type=Path, required=True)
    parser.add_argument("--current-binary", type=Path, required=True)
    parser.add_argument("--sdk-server", type=Path, required=True)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--modecheck-results", type=Path)
    parser.add_argument("--seed", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    metadata = {
        "release": "1.6.1",
        "seed": args.seed,
        "baseline_binary_sha256": digest(args.baseline_binary),
        "current_binary_sha256": digest(args.current_binary),
        "sdk_server_sha256": digest(args.sdk_server),
    }
    if args.source_root:
        metadata["source_sha256"] = {
            path.name: digest(path) for path in sorted(args.source_root.iterdir())
            if path.suffix in (".cpp", ".hpp")
        }
    lines = ["# 1.6.1 受控决策官方配对验收", "",
             "同一 SDK、5000 ms 时限、种子 " + args.seed + "；基础分按正式目标、"
             "计分约束和真实动作成本复算。", ""]
    all_rows = []
    summaries = {}
    with zipfile.ZipFile(output / "official-logs.zip", "w",
                         compression=zipfile.ZIP_DEFLATED) as archive, \
         zipfile.ZipFile(output / "official-inputs.zip", "w",
                         compression=zipfile.ZIP_DEFLATED) as inputs_archive:
      for raw in args.suite:
        name, filename = raw.split(":", 1)
        rows = json.loads(Path(filename).read_text(encoding="utf-8"))
        if any(r["baseline"]["status"] != "ok" or
               r["current"]["status"] != "ok" or
               r["baseline"]["base"] is None or
               r["current"]["base"] is None for r in rows):
            raise ValueError("incomplete official case in " + name)
        all_rows.extend((name, row) for row in rows)
        stored_cases = set()
        for row in rows:
            case = Path(row["case"])
            arcname = name + "/stage" + str(row["stage"]) + "/" + case.name
            if arcname in stored_cases:
                continue
            if digest(case) != row["case_sha256"]:
                raise ValueError("case changed after evaluation: " + str(case))
            inputs_archive.write(case, arcname)
            stored_cases.add(arcname)
        for log in sorted(Path(filename).parent.glob("*/client.log")):
            archive.write(log, name + "/" + log.parent.name + "/client.log")
        for log in sorted(Path(filename).parent.glob("*/server.log")):
            archive.write(log, name + "/" + log.parent.name + "/server.log")
        groups = summarize(rows)
        summaries[name] = groups
        lines.extend(["## " + name, "",
                      "| 阶段／输入 | 配对 | 基础分原版→当前 | 官方分原版→当前 | "
                      "提分／退步 | 动作变化 | 当前超时 | 平均计时差 ms |",
                      "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"])
        for item in groups:
            lines.append("| Stage {} {} | {} | {}→{} | {}→{} | {}/{} | {} | {} | {} |".format(
                item["stage"], item["mode"].upper(), item["cases"],
                item["baseline_base"], item["current_base"],
                item["baseline_official"], item["current_official"],
                item["base_gains"], item["base_losses"],
                item["changed_actions"], item["timeouts"],
                item["mean_time_delta_ms"]))
        lines.append("")
        regressions = [row for row in rows if
                       row["current"]["base"] < row["baseline"]["base"]]
        if regressions:
            lines.append("基础分退步：" + ", ".join(
                "{} Stage {} {} ({}→{})".format(Path(r["case"]).name,
                    r["stage"], r["mode"], r["baseline"]["base"],
                    r["current"]["base"]) for r in regressions) + "。")
            lines.append("")
    if args.modecheck_results:
        modecheck = json.loads(args.modecheck_results.read_text(encoding="utf-8"))
        shutil.copyfile(str(args.modecheck_results),
                        str(output / "stage2-modecheck.json"))
        with zipfile.ZipFile(output / "stage2-modecheck-logs.zip", "w",
                             compression=zipfile.ZIP_DEFLATED) as archive:
            for log in sorted(args.modecheck_results.parent.glob("*/client.log")):
                archive.write(log, log.parent.name + "/client.log")
            for log in sorted(args.modecheck_results.parent.glob("*/server.log")):
                archive.write(log, log.parent.name + "/server.log")
        same = sum(row["baseline"]["action_sequence"] ==
                   row["current"]["action_sequence"] for row in modecheck)
        lines.extend(["## Stage 2 模式复核", "",
                      "同一 1.6.1 二进制以 `off` 和 `guarded` 运行 3 道历史题，"
                      "{} 道动作相同、{} 道不同。第 19 题在接近时限时出现分数波动；"
                      "详见 `stage2-modecheck.json` 与 `stage2-modecheck-logs.zip`。".format(
                          same, len(modecheck) - same), ""])
    total_gains = sum(r["current"]["base"] > r["baseline"]["base"]
                      for _, r in all_rows)
    total_losses = sum(r["current"]["base"] < r["baseline"]["base"]
                       for _, r in all_rows)
    total_official_before = sum(r["baseline"]["official"] for _, r in all_rows)
    total_official_after = sum(r["current"]["official"] for _, r in all_rows)
    stage2_changes = sum(r["stage"] == 2 and
                         r["current"]["action_sequence"] !=
                         r["baseline"]["action_sequence"]
                         for _, r in all_rows)
    lines.extend([
        "## 验收结论", "",
        "共 {} 组配对：基础分提高 {} 组、下降 {} 组；官方分合计 {}→{}（{:+d}）。".format(
            len(all_rows), total_gains, total_losses,
            total_official_before, total_official_after,
            total_official_after - total_official_before), "",
        "受控任务组仅在 Stage 1 的可完整模拟场景接管；Stage 2 的新决策入口均回退。"
        "Stage 2 有 {} 组动作与冻结的原 1.6 不同，未达到计划中的动作一致门槛。".format(
            stage2_changes), "",
        "原 60 题官方分合计变化 {:+d}；1.6.1 保持显式 `guarded` 开关，默认沿用旧模式。".format(
            sum(r["current"]["official"] - r["baseline"]["official"]
                for name, r in all_rows if name == "user60")), "",
        "每题的实际分数、动作序列、计时和选择日志见 `results.json`。", "",
        "二进制与 SDK SHA-256 见 `metadata.json`，题目与原始平台日志分别见 `official-inputs.zip` 和 `official-logs.zip`。", "",
    ])
    (output / "REPORT.md").write_text("\n".join(lines), encoding="utf-8")
    (output / "metadata.json").write_text(json.dumps(
        metadata, indent=2), encoding="utf-8")
    (output / "summary.json").write_text(json.dumps(
        summaries, indent=2), encoding="utf-8")
    (output / "results.json").write_text(json.dumps(
        [{"suite": name, **row} for name, row in all_rows],
        ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
