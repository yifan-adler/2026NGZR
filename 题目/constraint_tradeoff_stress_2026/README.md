# 约束决策变形压力题（40 题）

这一组以 12 个已核实的高分题原型生成 40 道变形题，题面理论满分 860–960 分；Stage 1 有 19 道，Stage 2 有 21 道。包含 9 道正收益、22 道负收益、9 道正常对照。SDK 官方分会另加时间奖励，可能超过题面理论满分。

四种变形分别检验：切换 Stage 1/2 已知程度、逆转任务次序、把一个原先已完成的目标改成真实待完成目标、以及恢复第二条受保护约束。后一种变形有 4 道，专门测试多个约束同时受损的决策。每题的 IT 与 NT 指令逐条同步，`manifest.json` 记录原型和变形方式。

生成方式：`python3 src1.6/tests/generate_tradeoff_stress.py`。验证方式：运行 `src1.6/tests/run_tradeoff_challenge.py`，并指定 `--fixture-dir 题目/constraint_tradeoff_stress_2026`。逐题结果见[测试报告](../../test-results/constraint-tradeoff-stress-20260924/REPORT.md)。
