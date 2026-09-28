#!/usr/bin/env python3
"""Official paired runs, raw artifacts and full trace diffs. Python 3.6+."""
import argparse
import difflib
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import shutil
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src1.1.2 (x)' / 'tools'))
from baseline import run_case, digest
VERSIONS = ['src1.3.2', 'src1.3.3-fixed']

def trace(path):
    text = path.read_text(encoding='utf-8', errors='replace')
    text = re.sub(r'\x1b\[[0-9;]*m', '', text)
    return text

def extract(run):
    client, server = trace(run / 'client.log'), trace(run / 'server.log')
    actions = re.findall(r'^\s*\[([A-Za-z_]+(?:\s+[^|]*?)?)\|[^\]]*\]\s*$', server, re.M)
    observations = re.findall(r'^\s*(\[(?:Ask\w*|Sense)\b[^\]]*\])\s*$', server, re.M)
    parsed = client.split('Task:\n', 1)[-1].split('Parse Tasks, Cons and Infos finished.')[0]
    parsed = re.sub(r'^Validation:[^\n]*\n', '', parsed, flags=re.M).strip()
    decisions = []
    for line in client.splitlines():
        if any(x in line for x in ('[3A]', '[3B]', '[MultiGoto]', '[MultiPuton]', '[Defer]',
                                  '[Zero-Action]', '[FilterConstraintsByTaskConflicts]',
                                  '风险系数是', 'Task done')):
            line = re.sub(r',? elapsed=\d+ms', '', line)
            line = re.sub(r'(remaining|budget_remaining|budget_remaining_ms)=\d+(ms)?', r'\1=<clock>', line)
            # Baseline emitted a harmless zero-entry discard, removed by A.
            if 'Discarded goto_cons at location 0 due to 0 conflicts.' in line:
                continue
            line = re.sub(r' and task (\w+) at location -?\d+', r' and task \1', line)
            decisions.append(line)
    return dict(actions=[s.strip() for s in actions], observations=observations,
                parsed=parsed, decisions=decisions,
                preflight=re.findall(r'\[Preflight\] task[^\n]*', client),
                errors=[s for s in client.splitlines() if '[ERROR]' in s])

def build(version, sdk, out):
    source = ROOT / version
    out.mkdir(parents=True, exist_ok=False)
    executable = out / 'example'
    sources = sorted(source.glob('*.cpp'))
    command = ['g++', '-std=c++11', '-O2', '-g', '-Wall', '-Wextra',
               '-I'+str(source), '-I'+str(sdk/'include'), '-I'+str(sdk/'src')]
    command += [str(p) for p in sources]
    command += ['-L'+str(sdk/'lib'), '-lframe', '-lutility', '-lboost_thread',
                '-lboost_system', '-lboost_chrono', '-lboost_date_time', '-lboost_regex',
                '-lpthread', '-ldl', '-o', str(executable)]
    with (out/'build.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    (out/'build.json').write_text(json.dumps(dict(command=command,
        source_sha256={p.name:digest(p) for p in source.iterdir() if p.suffix in ('.cpp','.hpp','.txt')},
        sdk_sha256={str(p.relative_to(sdk)):digest(p) for p in
                    [sdk/'bin/cserver',sdk/'lib/libasp.so',sdk/'lib/libframe.a']},
        executable_sha256=digest(executable)), indent=2), encoding='utf-8')
    return executable

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, default=Path('/home/yifan/env-release-2026'))
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--seed', type=int)
    p.add_argument('--mode', choices=['it','nt'], default='it')
    p.add_argument('--case-dir', type=Path, default=ROOT/'题目/realcompetiton_2024')
    p.add_argument('--cases', type=int, nargs='+', default=[i for i in range(1,37) if i != 2])
    p.add_argument('--executables', type=Path, help='reuse a previous output build directory')
    args = p.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    with socket.socket() as probe: probe.bind(('127.0.0.1',7932))
    metadata = dict(seed=args.seed, excluded={'02':'Excluded by user: malformed XML'},
                    os=subprocess.check_output(['uname','-a']).decode(),
                    release=Path('/etc/os-release').read_text(),
                    compiler=subprocess.check_output(['g++','--version']).decode())
    (args.output/'environment.json').write_text(json.dumps(metadata,indent=2))
    if args.executables:
        args.executables = args.executables.resolve()
        executables = {v:args.executables/('build-'+v)/'example' for v in VERSIONS}
    else:
        executables = {v:build(v,args.sdk,args.output/('build-'+v)) for v in VERSIONS}
    if args.seed is not None:
        shim = args.output/'seed_rng.so'
        subprocess.run(['g++','-shared','-fPIC',str(Path(__file__).with_name('seed_rng.cpp')),
                        '-ldl','-o',str(shim)],check=True)
        # ld.so splits LD_PRELOAD at spaces, even if shell quoting was correct.
        preload_dir = Path(tempfile.mkdtemp(prefix='rdfw-seed-'))
        preload = preload_dir/'seed_rng.so'
        shutil.copy2(str(shim),str(preload))
        os.environ['LD_PRELOAD'] = str(preload)
        os.environ['RDFW_TEST_SEED'] = str(args.seed)
    rows=[]
    for i in args.cases:
        case_id='{:02d}'.format(i)
        case=args.case_dir.resolve()/(case_id+'.xml')
        # Use the SDK's original XML unchanged; its TinyXML accepts legacy
        # comments that Python's stricter ElementTree refuses (e.g. case 03).
        env=re.search(r'<env\s+([^>]+)>',case.read_text(encoding='utf-8')).group(1)
        flags=dict(re.findall(r'(mis|err|ans)="(on|off)"',env))
        stage=1 if all(flags.get(k)=='off' for k in ('mis','err','ans')) else 2
        pair=[]
        for v in VERSIONS:
            out=args.output/v/case_id
            result=run_case(args.sdk,ROOT/v,executables[v],case,stage,args.mode,out,5000,None)
            result.update(extract(out))
            result['evaluator_valid'] = result['final_goals'] is not None
            if not result['evaluator_valid']: result['status']='evaluator_failed'
            server_text=trace(out/'server.log')
            if args.seed is not None and ('cannot be preloaded' in server_text or
                    '[RDFW_TEST_SEED] '+str(args.seed) not in server_text):
                raise RuntimeError('Seed interposer not confirmed in server log')
            (out/'trace.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
            pair.append(result)
            print(v,case_id,'stage',stage,result['status'],result['official_score'],result['action_count'],flush=True)
        a,b=pair
        row=dict(id=case_id, stage=stage, status=[a['status'],b['status']],
                 scores=[a['official_score'],b['official_score']], raw_scores=[a['raw_score'],b['raw_score']],
                 action_counts=[a['action_count'],b['action_count']])
        for field in ('actions','observations','parsed','decisions'):
            row[field+'_equal']=a[field]==b[field]
            left=a[field].splitlines() if isinstance(a[field],str) else a[field]
            right=b[field].splitlines() if isinstance(b[field],str) else b[field]
            (args.output/(case_id+'-'+field+'.diff')).write_text(
                '\n'.join(difflib.unified_diff(left,right,fromfile=VERSIONS[0],tofile=VERSIONS[1],lineterm='')),
                encoding='utf-8')
        rows.append(row)
        (args.output/'comparison.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    print('COMPLETE',len(rows),'pairs',flush=True)

if __name__=='__main__': main()
