# 1.3.3-fixed

以 src1.3.3B 为代码基础，以 src1.3.2 的合法题行为为回归基准。

默认保留题目中 task/constraint 的出现次数、顺序、绑定和权重。重复内容在旧比赛中可能承载计分与聚合权重，仅根据绑定相同无法判断它是否为非法副本。Preflight 仍建立语义唯一键和重复统计；只有调用者明确传入 `-deduplicate 1`，才采用 B 的唯一目标契约并删除重复副本。不得按题号自动切换契约。

修复内容：

- NL give 的显式 human Y 绑定不再被误判为非法一元参数；IT `(give human X)` 仍按原表示保留。
- WorldState 全局停止只用于机器人身份/初始位置、对象注册和越界等无法安全继续的问题。human 布局、大物体同位置、无关对象类型不完整改为诊断；无效指令仍局部隔离。
- S-expression marker 支持左括号后的空白。仅缺最外层右括号、action/cond 已完整的简单指令可在下一个 marker 处恢复；坏 constraint 的内部 task 仍不提升为任务。
- 继承有界整数、对象与位置维度分离、空树/空栈/空绑定保护、局部非法事实隔离、运行时类型检查和 malformed wrapper recovery。

CandidatePlan、ScoreEvaluator、DeadlineManager 源码与 1.3.2 相同；TerminalChecker 仅继承 A 的 IsUsable 安全条件。求解选择、风险阈值与聚合策略使用保留下来的原始出现序列。

详见 [PREFLIGHT.md](PREFLIGHT.md)、[AUDIT.md](AUDIT.md)、[VALIDATION.md](VALIDATION.md)。

测试入口：

```sh
python3 src1.3.3-fixed/tests/compare_legal.py --output /tmp/fixed-native-new
python3 src1.3.3-fixed/tests/compare_legal.py --output /tmp/fixed-seeded-new --seed 20260923
python3 src1.3.3-fixed/tests/compare_parse.py /tmp/fixed-parse-new
```

以上必须在 WSL2 Ubuntu-18.04 内运行；默认使用 `/home/yifan/env-release-2026`，输出目录必须尚不存在。02 已按用户明确要求排除。固定种子仅作为补充归因实验，不改变时间、随机算法、评分公式或客户端二进制；完整原始运行也必须保留。
