# Preflight 与输入恢复契约

入口：ParseEnv → IT/NL parser → RunQuestionPreflight → ParseInfo → Stage 2 correction → Cons_plan → TaskOptimization → execution。

## 重复项

默认 `deduplicate_input=false`，保留全部合法出现，包括源条件不同但恰巧绑定相同的指令。唯一键和重复统计是诊断视图；planner、风险、multi-goto/multi-puton、终态与计分继续消费原始出现序列。

显式 `-deduplicate 1` 表示调用者声明输入采用唯一目标契约。此时按 B 的语义 key 稳定保留首次出现，支持 `in/inside`、`near/nextto`、near 对称与完整绑定集合；不合并不同 ID、不同类别/极性、不同集合或相反动作。非法题回归必须显式启用该契约。

`raw` 为解析成功进入列表的条数，`unique` 为有效唯一 key 数，`duplicates` 为后续重复次数，`rejected` 为局部拒绝数。默认模式满足保留条数 = raw - rejected；唯一模式满足保留条数 = unique。重复调用不会继续改变默认模式的列表。

## WorldState

| 检查 | 行为 |
|---|---|
| robot 身份/初始位置不可用，对象 ID 与注册槽不一致、位置越界 | 全局错误，在任何平台动作前停止 |
| human 非 ID 1、不唯一或缺失 | warning；give 仍单独检查实际 human 目标，其余任务继续 |
| Stage 1 大物体位置碰撞 | warning；不把世界布局约定当作内存安全条件 |
| 无关对象无 sort/运行时类型 | warning；引用不能建模对象的指令单独隔离 |
| 稀疏空槽 | 保留；不能成为有效指令绑定 |
| Stage 2 未知位置、未知开关状态、位置描述碰撞 | 保留既有询问、感知和纠错语义 |

## Schema 与恢复

IT give 使用 `(give human X)`；NL give 可以使用 X + 显式 human Y，两者均保留其解析表示。Y 若指向非 human、或运行时类型/注册无效，仍拒绝。

标准 S-expression marker 允许左括号与标签之间的空格、换行、制表符。简单 task/info 若 action 与 cond 已完整且仅外层右括号缺失，可在下一 marker 前补闭并重新进行完整 schema 校验。缺失内层结构、未知谓词或非法参数不猜测修复。

constraint 仍整体维护极性边界：多 child、非法嵌套、错误 child kind 的闭合 wrapper 整体拒绝；坏括号流恢复后续同级 marker，已消费的 constraint payload 不升格为 task。

边界：客户端不修复原 XML、题源与 IT/NL 对齐，不判断所有约束的初始真值。来源未声明唯一目标时，不从“文本重复”推断输入非法。
