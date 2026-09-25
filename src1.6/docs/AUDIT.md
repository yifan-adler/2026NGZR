# 1.3.3-fixed 合法输入保留审计

审计范围为 src1.3.2 → src1.3.3A → src1.3.3B 的入口、索引安全改动及其规划消费者；在新目录修复，不修改上述版本。验收以原比赛题的实际语义为准，不以新出题规则把已有重复题自动降为非法题。

## 风险与处置

| 路径 | 发现的问题/审计结论 | 本版处置与证据 |
|---|---|---|
| SemanticInstructionKey → Preflight → tasks/constraints | B 删除重复出现，改变计分、风险阈值、聚合权重、遍历顺序和计划候选 | 默认保留出现序列；唯一 key 与 duplicate 计数仍生成；显式 `-deduplicate 1` 保留 B 去重机制。19 道重复比赛题不再被自动改写 |
| NL get_task_instruction → ValidateInstruction | give-to-human 带 Y，被 unary guard 拒绝；01 NL 丢失首项 give | 区分 IT literal-human 与 NL explicit-human-Y，仅允许 human 大物体 Y；新增测试，35 题 NL 快照对照 |
| InstructionMarkerAt | 左括号与标签之间合法空白不识别，可能漏任务或把 constraint child 升格 | 跳过标准空白再识别完整 token；空白 wrapper 测试覆盖 |
| ParseInstruction malformed recovery | 完整 action/cond 仅缺外层右括号也被整项丢弃 | 只在深度为 1 且发现下一 marker 时补闭简单 task/info，再走 schema；04 能保留五项。坏 constraint payload 不做该修复 |
| WorldState human 布局 | B 把 human 不是 ID 1、数量异常变为全题硬拒绝 | warning；仍按对象注册与类型保证访问安全。无 human 不影响 close 等任务，give 继续局部校验 |
| WorldState big location | B 在 Stage 1 因位置碰撞整题拒绝 | 改为 warning，保留既有世界与求解；Stage 2 未知/错误位置继续原纠错链 |
| WorldState 无关对象信息不完整 | 一个无关未定型对象导致整题被拒绝 | warning + 指令局部绑定隔离；稀疏槽不可成为合法任务对象 |
| robot 身份、对象注册、越界 | 指针/ID 不一致或位置越界确实可能破坏后续索引安全 | 保留全局 gate；测试确认缺机器人位置时零平台调用 |
| ParseEnvSentence/ParseEnv | 完整 token 数值解析、有界 ID、叶子 fact、延后 hold/plate/inside 关联 | 保留；合法事实重排、晚 size/type 不覆盖容器状态；35 题对象状态快照核对 |
| 条件解析/类型推断 | B 增加 id/type 条件和运行时类型要求；每个谓词有对应实际操作类型 | 保留；未显式给 type 从实际对象推断；错类型/空绑定/稀疏绑定局部拒绝；合法集合每个成员均校验，不拆集合 |
| NL 空 token/空栈/空树、父子引用环 | 合法树走原语法，坏句局部拒绝；clear_tree 解除 shared_ptr 环 | 保留；NL 全题快照及 ASan/UBSan 验证 |
| Cons_plan / CalculateTaskRisk | A 增加 UNKNOWN 位置与空指针 guard，防止负索引；合法位置原公式不变 | 保留；默认重复出现恢复原风险权重；不同极性、冲突任务均保留 |
| FilterConstraintsByTaskConflicts | A 用动态 vector 替换固定 100 项栈数组；删除旧代码无条件清零 goto_cons[0] 的越界式尾迭代 | 保留纠错。旧版无冲突也打印 location 0 零项，该无效日志在对照时单独规范化；不恢复误清合法位置 0 的缺陷 |
| EnsureObject/Location/EvidenceCapacity | A 分离对象行和位置列，限制负/超大值；普通合法 ID 不改变状态 | 保留；MAX_OBJECT_ID=255 与已有 256 槽并查集一致，MAX_LOCATION_ID=4095，输入 1MiB。超出此实现支持范围不属于已证明兼容范围 |
| SolveTask / DoBehavious / 原子动作 | A 添加对象、运行时类型、行列访问保护；有效绑定走相同分支 | 保留，官方全动作和候选日志对照；无效指令不进入动作链 |
| GetSmall/BigObjectStatus / AskLoc / Sense | 严格核对回复 ID、格式及范围；UNKNOWN 仍沿原询问与感知路径 | 保留；固定 srand 种子提供相同随机证据，原平台二进制与随机算法不改 |
| TerminalChecker / Candidate / Score / Deadline | 除 TerminalChecker 从 hasMissingObjects 改为 IsUsable 外，独立算法文件与 1.3.2 相同 | 合法任务 IsUsable 为 true；通过解析集合、官方最终 value 条目及关键决策日志追踪 |
| 计时内诊断日志 | B 对成功的 inferred 类型多次逐项打印，对重复项逐项打印，并扩展每个 ToString | 默认省略成功推断和保留副本的逐项日志；保留 summary、错误和真实 repair 日志，unique 模式仍记重复 key。降低新增容错日志对实时预算的扰动 |
| Plan/Fini 跨题重置 | Report、tasks、约束与世界清理；调用配置需跨题持久 | 保留 report 重置；deduplicate_input 是显式运行契约，不随 Fini 丢失 |

## 不可混淆的验证层

- 原始官方运行：真实 SDK、真实 5 秒墙钟、原 XML、实际 score/time/action/answer-set。
- 固定种子官方运行：测试专用 srand interposer；必须在 server.log 出现种子确认，预加载失败即停止。平台二进制、随机算法、动作、时钟、评分不改。
- 离线解析：同一个测试主程序分别链接两个版本，比较对象、条件、X/Y 绑定、出现顺序与数量、纠错后状态；同时跑 IT/NL。
- 官方证据回放：两个完整 Plan 使用同一动作/回复带和同一测试时钟；严格逐个核对请求与最终状态，用于隔离墙钟噪声。回放不是官方评分，不能替代实时验收。

## 题源与结论边界

02 按用户明确指示不再测试。04、05 原始 XML 可被平台装载，但 instruction 括号破损导致官方评分器不生成 vtask.lp，vanswer 为空。两版原样返回的 0 分必须标为 evaluator_failed，不能计作合法题得分相同或通过。03 的旧式注释被 Python 标准 XML parser 拒绝、TinyXML 接受，runner 仅读取 flags，不改写 XML。

完整逐题证据、计分分解和是否满足最终验收条件见 VALIDATION.md。有限题集与边界测试证明覆盖范围内的行为；不宣称能证明所有任意输入都无缺陷。
