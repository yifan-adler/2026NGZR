#!/usr/bin/env python3
"""Same-seed official pairs, alternating run order, retained raw evidence."""
import argparse
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
from compare_legal import build, extract

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--versions', nargs='+', default=['src1.6.3', 'src1.6.4'])
    p.add_argument('--binaries', nargs='+', type=Path)
    p.add_argument('--repeats', type=int, default=3)
    p.add_argument('--cases', nargs='+', help='Target original cases, both IT and NT')
    p.add_argument('--full', action='store_true')
    p.add_argument('--quick', action='store_true', help='the 7 decision/combinatorial fixtures, IT/NT')
    p.add_argument('--group-mode', default='guarded', choices=['off', 'guarded'])
    p.add_argument('--diagnostics', choices=['on', 'off'], default='on')
    p.add_argument('--sdk', type=Path, default=Path('/home/yifan/env-release-2026'))
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    binaries = a.binaries or [build(v, a.sdk, a.output / ('build-' + Path(v).name)) for v in a.versions]
    assert len(binaries) == len(a.versions)
    shim = Path('/tmp/rdfw-164-seed.so')
    subprocess.run(['g++', '-shared', '-fPIC', str(Path(__file__).with_name('seed_rng.cpp')), '-ldl', '-o', str(shim)], check=True)
    os.environ.update(LD_PRELOAD=str(shim), RDFW_TEST_SEED='20260927', RDFW_TASK_GROUP_MODE=a.group_mode, RDFW_STAGE_TIMING='1' if a.diagnostics=='on' else '0')
    assets = a.output / 'assets'
    assets.mkdir()
    (assets / 'words.txt').write_bytes((ROOT / 'src1.6.4/words.txt').read_bytes().replace(b'\r\n', b'\n'))
    cases = [(ROOT / '题目/realcompetiton_2024' / (i + '.xml'), m) for i, m in [('06','it'),('06','nt'),('29','it')]]
    if a.cases:
        cases = [(ROOT / '题目/realcompetiton_2024' / (i + '.xml'), m)
                 for i in a.cases for m in ('it', 'nt')]
    if a.full or a.quick:
        files = []
        for d in ('decision','combinatorial_gain'):
            files += sorted((ROOT / 'src1.6.4/tests/fixtures' / d).glob('*.xml'))
        if not a.quick:
            files += sorted((ROOT / '题目/constraint_tradeoff_challenge_2026').glob('*.xml'))
            files += [ROOT / '题目/realcompetiton_2024' / (i+'.xml') for i in ('01','03','06','15','28','29')]
        cases = [(f, m) for f in files for m in ('it','nt')]
    rows = []
    for repeat in range(a.repeats):
        for case, mode in cases:
            flags = dict(re.findall(r'(mis|err|ans)="(on|off)"', re.search(r'<env\s+([^>]+)>', case.read_text()).group(1)))
            stage = 1 if all(flags.get(k)=='off' for k in ('mis','err','ans')) else 2
            pair = [None] * len(binaries)
            for idx in (list(range(len(binaries))) if repeat % 2 == 0 else list(reversed(range(len(binaries))))):
                out = a.output / Path(a.versions[idx]).name / (case.parent.name+'-'+case.stem+'-'+mode+'-r'+str(repeat+1))
                result = run_case(a.sdk, assets, binaries[idx], case, stage, mode, out, 5000, None)
                client = (out/'client.log').read_text(errors='replace')
                server = (out/'server.log').read_text(errors='replace')
                startup_attempt = None
                # A harness handshake timeout before Plan is not a planner
                # outcome. Retain its logs and retry once; never retry a run
                # that reached Plan, crashed there or hit its deadline.
                if result['external_timeout'] and '[PlannerVersion]' not in client:
                    startup_attempt = dict(output=str(out), result=result)
                    out = Path(str(out)+'-startup-retry')
                    result = run_case(a.sdk, assets, binaries[idx], case, stage, mode, out, 5000, None)
                    client = (out/'client.log').read_text(errors='replace')
                    server = (out/'server.log').read_text(errors='replace')
                assert '[RDFW_TEST_SEED] 20260927' in server
                goals, cons = result['final_goals'], result['credited_constraints']
                cost = sum(n*(4 if k=='move' else 1 if k=='sense' else 2) for k,n in result['actions'].items())
                final = re.findall(r'\[3A\]\[final\].*?base_score=(-?\d+), action_cost=(\d+)',client)
                pair[idx] = dict(version=a.versions[idx], status=result['status'], startup_attempt=startup_attempt, goals=goals, constraints=cons, cost=cost,
                    base=None if goals is None or cons is None else 40*goals+(20*cons if goals else 0)-cost,
                    official=result['raw_score'], timed_out=result['platform_timed_out'],
                    elapsed_ms=int(re.findall(r'elapsed=(\d+)ms',client)[-1]) if re.findall(r'elapsed=(\d+)ms',client) else None,
                    internal_base=int(final[-1][0]) if final else None, internal_cost=int(final[-1][1]) if final else None,
                    timing=re.findall(r'\[StageTiming\][^\n]*',client),
                    tradeoff=re.findall(r'\[TradeoffDecision\][^\n]*',client),
                    deadlines=re.findall(r'\[3B\]\[Deadline\][^\n]*',client), trace=extract(out), output=str(out))
            rows.append(dict(case=str(case), sha256=digest(case), stage=stage, mode=mode, repeat=repeat+1, pair=pair))
            (a.output/'results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2))
            print(case.stem,mode,repeat+1,[(r['version'],r['base']) for r in pair],flush=True)
    return int(any(r['status']!='ok' or r['base'] is None or r['timed_out'] or r['internal_cost']!=r['cost'] for row in rows for r in row['pair']))
if __name__ == '__main__':
    sys.exit(main())
