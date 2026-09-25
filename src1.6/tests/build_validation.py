#!/usr/bin/env python3
"""Render the verified release audit as a version-local review document."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
result=ROOT/'test-results'
name={'seed':'realcompetition-src133-fixed-final-seed-20260923',
      'natural':'realcompetition-src133-fixed-final-natural',
      'repaired':'realcompetition-src133-fixed-repaired-pair-final'}
load=lambda n:json.loads((result/name[n]/'analysis.json').read_text(encoding='utf-8'))
seed=load('seed');natural={r['id']:r for r in load('natural')};repair={r['id']:r for r in load('repaired')}
audit=json.loads((result/'realcompetition-src133-fixed-release-audit.json').read_text(encoding='utf-8'))
assert audit['source_hashes']=='verified' and audit['seed']['losses']==[]
lines=['# 1.3.3-fixed 验证与验收','',
'日期：2026-09-23。系统：WSL2 Ubuntu 18.04.2，g++ 7.5.0，官方 `/home/yifan/env-release-2026`，Stage 2 / IT，平台 5000 ms。版本源码/SDK/题源 SHA-256 与实际运行产物核对通过。',
'',
'按用户后续要求，**02 不再测试**。其余 35 题均用未修改 XML 分别运行 1.3.2、1.3.3-fixed。04、05 的原始 instruction 括号损坏，评分器无法生成 `vtask.lp`；这两题原样运行的 0 分无有效 answer-set，明确标记 `evaluator_failed`，不能冒充过关。',
'',
'作为补充，另生成仅补齐右括号的 04/05 测试副本：04 增加 5 个 `)`，05 增加 1 个 `)`，其余非括号 token 不变，原 XML 文件未修改。两版在相同副本和固定随机种子下均得 205 分、22 个相同动作。副本的 SHA-256、逐字符 diff 和两版原始日志均保存。',
'',
'完整原题固定种子运行中，33 道能正常评分的题目**无一降分**；其中 31 道动作序列完全一致。28、29 两题，修复版在相同观测下各多执行一组 `Move/PickUp/Move/PutDown`，各多完成 2 项目标，官方分分别 +60。另有无效评分的 05 原题动作相同。新增 Preflight 对这 33 题的 rejected/world_errors 均为 0。IT 与 NL 的离线解析/绑定/纠错后快照共 70 对，仅 04 的损坏 IT 题存在预期恢复差异。',
'',
'无固定种子的原题实跑中，33 道有效题有 1 道（15）修复版降 32 分。两版在相同 AskLoc(11) 请求处首次收到不同随机回答：`not_known` 与 `inside(11,6)`；随后动作路径分叉。固定种子复核中 15 的解析、任务、动作、观测、决策与官方分完全相同。因此该单轮失分有可追溯的外部随机证据差异，不能归因于新增容错机制。原始日志中的其他 11 道动作差异同样首次出现在同一 AskLoc 请求的不同回答。',
'',
'固定种子由测试专用 `srand` 预加载层控制，保留平台随机算法、时钟、动作和评分器二进制；每份 server.log 均记录种子确认。无固定种子的整套原样实验也完整保留。官方分数含真实耗时奖励，动作及终态相同但相差 2 分时，逐题分解为目标/约束基础分、动作成本和时间奖励。',
'',
'四组单元测试与四组 ASan/UBSan 测试均通过；20 道标记非法的负向题以显式 `-deduplicate 1` 跑过，去重、schema 局部隔离和 malformed recovery 均有日志。',
'',
'## 逐题对照','',
'“动作”列为 **固定种子**的动作序列对比；官方得分格式为 1.3.2→1.3.3-fixed。04/05 固定种子列的 0 分为原题评分器失败，旁边给出修复副本结果。',
'',
'| 题 | 原题固定种子官方分 | 原题无种子官方分 | 固定种子动作 | 解释 |',
'|---|---:|---:|---|---|']
for row in seed:
    case=row['id'];n=natural[case]
    pair=lambda v:'→'.join(map(str,v))
    if case in repair:
        note='原题评分器失败；补括号副本 {} 分、{} 动作均相同'.format(pair(repair[case]['scores']),repair[case]['action_counts'][0])
        action='原题无有效评分' if case=='04' else '原题动作相同'
    elif case in ('28','29'):
        note='同证据下修复版多执行一组搬运，多完成 2 项；候选/截止门控分叉见逐题 diff'
        action='基线为修复版前缀'
    elif not n['observations_equal']:
        d=n['first_event_difference']
        note='无种子首分叉：{} → {}'.format(d['baseline'],d['fixed'])
        action='完全相同' if row['actions_equal'] else '固定种子相同'
    else:
        note='解析/任务/约束/观测/动作/关键决策相同'
        action='完全相同'
    lines.append('|{}|{}|{}|{}|{}|'.format(case,pair(row['scores']),pair(n['scores']),action,note))
lines += ['',
'## 可追溯产物','',
'- `../test-results/realcompetition-src133-fixed-final-seed-20260923/`：最终源码构建哈希、35 对原题日志、官方 evaluator 输出、逐字段 diff、analysis.json、report.md。',
'- `../test-results/realcompetition-src133-fixed-final-natural/`：35 对无固定种子原题实跑与逐题解释。',
'- `../test-results/realcompetition-src133-fixed-repaired-fixtures-final/`：04/05 最小括号修复副本、原题 SHA、改动 diff。',
'- `../test-results/realcompetition-src133-fixed-repaired-pair-final/`：两版对修复副本的官方实跑。',
'- `../test-results/realcompetition-src133-fixed-parse-final/`：70 对 IT/NL 解析、WorldState、任务/约束、纠错后状态快照。',
'- `../test-results/realcompetition-src133-fixed-replay-final/`：相同回复及测试时钟下的计划回放；32 对完成且最终状态相同，06 两版共同在第 41 个请求处与录制轨迹不符，此回放不作为官方评分证据。',
'- `../test-results/realcompetition-src133-fixed-unit/`：单元、ASan/UBSan 日志。',
'- `../test-results/realcompetition-src133-fixed-release-audit.json`：覆盖、哈希和测试门槛检查结果。',
'',
'**验收结论：**在 33 道可原样评分题以及 04/05 的仅补括号副本上，固定外部随机证据后未发现由新增容错机制导致的失分或策略退化。02 已按用户要求排除。由于 04/05 原题的官方评分器无法有效评分，严格按“原样 35 题全部有效官方得分”理解的验收条件无法满足；其余证据和修复副本结果可供审查。',
'']
dest=ROOT/'src1.3.3-fixed/VALIDATION.md';dest.write_text('\n'.join(lines),encoding='utf-8')
print(dest)
