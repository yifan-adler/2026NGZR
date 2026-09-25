#!/usr/bin/env python3
"""Run constraint trade-off fixtures against the 2026 evaluation SDK.

The SDK binaries can be rebuilt from the supplied source for the local Linux ABI.
"""

import argparse
import json
import re
import socket
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src1.6'
FIXTURES = ROOT / '题目' / 'constraint_tradeoff_2026'
sys.path.insert(0, str(ROOT / 'src1.1.2 (x)' / 'tools'))
sys.path.insert(0, str(SOURCE / 'tests'))
from baseline import run_case
from compare_legal import build, trace


CASES = (
    ('01-one-goal-loss', 1, 2, 1, False),
    ('02-two-goal-profit', 1, 4, 0, True),
    ('03-four-goal-reordered', 1, 6, 0, True),
    ('04-stage2-two-goal', 2, 4, 0, True),
    ('05-near-shared-goals', 1, 5, 0, True),
    ('06-inside-shared-goals', 1, 4, 0, True),
    ('07-existing-four-goal', 1, 6, 0, True),
)


def feedback(log_path):
    result = []
    for line in trace(log_path).splitlines():
        marker = '[DecisionFeedback] '
        if marker in line:
            result.append(json.loads(line.split(marker, 1)[1]))
    return result


def actions(log_path):
    return re.findall(r'^\s*\[([A-Za-z_]+(?:\s+[^|]*?)?)\|[^\]]*\]\s*$',
                      trace(log_path), re.M)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--executable', type=Path,
                        help='Reuse an executable already built from src1.6')
    parser.add_argument('--cases', nargs='+',
                        help='Run only these fixture IDs, such as 01-one-goal-loss')
    parser.add_argument('--modes', nargs='+', choices=('it', 'nt'),
                        help='Run only the selected language modes')
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    output = args.output.resolve()
    for required in ('bin/cserver', 'include/cserver/plug.hpp', 'lib/libframe.a'):
        if not (sdk / required).is_file():
            parser.error('SDK missing ' + required)
    if not (sdk / 'bin/example').is_file():
        parser.error('SDK missing no-action control client bin/example')
    output.mkdir(parents=True, exist_ok=False)
    assets = output / 'client-assets'
    assets.mkdir()
    words = (SOURCE / 'words.txt').read_bytes().replace(b'\r\n', b'\n')
    (assets / 'words.txt').write_bytes(words)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 7932))
    executable = (args.executable.resolve() if args.executable else
                  build('src1.6', sdk, output / 'build'))
    if not executable.is_file():
        parser.error('executable does not exist: ' + str(executable))
    selected_cases = [case for case in CASES if args.cases is None or
                      case[0] in args.cases]
    if args.cases is not None and len(selected_cases) != len(set(args.cases)):
        parser.error('unknown or repeated case ID')
    modes = args.modes or ('it', 'nt')
    rows = []
    for name, stage, expected_goals, expected_constraints, expect_trade in selected_cases:
        case = (ROOT / '题目' / 'liuyifan1.0' / '05.xml'
                if name == '07-existing-four-goal' else FIXTURES / (name + '.xml'))
        env = ET.parse(str(case)).getroot().find('env')
        expected_flag = 'on' if stage == 2 else 'off'
        assert env is not None and all(env.get(k) == expected_flag
                                       for k in ('mis', 'err', 'ans')), name
        for mode in modes:
            key = name + '-' + mode
            result = run_case(sdk, assets, executable, case, stage, mode,
                              output / key, 5000, None)
            control = run_case(sdk, assets, sdk / 'bin/example', case, stage, mode,
                               output / (key + '-no-action'), 5000, None)
            records = feedback(output / key / 'client.log')
            considered = [r for r in records if r['event'] == 'candidate_considered']
            selected = [r for r in records if r['event'] == 'candidate_selected']
            actual = [r for r in records if r['event'] == 'candidate_result']
            server_actions = actions(output / key / 'server.log')
            action_cost = sum(4 if action.startswith('Move ') else
                              1 if action.startswith(('Sense', 'AskLoc')) else 2
                              for action in server_actions)
            goals = result['final_goals']
            constraints = result['credited_constraints']
            base_score = (40 * goals + (20 * constraints if goals else 0) - action_cost
                          if goals is not None and constraints is not None else None)
            control_goals = control['final_goals']
            control_constraints = control['credited_constraints']
            control_base = (40 * control_goals +
                            (20 * control_constraints if control_goals else 0)
                            if control_goals is not None and
                            control_constraints is not None else None)
            base_delta = (base_score - control_base if base_score is not None and
                          control_base is not None else None)
            meets_outcome = (result['status'] == 'ok' and control['status'] == 'ok' and
                             goals == expected_goals and
                             constraints == expected_constraints and
                             bool(server_actions) == expect_trade and
                             base_delta is not None and
                             ((base_delta > 0 and
                               result['raw_score'] > control['raw_score'])
                              if expect_trade else (base_delta == 0)))
            row = dict(case=name, stage=stage, mode=mode, status=result['status'],
                       control_status=control['status'],
                       case_sha256=result['case_sha256'], official_score=result['raw_score'],
                       goals=goals, constraints=constraints,
                       actions=server_actions, action_cost=action_cost,
                       base_score=base_score, control_goals=control_goals,
                       control_constraints=control_constraints,
                       control_base_score=control_base, base_delta=base_delta,
                       official_delta=(result['raw_score'] - control['raw_score']
                                       if result['raw_score'] is not None and
                                       control['raw_score'] is not None else None),
                       expected_goals=expected_goals,
                       expected_constraints=expected_constraints,
                       expected_trade=expect_trade, meets_outcome=meets_outcome,
                       selected_count=len(selected), result_count=len(actual),
                       first_selected=selected[0] if selected else None,
                       first_considered=considered[0] if considered else None,
                       nonzero_errors=[r['error'] for r in actual
                                       if any(r['error'].values())])
            rows.append(row)
            (output / 'results.json').write_text(json.dumps(
                rows, ensure_ascii=False, indent=2), encoding='utf-8')
            print(key, result['status'], 'goals', goals, 'constraints', constraints,
                  'base', base_score, 'outcome', meets_outcome, flush=True)
    return int(any(row['status'] != 'ok' or row['control_status'] != 'ok' or
                   not row['meets_outcome']
                   for row in rows))


if __name__ == '__main__':
    raise SystemExit(main())
