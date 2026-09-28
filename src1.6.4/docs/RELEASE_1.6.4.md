# src1.6.4：State Invariant / World State Correctness

独立基线为已完成的 src1.6.3。先读 RELEASE_1.6.3、现有 CTest、评分审计和实现，再建立 src1.6.4。所有修改及证据位于本版本；原版本、SDK、原题均未改。最终构建源码、基线发布源码、SDK 与 86 对题源哈希的核对见 `../test-results/validation-20260927/manifest.json`。

## 1. 审计路径

审计 Object/SmallObject/BigObject/Container/Robot 的构造与指针表示；ParseEnvSentence/ParseEnv/ParseInfo、GetSmallObjectStatus/GetBigObjectStatus/AskLoc、两条 Sense 管线；所有九种平台动作和对应 SolveTask/HoldSmallObject/TakeOutLogic；SearchConditionObject、ValidateInstruction、QuestionPreflight；must-near/must-in/open-close correction；TerminalChecker/LegacyPriorityChecker/ScoreEvaluator、约束资格与历史 UNKNOWN；BuildTaskGroupPlan 的保存恢复、合成 PutOn、收尾 Move、授权动作签名、world_revision、failed_task_revision 和感知缓存。

只读核对 SDK `res/evaluate.lp`、`forcons.lp`、`fortask.lp`、`src/platform.cpp`、`src/evaluate.cpp`。没有改评分函数、legacy 优先级、搜索选项或期限常量。

## 2–4. 真实问题、根因与修复

| 问题 | 根因 | 最小修复 / 文件 |
|---|---|---|
| 直接构造对象的 id/location 颠倒 | SmallObject、BigObject 调用 Object 构造器的参数顺序错误，Container 随之受影响 | `rdfw.hpp` 修正参数顺序；未改变从已注册 Object 转换的路径 |
| inside 与容器成员表分叉、重复成员和旧 on 残留 | 多个更新只改 small->inside，不维护旧容器 vector；ParseInfo(inside) 每次追加；携带 setter 未清除旧 on | `rdfw.cpp` 小型成员清理函数用于成功动作、inside/plate 信息更新；`rdfw.hpp` setter 清除 on 并保证同一对象不能同时占据 hold/plate |
| 成功动作验证了错误的容器旧位置 | Open/Close/PutIn/TakeOut 仅设置位置证据 verified，没有把容器位置改为机器人当前位置 | 成功分支统一确认容器当前位置，按 inside 自身证据强度及约束依赖传播内容物位置；旧位置感知缓存失效 |
| on/near 信息错误证明“物体在外部” | 将同地谓词当作 inside=NONE，并将 inside 标为已验证 | 保留同地语义；不从 on/near 证明外部。若新位置与旧容器关系冲突，只撤销关系为 UNKNOWN、移除旧成员，不强行确定为 NONE |
| 弱 inside 经 goto 升级为已验证位置 | 看见容器即认为物体确实在容器内，未检查 inside 是否验证 | 只有已验证 inside 才能结合看到容器证明物体位置；当前位置分支沿用现有两次尝试界限，不能验证则退出，避免循环无界 |
| must-near 旧推导不随锚点失效 | 推导不作为独立票源，但刷新时没有撤销过期值；原传播还把未改变的弱初始锚点改标为推导 | 锚点未知、改变、冲突时撤销依赖位置；仅剩弱锚点时降级验证强度；与所选位置一致的弱初始来源继续保留为弱来源 |
| dry-run 前置条件不同于 SDK | Open/Close 未要求空手或相应开关值；UNKNOWN=-1 经 bool 视为 opened；Move 允许原地；PickUp 未排除托盘同物体 | `DryRunActionSucceeds` 对已知必要条件做精确比较，Unknown 不充当打开事实，恢复 SDK 已有前置条件 |
| 未知开关被当成已打开 | Sense 的缺席清理与 dry-run Sense 都用三值 `isOpen` 作 bool；UNKNOWN=-1 被解释为 true | 只有 `isOpen==1` 才按已打开处理；未知开关下不因内容物未被看到就清空位置 |
| must-open 在 UNKNOWN 上没有形成状态事实 | 推导分支用 `!isOpen` 判断是否写 open=1；UNKNOWN=-1 不进入分支，却标记证据已验证 | must-open 和 not-closed 的打开分支均改为 `isOpen != 1`；推导结果与 TerminalChecker 一致 |
| derived inside 的依赖在动作后丢失 | 成功容器动作确认容器位置时，把依赖 constraint-derived inside 的内容物位置标成直接 ACTION_SUCCESS；原约束失效后位置仍被当真 | 内容物位置保留 CONSTRAINT_DERIVED 来源，TerminalChecker 继续检查支持约束的资格 |
| 收尾 Move 投影与真实执行不一致 | 手工改 location/成本/账本，遗漏携带证据、感知失效、derived 刷新及 Move 的既有携带清理 | `PreviewFinalMove` 复用现有 BuildTaskGroupPlan 快照及真实 Move 封装；没有增加候选、动作策略或搜索分支 |

产品代码仅改 `rdfw.hpp`、`rdfw.cpp`、版本 CMakeLists；测试改 `tests/CMakeLists.txt`、stub、新增 `state_invariant_tests.cpp`、`baseline_probe/CMakeLists.txt`。配对工具更新默认版本及定向案例参数；新增证据打包工具；README 与本报告更新。

## 5. 回归测试

新增 20 个 CTest 状态用例（0–19）：对象构造；成功动作确认错误位置；取放/托盘/移动的成员迁移；重复 inside 与位置关系；UNKNOWN 动作前置条件；九种失败动作不改变物理事实；任务组投影保存恢复与实执行终态/分数一致；弱 inside 不验证 goto；must-near 锚点改变使旧推导失效；Ask 回答继续 UNKNOWN；未知开关下 Sense 不确定包含关系；PutIn/TakeOut/Open/Close 全链及 terminal 互斥结果；稳定绑定随真实状态更新评分且约束资格不恢复；收尾 Move 投影一致；on/near 不证明外部；感知缓存与携带互斥；弱初始锚点保留及随后撤销；动作确认容器时保留内容物位置对 derived inside 的依赖；开关 UNKNOWN 时 Sense 不误判内容物缺席，must-open 将 UNKNOWN 正确推导为 open。

将最终新增测试直接编译到**未改动的 src1.6.3**，使用只供测试的可控平台 stub：20 项中 15 项失败、5 项通过，失败断言均保留在 `probe-ctest.log`。这用于证明问题及保护既有正确行为，不作为基线 CTest 失败。原 src1.6.3 的正式 CTest 仍全部通过。

## 6–7. 验证和版本差异

最终结果及动作差异以 `../test-results/validation-20260927/summary.json`、`behavior-differences.json` 和各套原始证据 zip 为准。单元测试全部包含原有测试，没有删除或放宽断言。

开发中的失效逻辑曾使 03 IT/NT 从基础分 334 降为 0：原传播把全部弱位置标记成推导，而新刷新发现没有独立锚点后撤销全部位置，完整投影因此失败。这是本轮实现回归，已修复来源标记并增加用例 16；最终验收重新构建及重跑，不使用此前两轮作为发布通过证据。修复前断言日志也单独保留。

| 检查 | 最终结果 |
|---|---|
| src1.6.3 原 CTest / ASan+UBSan | 11/11、10/10 通过 |
| src1.6.4 CTest / ASan+UBSan | 31/31、30/30 通过，包含全部原有测试；无 sanitizer 错误 |
| 官方评分语义 | 16 个原案例全部通过 |
| 原有 guarded 全量 | 86/86 对同基础分、同动作；0 增分、0 降分 |
| 03、06、29 IT/NT，各三轮交替执行顺序 | 18/18 对同基础分、同动作；03=334，06=330，29=856 |
| guarded off 原路径 | 14/14 对同基础分、同动作 |
| 最终官方运行总数 | 236 次有效运行；0 硬超时、0 动作成本不匹配、0 内部下界高于官方基础分 |

118 对最终配对的目标、约束计分与动作成本均相同；7 对含耗时奖励的官方原始总分相差 2 分（全量 5 对、定向 2 对），差额完全落在时间奖励，详见 `timing-differences.json`。没有将耗时奖励差异说成基础分回归或收益。未发现本轮状态修复改变最终回归动作；新增最小状态用例中的行为变化是上述已复现的问题修正。

## 8. 保留的语义与 State Model 风险

- location 是规划器物理位置；inside 物体同时有容器位置不必然是两个互斥场景事实。Stage 1 的显式 ASP at 保存在 score_locations，PutIn 后评分 at 被删除而导航位置继续保留；未修改这项设计。
- 初始 Stage 2 env 与 Ask 回答继续是弱证据。初始 env 与 Ask 回答没有升级为验证事实。`ParseInfo` 对指令补充信息仍沿用 1.6.3 的可信处理；SDK 的评分器忽略普通 `:info` 指令，所以它不能单独证明官方世界真值。若题目允许错误补充信息，这条路径需要 1.6.5 明确分层并以官方可复现实验确定强度。
- 失败的原子动作不改变物理事实，但仍有官方动作扣分和失败诊断；FromPlate 失败后的启发式限制清理、Move 内在发出 Move 前的成功 PutDown/FromPlate 仍保留。复合调用最终失败不意味着此前成功的平台动作回滚。
- task binding 条件只含 id/sort/color/type，运行时这些身份属性和注册指针不变。位置、inside、opened 改变不使对象绑定失效；评分重新读事实/枚举条件绑定。不增加没有证据需求的全量重绑定机制。未来新增动态属性或替换注册对象需要相应失效机制。
- SDK 的 Open/Close 失败结合已确认同地、空手等前置条件，可证明已经处于目标开关状态；这是既有确定规则，不简单把所有失败都当成“状态未知”。
- Stage 2 尚无完整的“物理位置 / 显式 ASP at / 带依赖证据”三层模型；inside 位置与官方 at 的差别仍可能使内部估计不同于官方。当前测试通过不能证明所有 Stage 2 下界都是严格数学下界。
- constraint-derived 证据仍按对象/来源及粗粒度约束账本关联，没有完整依赖图。此次修复组内 must-near 锚点过期，未实现所有跨容器、多约束、矛盾来源的真值维护系统。初始化纠错函数也未设计为任意运行时重放接口。
- Stage 2 的 dry-run Sense/Ask 仍从内部假设生成回复，不能预言平台未知状态；保留原 guarded 仅支持 Stage 1 的范围。UNKNOWN 的目标、约束和历史资格之间的联合关系仍未求解。
- 单状态内复用 TerminalSummary 是局部优化，没有跨状态终态/得分缓存。本版检查感知记录与 world_revision 的现有更新；未重构 legacy 风险表为完备增量依赖系统。
- 平台同步调用及负载抖动仍可能改变末段动作准入。测试中的无硬超时与重复动作一致只覆盖记录的运行。

## 9. 下一版建议

建议进入 src1.6.5 Evidence / Fact / Derived State 分层。优先显式记录证据强度与依赖、弱假设与验证事实、derived 的撤销条件，并使真实执行与所有 projection 共用状态转移。先保持本版回归矩阵与 1.6.3 评分/期限契约，再逐步替换并行标记；不要以 heuristic 或宽松上界补造世界真值。
