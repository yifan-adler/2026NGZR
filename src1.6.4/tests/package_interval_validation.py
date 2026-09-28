#!/usr/bin/env python3
"""Audit final build hashes, summarize behavior/performance and retain evidence."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import shutil
import statistics
import zipfile

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('full','off','target','diagnostics_off','reproduce','profile','output'):
        p.add_argument('--'+name.replace('_','-'),type=Path,required=True)
    p.add_argument('--diagnostic-source',type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parents[2]
    a.output.mkdir(parents=True,exist_ok=False)
    for v in ('src1.6.2','src1.6.3'):
        meta=read(a.full/('build-'+v)/'build.json')
        assert all(sha(root/v/n)==value for n,value in meta['source_sha256'].items()),v
        shutil.copy2(str(a.full/('build-'+v)/'build.json'),str(a.output/(v+'-build.json')))
    original_build=read(Path('/tmp/rdfw-162-final-guarded/build-src1.6.2/build.json'))
    current_baseline=read(a.full/'build-src1.6.2/build.json')
    assert original_build['source_sha256']==current_baseline['source_sha256']
    assert original_build['sdk_sha256']==current_baseline['sdk_sha256']
    full=read(a.full/'results.json');off=read(a.off/'results.json');target=read(a.target/'results.json')
    assert len(full)==86 and len(off)==14 and len(target)==15
    groups=dict(full=full,off=off,target=target,diagnostics_off=read(a.diagnostics_off/'results.json'),reproduce=read(a.reproduce/'results.json'),profile=read(a.profile/'results.json'))
    summary={}
    for label,rows in groups.items():
        valid=[x for r in rows for x in r['pair'] if x['status']=='ok' and x['base'] is not None]
        invalid=[dict(case=r['case'],mode=r['mode'],repeat=r['repeat'],version=x['version'],output=x['output']) for r in rows for x in r['pair'] if x['status']!='ok' or x['base'] is None or x['timed_out']]
        pairs=[r for r in rows if len(r['pair'])==2 and all(x['base'] is not None for x in r['pair'])]
        summary[label]=dict(rows=len(rows),runs=sum(len(r['pair']) for r in rows),invalid=invalid,
            cost_mismatches=[x['output'] for x in valid if x['cost']!=x['internal_cost']],
            gains=sum(r['pair'][1]['base']>r['pair'][0]['base'] for r in pairs),
            equal=sum(r['pair'][1]['base']==r['pair'][0]['base'] for r in pairs),
            losses=[dict(case=r['case'],mode=r['mode'],repeat=r['repeat'],old=r['pair'][0]['base'],new=r['pair'][1]['base']) for r in pairs if r['pair'][1]['base']<r['pair'][0]['base']],
            action_equal=sum(r['pair'][0]['trace']['actions']==r['pair'][1]['trace']['actions'] for r in pairs),
            internal_base_differences=[dict(case=r['case'],mode=r['mode'],stage=r['stage'],version=x['version'],official_base=x['base'],lower=x['internal_base']) for r in rows for x in r['pair'] if x['base']!=x['internal_base']],
            lower_bound_exceeds_official=[x['output'] for r in rows for x in r['pair'] if x['base'] is not None and x['internal_base'] is not None and x['internal_base']>x['base']],
            startup_retries=[x['startup_attempt']['output'] for r in rows for x in r['pair'] if x.get('startup_attempt')])
    assert all(not summary[k]['invalid'] and not summary[k]['cost_mismatches'] for k in ('full','off','target','diagnostics_off'))
    lines=['# src1.6.3 官方配对及性能证据','',
        'WSL2 Ubuntu-18.04 / g++ 7.5.0 / 原官方 SDK。固定外部种子 20260927，原题未修改；平台上限 5000 ms。基础分不含时间奖励，耗时为真实墙钟。', '',
        '|题组 / 题|模式|Stage|1.6.2 基础分|1.6.3 基础分|1.6.3 确定下界|动作相同|','|---|---|---:|---:|---:|---:|---|']
    behavior=[]
    for r in full:
        f=Path(r['case']);old,new=r['pair']
        same=old['trace']['actions']==new['trace']['actions']
        lines.append('|{} / {}|{}|{}|{}|{}|{}|{}|'.format(f.parent.name,f.stem,r['mode'],r['stage'],old['base'],new['base'],new['internal_base'],'是' if same else '否'))
        if not same:
            diff=list(difflib.unified_diff(old['trace']['actions'],new['trace']['actions'],fromfile='1.6.2',tofile='1.6.3',lineterm=''))
            behavior.append(dict(case=str(f),mode=r['mode'],old_goals=old['goals'],new_goals=new['goals'],old_constraints=old['constraints'],new_constraints=new['constraints'],old_cost=old['cost'],new_cost=new['cost'],observations_equal=old['trace']['observations']==new['trace']['observations'],action_diff=diff))
    lines+=['','## 06 / 29 重复回归','','|题|模式|轮|1.6.2 基础分|1.6.3 基础分|1.6.3 目标/约束/成本|','|---|---|---:|---:|---:|---|']
    performance={}
    for r in target:
        old,new=r['pair'];lines.append('|{}|{}|{}|{}|{}|{}/{}/{}|'.format(Path(r['case']).stem,r['mode'],r['repeat'],old['base'],new['base'],new['goals'],new['constraints'],new['cost']))
    for idx,label in enumerate(('1.6.2 timing snapshot','1.6.3')):
        selected=[r['pair'][idx] for r in target if Path(r['case']).stem=='29']
        phases={}
        for x in selected:
            for line in x['timing']:
                match=re.search(r'phase=(\w+) calls=(\d+) us=(\d+)',line)
                if match:
                    phase,calls,us=match.groups();phases.setdefault(phase,[]).append(dict(calls=int(calls),us=int(us)))
        performance[label]={k:dict(median_us=statistics.median(x['us'] for x in values),range_us=[min(x['us'] for x in values),max(x['us'] for x in values)],calls=[x['calls'] for x in values]) for k,values in phases.items()}
    elapsed={}
    for label,rows in [('timing_on',target),('timing_off',groups['diagnostics_off'])]:
        values=[r['pair'][-1].get('elapsed_ms') for r in rows if Path(r['case']).stem=='29']
        values=[v for v in values if v is not None]
        if values: elapsed[label]=dict(median_ms=statistics.median(values),range_ms=[min(values),max(values)],values_ms=values)
    performance['elapsed']=elapsed
    lines+=['','## 29 阶段耗时（5 轮中位数）','','汇总阶段耗时含嵌套调用，不能相加；计时本身有额外开销。platform 为同步 SDK 调用墙钟，tail 包含它。','', '|阶段|1.6.2 us|1.6.3 us|','|---|---:|---:|']
    for phase in performance.get('1.6.3',{}):
        lines.append('|{}|{}|{}|'.format(phase,performance['1.6.2 timing snapshot'].get(phase,{}).get('median_us'),performance['1.6.3'][phase]['median_us']))
    lines+=['','详细行为差异：behavior-differences.json。完整统计：summary.json。原始日志、ASP 终态、构建命令和输入哈希见各 evidence.zip。', '',
        'reproduce 是最初 1.6.1/1.6.2 的复现；profile 是统一 5000 ms 前的开发诊断对照，包含未交付原型的退化结果，不计入最终验收。最终验收仅使用 full、off、target、diagnostics_off。']
    (a.output/'REPORT.md').write_text('\n'.join(lines)+'\n')
    (a.output/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2))
    (a.output/'performance.json').write_text(json.dumps(performance,indent=2))
    (a.output/'behavior-differences.json').write_text(json.dumps(behavior,ensure_ascii=False,indent=2))
    diagnostic_meta=read(a.profile/'build-rdfw-163-diagnostic-162/build.json')
    assert all(sha(a.diagnostic_source/n)==value for n,value in diagnostic_meta['source_sha256'].items())
    with zipfile.ZipFile(str(a.output/'baseline-timing-source.zip'),'w',zipfile.ZIP_DEFLATED) as z:
        for f in sorted(a.diagnostic_source.iterdir()):
            if f.is_file(): z.write(str(f),f.name)
    for label,source in [('full',a.full),('off',a.off),('target',a.target),('diagnostics-off',a.diagnostics_off),('reproduce',a.reproduce),('profile',a.profile)]:
        shutil.copy2(str(source/'results.json'),str(a.output/(label+'-results.json')))
        with zipfile.ZipFile(str(a.output/(label+'-evidence.zip')),'w',zipfile.ZIP_DEFLATED) as z:
            for f in sorted(source.rglob('*')):
                if f.is_file() and f.suffix in ('.json','.xml','.log','.txt','.lp','.sh','.cpp','.hpp'):
                    z.write(str(f),str(f.relative_to(source)))
    for label,folder in [('unit','/tmp/rdfw-163-unit'),('asan','/tmp/rdfw-163-asan')]:
        shutil.copy2(folder+'/Testing/Temporary/LastTest.log',str(a.output/(label+'-LastTest.log')))
        shutil.copy2(folder+'/CMakeCache.txt',str(a.output/(label+'-CMakeCache.txt')))
    for name in ('ctest','asan-ctest','baseline-ctest','baseline-asan-ctest'):
        shutil.copy2('/tmp/rdfw-163-'+name+'.log',str(a.output/(name+'.log')))
    diff=[]
    for f in sorted((root/'src1.6.3').iterdir()):
        if f.is_file() and f.suffix in ('.cpp','.hpp'):
            old=root/'src1.6.2'/f.name
            diff+=list(difflib.unified_diff(old.read_text().splitlines() if old.exists() else [],f.read_text().splitlines(),fromfile='src1.6.2/'+f.name,tofile='src1.6.3/'+f.name,lineterm=''))
    (a.output/'source.diff').write_text('\n'.join(diff)+'\n')
    (a.output/'manifest.json').write_text(json.dumps(dict(source_hashes_verified=True,baseline_162_unchanged=True,sdk_unchanged=True,source_files={str(f.relative_to(root/'src1.6.3')):sha(f) for f in (root/'src1.6.3').rglob('*') if f.is_file() and 'test-results' not in f.parts},artifacts={f.name:sha(f) for f in a.output.iterdir() if f.is_file()}),indent=2))
    print(json.dumps(summary,ensure_ascii=False,indent=2));print(json.dumps(performance,indent=2))
if __name__=='__main__':main()
