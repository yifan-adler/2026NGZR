#!/usr/bin/env python3
"""Run a trade-off challenge against old, new and no-action clients."""

import argparse
import hashlib
import json
import os
import socket
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
CASES = ROOT / '题目' / 'constraint_tradeoff_challenge_2026'
sys.path.insert(0, str(ROOT / 'src1.1.2 (x)' / 'tools'))
sys.path.insert(0, str(ROOT / 'src1.6' / 'tests'))
from baseline import run_case
from compare_legal import extract


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def score(result):
    goals = result['final_goals']
    constraints = result['credited_constraints']
    if goals is None or constraints is None:
        return None
    cost = sum(n * (4 if action == 'move' else
                    1 if action in ('sense', 'askloc') else 2)
               for action, n in result['actions'].items())
    return 40 * goals + (20 * constraints if goals else 0) - cost


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--old', type=Path, required=True)
    parser.add_argument('--new', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture-dir', type=Path, default=CASES)
    parser.add_argument('--cases', nargs='+', help='Run selected IDs first')
    parser.add_argument('--modes', nargs='+', choices=('it', 'nt'), default=('it', 'nt'))
    args = parser.parse_args()
    fixture_dir = args.fixture_dir.resolve()
    rows = json.loads((fixture_dir / 'manifest.json').read_text(encoding='utf-8'))
    assert rows and len({row['id'] for row in rows}) == len(rows)
    if args.cases:
        rows = [row for row in rows if row['id'] in args.cases]
        if len(rows) != len(set(args.cases)):
            parser.error('unknown or repeated case ID')
    sdk, old, new, output = (p.resolve() for p in
                             (args.sdk, args.old, args.new, args.output))
    for path in (sdk / 'bin/cserver', sdk / 'bin/example', old, new):
        if not path.is_file():
            parser.error('missing file: ' + str(path))
    output.mkdir(parents=True, exist_ok=False)
    assets = output / 'client-assets'
    assets.mkdir()
    (assets / 'words.txt').write_bytes(
        (ROOT / 'src1.6' / 'words.txt').read_bytes().replace(b'\r\n', b'\n'))
    shim = output / 'seed_rng.so'
    subprocess.run(['g++', '-shared', '-fPIC',
                    str(ROOT / 'src1.6' / 'tests' / 'seed_rng.cpp'),
                    '-ldl', '-o', str(shim)], check=True)
    os.environ['LD_PRELOAD'] = str(shim)
    os.environ['RDFW_TEST_SEED'] = '20260924'
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 7932))
    metadata = dict(old_executable_sha256=sha256(old),
                    new_executable_sha256=sha256(new),
                    sdk_cserver_sha256=sha256(sdk / 'bin/cserver'),
                    seed=20260924, time_limit_ms=5000,
                    cases=[row['id'] for row in rows], modes=args.modes)
    (output / 'metadata.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    results = []
    for row in rows:
        case_path = fixture_dir / (row['id'] + '.xml')
        env = ET.parse(str(case_path)).getroot().find('env')
        flag = 'on' if row['stage'] == 2 else 'off'
        assert all(env.get(k) == flag for k in ('mis', 'err', 'ans'))
        assert row['theoretical_max'] <= 1000
        for mode in args.modes:
            run = {}
            for version, executable in [('control', sdk / 'bin/example'),
                                        ('old', old), ('new', new)]:
                run_dir = output / (row['id'] + '-' + mode + '-' + version)
                result = run_case(sdk, assets, executable, case_path, row['stage'], mode,
                                  run_dir, 5000, None)
                action_trace = extract(run_dir)['actions']
                run[version] = dict(status=result['status'],
                                    official=result['raw_score'],
                                    base=score(result),
                                    goals=result['final_goals'],
                                    constraints=result['credited_constraints'],
                                    actions=action_trace,
                                    action_counts=result['actions'],
                                    seconds=result['platform_seconds'],
                                    case_sha256=result['case_sha256'])
            control, before, after = (run[k] for k in ('control', 'old', 'new'))
            valid = (all(run[k]['status'] == 'ok' for k in run) and
                     all(run[k]['base'] is not None for k in run) and
                     all(run[k]['official'] is not None for k in run))
            if not valid:
                verdict = 'invalid'
            elif row['category'] == 'positive':
                verdict = ('pass' if after['base'] > control['base'] and
                           after['constraints'] < control['constraints'] else 'miss')
            elif row['category'] == 'negative':
                # Stage 2 may need one or more information actions even when it
                # correctly refuses the physical constraint-breaking task.
                # Independent, profitable goals may still be completed, so a
                # physical action is not by itself evidence of a bad trade.
                info_cost = sum(n for action, n in after['action_counts'].items()
                                if action in ('sense', 'askloc'))
                verdict = ('pass' if
                           after['constraints'] >= control['constraints'] and
                           after['base'] >= control['base'] - info_cost
                           else 'mistake')
            else:
                info_cost = sum(n for action, n in after['action_counts'].items()
                                if action in ('sense', 'askloc'))
                if after['base'] < before['base']:
                    verdict = 'regression'
                elif (after['base'] < control['base'] - info_cost or
                      after['constraints'] < control['constraints']):
                    verdict = 'mistake'
                else:
                    verdict = 'pass'
            result_row = dict(id=row['id'], category=row['category'],
                              family=row['family'], stage=row['stage'], mode=mode,
                              theoretical_max=row['theoretical_max'],
                              verdict=verdict, control=control, old=before, new=after,
                              base_delta=after['base'] - before['base'] if valid else None,
                              official_delta=after['official'] - before['official']
                              if valid else None)
            results.append(result_row)
            (output / 'results.json').write_text(
                json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
            print(row['id'], mode, verdict, 'base',
                  control['base'], before['base'], after['base'], flush=True)
    return int(any(row['verdict'] != 'pass' for row in results))


if __name__ == '__main__':
    raise SystemExit(main())
