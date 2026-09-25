# 高分约束收益挑战（30 题）

本组有 10 道正收益题（P）、10 道负收益题（N）和 10 道正常对照题（C）。每类同时覆盖 Stage 1 与 Stage 2：全组 Stage 1 为 22 题，Stage 2 为 8 题。每题包含 21–23 个互不重复的目标和 1 条约束，题面理论满分为 860–940 分；SDK 官方得分还会叠加时间奖励，因此可能高于题面理论满分。

题目从上一轮已评分的小题生成，再加入不涉及主要交易对象的已完成放置目标。负收益的 near／inside 题改为只损失一条约束，同时失去一个原先完成的目标，避免通过“两条约束直接禁行”的旧门槛。每题保留 IT 结构化指令和逐条对应的 NT 自然语言。`manifest.json` 记录类别、阶段、目标数、约束数、理论满分与补充目标数。

运行 `python3 src1.6/tests/generate_tradeoff_highscore.py` 可重建 XML。评分使用 `src1.6/tests/run_tradeoff_challenge.py --fixture-dir 题目/constraint_tradeoff_highscore_2026`，分别运行修改前、修改后和不动作客户端，在 IT／NT 两种模式下比较确定性基础分和官方分。测试结果见[高分题报告](../../test-results/constraint-tradeoff-highscore-20260924/REPORT.md)。
