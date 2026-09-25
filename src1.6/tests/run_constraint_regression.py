#!/usr/bin/env python3
"""Compare the constraint planner with the recorded 2026 competition cases."""

import argparse
import json
import os
import socket
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src1.1.2 (x)' / 'tools'))
from baseline import digest, run_case


def action_cost(actions):
    return sum(count * (4 if name == 'move' else
                        1 if name in ('sense', 'askloc') else 2)
               for name, count in actions.items())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, default=ROOT / 'test-results' /
                        'realcompetition-src1.5-all36-seed20260924' / 'results.json')
    parser.add_argument('--cases', nargs='+', help='Only run these two-digit case IDs')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    assets = output / 'client-assets'
    assets.mkdir()
    (assets / 'words.txt').write_bytes(
        (ROOT / 'src1.6' / 'words.txt').read_bytes().replace(b'\r\n', b'\n'))
    cases = assets / 'cases'
    cases.mkdir()
    shim = output / 'seed_rng.so'
    subprocess.run(['g++', '-shared', '-fPIC',
                    str(ROOT / 'src1.6' / 'tests' / 'seed_rng.cpp'),
                    '-ldl', '-o', str(shim)], check=True)
    os.environ['LD_PRELOAD'] = str(shim)
    os.environ['RDFW_TEST_SEED'] = '20260924'
    baseline = json.loads(args.baseline.read_text(encoding='utf-8'))
    rows = []
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 7932))
    for old in baseline:
        if not old.get('evaluator_valid') or old.get('status') != 'ok':
            continue
        name = old['id']
        if args.cases is not None and name not in args.cases:
            continue
        checkout_case = ROOT / '题目' / 'realcompetiton_2024' / (name + '.xml')
        case = cases / (name + '.xml')
        contents = checkout_case.read_bytes()
        case.write_bytes(contents)
        if digest(case) != old['case_sha256']:
            case.write_bytes(contents.replace(b'\r\n', b'\n'))
        if digest(case) != old['case_sha256']:
            raise ValueError('case changed: ' + name)
        result = run_case(args.sdk.resolve(), assets, args.executable.resolve(),
                          case, old['stage'], old['mode'], output / name,
                          5000, None)
        old_base = old['base_reward'] - old['action_cost']
        new_base = (40 * result['final_goals'] +
                    20 * result['credited_constraints'] -
                    action_cost(result['actions'])
                    if result['final_goals'] is not None and
                    result['credited_constraints'] is not None else None)
        row = dict(case=name, status=result['status'], old_base=old_base,
                   new_base=new_base,
                   base_delta=new_base - old_base if new_base is not None else None,
                   old_goals=old['final_goals'],
                   new_goals=result['final_goals'],
                   old_constraints=old['credited_constraints'],
                   new_constraints=result['credited_constraints'],
                   old_official=old['raw_score'],
                   new_official=result['raw_score'])
        rows.append(row)
        (output / 'results.json').write_text(
            json.dumps(rows, indent=2), encoding='utf-8')
        print(name, row['status'], 'base_delta', row['base_delta'], flush=True)
    return int(any(row['status'] != 'ok' or row['base_delta'] is None or
                   row['base_delta'] < 0 for row in rows))


if __name__ == '__main__':
    raise SystemExit(main())
