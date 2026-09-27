#!/usr/bin/env python3
"""Build 1.6.1/1.6.2 and compare unchanged official cases in the same mode."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src1.1.2 (x)/tools'))
from baseline import run_case, digest
from compare_legal import build

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--reuse-builds', type=Path)
    p.add_argument('--group-mode', choices=('guarded', 'off'), default='guarded')
    p.add_argument('--modes', nargs='+', default=['it', 'nt'])
    p.add_argument('--quick', action='store_true')
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    versions = ['src1.6.1', 'src1.6.2']
    if a.reuse_builds:
        binaries = {v: a.reuse_builds / ('build-' + v) / 'example' for v in versions}
    else:
        binaries = {v: build(v, a.sdk, a.output / ('build-' + v)) for v in versions}
    shim = a.output / 'seed_rng.so'
    subprocess.run(['g++', '-shared', '-fPIC', str(Path(__file__).with_name('seed_rng.cpp')),
                    '-ldl', '-o', str(shim)], check=True)
    # LD_PRELOAD paths cannot contain spaces.
    preload = Path('/tmp/rdfw-162-validation-seed.so')
    preload.write_bytes(shim.read_bytes())
    os.environ['LD_PRELOAD'] = str(preload)
    os.environ['RDFW_TEST_SEED'] = '20260927'
    os.environ['RDFW_TASK_GROUP_MODE'] = a.group_mode
    assets = a.output / 'assets'
    assets.mkdir()
    (assets / 'words.txt').write_bytes((ROOT / 'src1.6.2/words.txt').read_bytes().replace(b'\r\n', b'\n'))
    cases = []
    for directory in ('decision', 'combinatorial_gain'):
        cases += list(sorted((ROOT / 'src1.6.2/tests/fixtures' / directory).glob('*.xml')))
    if not a.quick:
        cases += list(sorted((ROOT / '题目/constraint_tradeoff_challenge_2026').glob('*.xml')))
        cases += [ROOT / '题目/realcompetiton_2024' / (i + '.xml')
                  for i in ('01', '03', '06', '15', '28', '29')]
    rows = []
    for case in cases:
        text = case.read_text(encoding='utf-8')
        flags = dict(re.findall(r'(mis|err|ans)="(on|off)"', re.search(r'<env\s+([^>]+)>', text).group(1)))
        stage = 1 if all(flags.get(k) == 'off' for k in ('mis', 'err', 'ans')) else 2
        for mode in a.modes:
            pair = []
            for version in versions:
                output = a.output / version / (case.parent.name + '-' + case.stem + '-' + mode)
                result = run_case(a.sdk, assets, binaries[version], case, stage, mode, output, 5000, None)
                server = (output / 'server.log').read_text(encoding='utf-8', errors='replace')
                client = (output / 'client.log').read_text(encoding='utf-8', errors='replace')
                assert '[RDFW_TEST_SEED] 20260927' in server
                goals, cons = result['final_goals'], result['credited_constraints']
                cost = sum(n * (4 if name == 'move' else 1 if name == 'sense' else 2)
                           for name, n in result['actions'].items())
                base = None if goals is None or cons is None else 40 * goals + (20 * cons if goals else 0) - cost
                final = re.findall(r'\[3A\]\[final\].*?base_score=(-?\d+), action_cost=(\d+)', client)
                lower = int(final[-1][0]) if final else None
                internal_cost = int(final[-1][1]) if final else None
                pair.append(dict(version=version, status=result['status'], goals=goals,
                    constraints=cons, cost=cost, base=base, internal_base=lower,
                    internal_cost=internal_cost, official=result['raw_score'],
                    timed_out=result['platform_timed_out'], actions=result['actions'],
                    guarded=re.findall(r'\[GuardedDecision\][^\n]*', client),
                    output=str(output)))
            row = dict(case=str(case), sha256=digest(case), stage=stage, mode=mode,
                       group_mode=a.group_mode, baseline=pair[0], current=pair[1])
            rows.append(row)
            (a.output / 'results.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2))
            print(case.parent.name, case.stem, stage, mode,
                  pair[0]['base'], pair[1]['base'], 'internal', pair[1]['internal_base'], flush=True)
    summary = dict(pairs=len(rows), sdk_sha256={str(f.relative_to(a.sdk)): digest(f)
        for f in [a.sdk/'bin/cserver', a.sdk/'lib/libasp.so', a.sdk/'res/fortask.lp', a.sdk/'res/forcons.lp']},
        invalid=[r['case'] + ':' + r['mode'] for r in rows if
                 any(x['status'] != 'ok' or x['base'] is None for x in (r['baseline'],r['current']))],
        losses=[dict(case=r['case'], mode=r['mode'], old=r['baseline']['base'], new=r['current']['base'])
                for r in rows if r['baseline']['base'] is not None and r['current']['base'] is not None
                and r['current']['base'] < r['baseline']['base']],
        cost_mismatches=[r['case']+':'+r['mode'] for r in rows
                         if r['current']['internal_cost'] != r['current']['cost']],
        base_differences=[dict(case=r['case'], mode=r['mode'], stage=r['stage'],
            official_base=r['current']['base'], internal=r['current']['internal_base'])
            for r in rows if r['current']['base'] != r['current']['internal_base']])
    (a.output/'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2))
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return int(bool(summary['invalid'] or summary['cost_mismatches']))

if __name__ == '__main__':
    raise SystemExit(main())
