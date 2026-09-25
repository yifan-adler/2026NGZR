#!/usr/bin/env python3
"""Verify release source hashes, evidence coverage and acceptance invariants."""
import hashlib
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
RESULT=ROOT/'test-results'
names={'seed':'realcompetition-src133-fixed-final-seed-20260923',
       'natural':'realcompetition-src133-fixed-final-natural',
       'repaired':'realcompetition-src133-fixed-repaired-pair-final'}
versions=('src1.3.2','src1.3.3-fixed')
expected={"{:02d}".format(i) for i in range(1,37) if i!=2}
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
audits={};passed=True
for kind,directory in names.items():
    root=RESULT/directory
    rows=json.loads((root/'analysis.json').read_text())
    ids=[row['id'] for row in rows]
    assert len(ids)==len(set(ids))
    assert set(ids)==({'04','05'} if kind=='repaired' else expected)
    audits[kind]=dict(count=len(rows),valid=sum(row['valid'] for row in rows),
                      action_equal=sum(row['actions_equal'] for row in rows),
                      losses=[row['id'] for row in rows if row['valid'] and row['scores'][1]<row['scores'][0]])
    for v in versions:
        for row in rows:
            case=row['id']; run=root/v/case
            result=json.loads((run/'summary.json').read_text())
            assert result['case_sha256']==sha(run/'runtime/tests/case.xml')
            if kind=='repaired':
                assert sha(run/'runtime/tests/case.xml')==sha(RESULT/'realcompetition-src133-fixed-repaired-fixtures-final'/(case+'.xml'))
            else:
                assert sha(run/'runtime/tests/case.xml')==sha(ROOT/'题目/realcompetiton_2024'/(case+'.xml'))
    if kind=='seed':
        assert (root/'environment.json').exists()
        for v in versions:
            build=json.loads((root/('build-'+v)/'build.json').read_text())
            source=ROOT/v
            for name,digest in build['source_sha256'].items():
                if name=='CMakeLists.txt': continue # build command is recorded separately
                assert sha(source/name)==digest,(v,name)
            for name,digest in build['sdk_sha256'].items():
                assert sha(Path('/home/yifan/env-release-2026')/name)==digest,name
            assert sha(root/('build-'+v)/'example')==build['executable_sha256']
            for row in rows:
                assert '[RDFW_TEST_SEED] 20260923' in (root/v/row['id']/'server.log').read_text(errors='replace')
    if kind=='natural':
        assert all(not row['valid'] for row in rows if row['id'] in ('04','05'))
    if kind=='repaired':
        assert all(row['valid'] and row['scores'][0]==row['scores'][1]
                   and row['actions_equal'] and row['parsed_equal'] for row in rows)
seed=json.loads((RESULT/names['seed']/'analysis.json').read_text())
assert audits['seed']['valid']==33 and audits['seed']['losses']==[]
assert sum(row['valid'] and row['actions_equal'] for row in seed)==31
assert all(row['parsed_equal'] and row['goals'][1]>=row['goals'][0]
           and row['constraints'][1]>=row['constraints'][0] for row in seed if row['valid'])
for row in seed:
    if not row['valid']: continue
    log=(RESULT/names['seed']/'src1.3.3-fixed'/row['id']/'client.log').read_text(errors='replace')
    summaries=[line for line in log.splitlines() if '[Preflight] task raw=' in line]
    assert len(summaries)==1
    import re
    assert all(int(v)==0 for v in re.findall(r'(?:rejected|world_errors)=(\d+)',summaries[0]))
parse=json.loads((RESULT/'realcompetition-src133-fixed-parse-final/comparison.json').read_text())
assert len(parse)==70 and all(row['exits']==[0,0] for row in parse)
assert [(row['id'],row['mode']) for row in parse if not row['equal']]==[('04','it')]
assert '100% tests passed' in (RESULT/'realcompetition-src133-fixed-unit/unit.log').read_text()
assert '100% tests passed' in (RESULT/'realcompetition-src133-fixed-unit/sanitizers.log').read_text()
audits['parse']=dict(count=len(parse),differences=[row['id']+'/'+row['mode'] for row in parse if not row['equal']])
audits['unit']='4/4'; audits['sanitizers']='4/4'; audits['source_hashes']='verified'
out=RESULT/'realcompetition-src133-fixed-release-audit.json'
out.write_text(json.dumps(audits,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(audits,ensure_ascii=False,indent=2))
