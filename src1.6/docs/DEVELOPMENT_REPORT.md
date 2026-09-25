# src1.5 开发报告：Decision Feedback Loop

日期：2026-09-24。基于 src1.4.1。只增加候选决策、执行结果及误差的记录；任务顺序、风险门槛、截止门槛、动作流程与官方评分逻辑未改。

本报告记录原始 1.5 反馈版本；后续的决策修改见[约束收益规划](CONSTRAINT_TRADEOFF_PLANNER.md)。

## 1. 当前决策流程审计

`RDFW` 保存世界对象、事实值、Verified 标志、EvidenceSource、任务与全过程约束 ledger。`TerminalChecker` 从世界和 ledger 计算目标与约束终态；`ScoreEvaluator` 按 40×目标 + 20×计分约束 − 动作成本计算确定性基础分。`EvaluateShadowCandidates` 为可用任务重算风险，`BuildCandidatePlan` 隔离 dry-run，产出动作序列、前后终态、分数、目标增减、约束破坏/保持、成本、边际分、效用与预计时长。候选按效用排序用于旁路展示；主循环仍按原任务顺序与风险条件执行，StopGate 和期限门槛阻止不能完整执行的计划；最终回收沿用已有正边际收益规则。MultiGoto 沿用既有汇聚策略。

原版的动作反馈在 `RDFW` 原子动作、Sense、AskLoc 中更新世界值、来源及 ledger；`AfterSolveTask` 和 `RefreshTaskStates` 更新任务状态；`ScoreEvaluator` 逐动作累计成本。原版 `EndCandidateExecution` 只记录执行动作计数/剩余预算，没有持久的实际结果。因此候选的预期目标/约束、证据、动作成本和实际完成情况在计划结束后无法按同一个 ID 比较。尤其 MultiGoto 的临时 puton 任务在 dry-run 评分时曾被误算成一个正式目标，必须从反馈预测中剔除。

## 2. 修改文件

- `candidate_plan.hpp/.cpp`：新增预测、证据、实际结果、误差、执行记录结构；预测直接从已有 TerminalSummary 和 ScoreSnapshot 计算。
- `rdfw.hpp/.cpp`：候选 ID、生成时间、世界修订号、证据快照、实际动作/失败记录、内存反馈历史及 JSON 日志；MultiGoto 临时任务的预测评分口径修正。
- `terminal_checker.hpp/.cpp`：在既有计分判断旁同步输出逐条 `constraint_credited`，供预测/实际的 gain/loss 对照；计分判断本身未改。
- `tests/recovery_evidence_tests.cpp`：验证重新激活候选生成新 ID、预测与执行关联及零误差。
- `tests/run_feedback_regression.py`：官方 SDK 六题 IT/NT 回归，校验候选 ID 配对、误差、证据、约束损失、低收益放弃及原始动作反馈与 1.4.1 一致。

## 3. CandidatePlan 新增数据与预测来源

`candidate_id` 在每次构建时递增；`generated_ms` 记录本次求解已过毫秒；`world_revision` 标记构建时世界版本；`task_indices` 标记包含的正式任务；`evidence` 保存各对象 location/inside/container_state 的来源与 Verified 状态。`prediction` 包含 goal gain/loss、constraint gain/loss、预计最终目标/计分约束数、动作数、成本、最终基础收益和边际 utility。当前评分器没有效率奖励预测，因此不伪造效率分；官方总分中的时间奖励只用于赛后对照。

预测从现有 dry-run 的前后 `ScoreSnapshot` 和 `TerminalSummary` 得到。`final_reward` 等于 `score_after.deterministic_base_score`，`utility` 等于后减前；动作成本来自同一评分器的差值。约束 gain/loss 使用与 `credited_constraints` 完全一致的逐条计分资格，能区分恢复表面状态与全过程约束分。MultiGoto 临时 puton 只用于生成动作，剔除临时目标后重新使用同一计算函数生成正式任务预测。

## 4. Actual、误差与日志

`BeginCandidateExecution` 保存执行前真实终态及分数；原子动作调用 `RecordAction` 计数、计成本，返回 false 时记录动作名及失败；`EndCandidateExecution` 从执行后终态及分数计算 Actual，连同原 CandidatePlan 写入 `DecisionFeedback()` 历史。`candidate_id` 是一轮内唯一关联键。失败原因可记录 `<Action>_returned_false`，任务未完成而没有动作失败时记录 `task_not_completed`。Sense 是无布尔返回的官方 API，不能从该接口判定感知动作是否失败。

误差统一定义为 `actual − prediction`，字段为 `goal_gain`、`constraint_gain`、`constraint_loss`、`action_cost`、`utility`。日志每行包含 `[DecisionFeedback]` 后的 JSON 对象，`schema=decision_feedback.v1`：

- `candidate_considered`：ID、阶段、任务、旧选择标志、资格、dry-run 是否完成、预测；可审计未选计划。
- `candidate_selected`：ID、任务、生成时刻、世界修订、selection_reason、完整预测、expected_reward、evidence。
- `candidate_result`：同一 ID、预测、实际、是否成功、失败动作/原因、误差。

日志只有记录作用；未据误差调整风险、权重、策略或参数。内存记录的 `CandidateExecutionRecord` 提供未来 `candidate feature → execution outcome` 数据接口。

## 5. 六题正式回归

环境：Ubuntu 18.04、2026 官方 SDK、5000 ms；01–06 各 IT/NT 一次。动作与平台回复流均与 src1.4.1 对应模式逐项一致。下表官方分含时间奖励，基础分仍按官方目标/约束扣动作成本计算。

| 题 | IT/NT 官方分 | 目标 | 计分约束 | 动作/成本 | 执行候选数（每模式） | 检验重点 |
|---|---:|---:|---:|---:|---:|---|
| 01 | 292/292 | 7/7 | 0/0 | 16/48 | 6 | MultiGoto；临时任务不虚增预测目标 |
| 02 | 372/372 | 9/9 | 0/0 | 16/48 | 4 | puton 与 goto 共享终态 |
| 03 | 373/373 | 7/7 | 2/2 | 9/25 | 2 | Sense 后续候选的 evidence 中保留 sense 来源 |
| 04 | 304/304 | 6/6 | 2/2 | 15/40 | 4 | 候选重新生成、ID 关联；定向单测覆盖 reactivation |
| 05 | 296/296 | 6/6 | 0/1 | 10/20 | 5 | 首个 putin 预测约束损失 1；关门后仍计 0 |
| 06 | 322/322 | 5/6 | 3/3 | 7/20 | 2 | give 候选不合格、预计约束损失 3、utility −114；未执行 |

六题每个 IT/NT 已执行候选的五项误差均为 0。五组 CTest 单测全部通过。完整原始证据、客户端 JSON 日志、官方评分和汇总结果见 `test-results/liuyifan1.0-src1.5-verified-20260924/`。官方总分与 src1.4.1 的历史运行可因耗时奖励不同而有小幅变化；基础分、动作和终态不变。

## 6. 保留接口与当前问题

`CandidatePlan` 保存决策时特征和证据，`CandidateExecutionRecord` 保存结果和逐项误差，可供 1.6+ 独立读取。本版没有概率预测、Bayesian shadow、自动学习或参数更新。

当前预测与实际都使用同一内部世界模型，误差为 0 不能证明模型与真实世界永远一致；六题只是小规模回归。官方时间效率分没有事前预测。MultiGoto 在某些布局下的单独 final-move 候选沿用原有简单成本估计，若最终移动本身改变多个 goto 终态，预测可能产生非零目标误差，应由本版记录暴露给后续分析。失败原因仅能按现有动作布尔返回值归类，平台未提供更细的失败码。
