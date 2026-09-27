#!/usr/bin/env python3
"""Decompose official scores and explain every paired difference from raw logs."""
import difflib
import json
from pathlib import Path
import re
import sys
from compare_legal import VERSIONS, extract, trace

def first_diff(a,b):
    for i in range(max(len(a),len(b))):
        x=a[i] if i<len(a) else None
        y=b[i] if i<len(b) else None
        if x!=y:return dict(index=i,baseline=x,fixed=y)
    return None

def metrics(path):
    data=json.loads((path/'summary.json').read_text())
    data.update(extract(path))
    answer=path/'runtime/vanswer.txt'
    result=answer.read_text(errors='replace') if answer.exists() else ''
    data['values']=[list(map(int,t)) for t in re.findall(r'value\((\d+),(-?\d+)\)',result)]
    data['evaluator_valid']='SATISFIABLE' in result and 'UNSATISFIABLE' not in result
    data['base_score']=sum(v for _,v in data['values'])
    data['action_cost']=sum((4 if a.startswith('Move ') else 1 if a=='Sense' else 2) for a in data['actions'])
    data['time_bonus']=(data['raw_score']-data['base_score']+data['action_cost']) if data['evaluator_valid'] else None
    data['events']=[m.strip() for m in re.findall(r'^\s*(\[[A-Za-z_]+[^\]]*\])\s*$',trace(path/'server.log'),re.M)]
    data['decision_evidence']=[dict(line=i+1,text=s) for i,s in enumerate(trace(path/'client.log').splitlines())
        if any(k in s for k in ('[3B][StopGate]','[3B][Deadline]','[3A][final]'))]
    return data

def summarize(root):
    rows=[]
    for row in json.loads((root/'comparison.json').read_text()):
        case=row['id'];a,b=[metrics(root/v/case) for v in VERSIONS]
        item=dict(id=case,scores=[a['official_score'],b['official_score']],
                  raw_scores=[a['raw_score'],b['raw_score']],
                  base_scores=[a['base_score'],b['base_score']],costs=[a['action_cost'],b['action_cost']],
                  time_bonus=[a['time_bonus'],b['time_bonus']],times=[a['platform_seconds'],b['platform_seconds']],
                  goals=[a['final_goals'],b['final_goals']],constraints=[a['credited_constraints'],b['credited_constraints']],
                  action_counts=[len(a['actions']),len(b['actions'])],
                  valid=all(x['evaluator_valid'] for x in (a,b)),
                  actions_equal=a['actions']==b['actions'],observations_equal=a['observations']==b['observations'],
                  parsed_equal=a['parsed']==b['parsed'],decisions_equal=a['decisions']==b['decisions'],
                  first_event_difference=first_diff(a['events'],b['events']),
                  first_decision_difference=first_diff(a['decisions'],b['decisions']),
                  decision_evidence={VERSIONS[0]:a['decision_evidence'],VERSIONS[1]:b['decision_evidence']})
        if not item['valid']:
            explanation='原题指令括号损坏，vtask.lp 未生成，官方 answer-set 为空；0 分不构成有效验收。'
            if case=='04':explanation+='基线只提取 give，fixed 恢复全部五项；原样动作 4→19。'
        elif item['actions_equal'] and item['observations_equal']:
            explanation='动作及观测相同；'
            if a['base_score']==b['base_score'] and a['action_cost']==b['action_cost']:
                explanation+='目标/约束得分和动作成本相同。'
                if a['raw_score']!=b['raw_score']:explanation+='原始分差仅来自官方实际耗时奖励（2 分一档）。'
            else:explanation+='终态计分差异需人工复核。'
        else:
            d=item['first_event_difference'] or {}
            x=d.get('baseline') or '';y=d.get('fixed') or ''
            if x.startswith('[AskLoc') and y.startswith('[AskLoc') and x.split('|')[0]==y.split('|')[0]:
                explanation='首个分叉是同一 AskLoc 请求收到不同平台回答；后续路径与终态沿不同证据演化。'
            elif a['events']==b['events'][:len(a['events'])] or b['events']==a['events'][:len(b['events'])]:
                explanation='完整动作/回复流为严格前缀关系；候选逻辑在剩余墙钟预算阈值处分叉，详见 Deadline/StopGate 行。'
            else: explanation='需逐项检查 first_event_difference、first_decision_difference 与完整 diff。'
        if item['valid']:
            explanation+=' 得分分解 Δraw = Δ目标约束 {} - Δ成本 {} + Δ耗时奖励 {} = {}。'.format(
                b['base_score']-a['base_score'],b['action_cost']-a['action_cost'],
                b['time_bonus']-a['time_bonus'],b['raw_score']-a['raw_score'])
            for d in (a,b):
                expected=2*int((5-d['platform_seconds'])*10)
                assert abs(d['time_bonus']-expected)<=2,(case,d['time_bonus'],expected)
        item['explanation']=explanation
        rows.append(item)
    (root/'analysis.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    lines=['# 逐题官方对照与差异归因','',
        '完整 actions/observations/parsed/decisions diff 位于同目录；analysis.json 保留首个分叉和对应原始日志行。',
        '官方原始分 = 最终 value 总分 − 所有动作成本 + 2×int((5−实际秒数)×10)；展示分按 1000 封顶。',
        '', '|题号|官方分 1.3.2→fixed|目标数|约束数|动作数|动作/观测/解析/决策一致|说明|',
        '|---|---|---|---|---|---|---|']
    pair=lambda a:'→'.join(map(str,a))
    for r in rows:
        eq='/'.join('是' if r[k+'_equal'] else '否' for k in ('actions','observations','parsed','decisions'))
        lines.append('|{id}|{score}|{goal}|{cons}|{act}|{eq}|{explanation}|'.format(
            score=pair(r['scores']),goal=pair(r['goals']),cons=pair(r['constraints']),act=pair(r['action_counts']),eq=eq,**r))
    (root/'report.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(root,len(rows),'valid',sum(r['valid'] for r in rows),
          'actions equal',sum(r['actions_equal'] for r in rows),
          'score losses',[(r['id'],r['scores']) for r in rows if r['scores'][1]<r['scores'][0]])
for arg in sys.argv[1:]:summarize(Path(arg).resolve())
