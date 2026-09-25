#!/usr/bin/env python3
"""Run liuyifan1.0 on the official SDK and audit decision feedback records."""
import json
import re
import socket
import sys
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src1.5'
SUITE = ROOT / '题目' / 'liuyifan1.0'
SDK = Path('/home/yifan/env-release-2026')
sys.path.insert(0, str(ROOT / 'src1.1.2 (x)' / 'tools'))
sys.path.insert(0, str(ROOT / 'src1.5' / 'tests'))
from baseline import run_case
from compare_legal import build, trace


def events(path):
    return [(a, b.strip(), v.strip()) for a, b, v in re.findall(
        r'^[ \t]*\[([A-Za-z_]+)([^\r\n|\]]*)\|([^\r\n\]]*)\][ \t]*$',
        trace(path), re.M)]


def feedback(path):
    records = []
    for line in trace(path).splitlines():
        marker = '[DecisionFeedback] '
        if marker in line:
            records.append(json.loads(line.split(marker, 1)[1]))
    return records


def main():
    output = Path(sys.argv[1]).resolve()
    output.mkdir(parents=True, exist_ok=False)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 7932))
    executable = build('src1.5', SDK, output / 'build')
    baseline = ROOT / 'test-results' / 'liuyifan1.0-src1.4.1-verified-20260924'
    rows = []
    for case in range(1, 7):
        number = f'{case:02d}'
        for mode in ('it', 'nt'):
            key = f'{number}-{mode}'
            run_dir = output / key
            result = run_case(SDK, SOURCE, executable, SUITE / f'{number}.xml',
                              2 if case in (3, 4) else 1, mode, run_dir, 5000, None)
            decision = feedback(run_dir / 'client.log')
            selected = {r['candidate_id']: r for r in decision if r['event'] == 'candidate_selected'}
            actual = {r['candidate_id']: r for r in decision if r['event'] == 'candidate_result'}
            assert selected.keys() == actual.keys(), key
            assert selected, key
            for candidate_id, record in actual.items():
                assert record['prediction'] == selected[candidate_id]['prediction'], key
                p, a, error = record['prediction'], record['actual'], record['error']
                for field in ('goal_gain', 'constraint_gain', 'constraint_loss', 'action_cost', 'utility'):
                    assert error[field] == a[field] - p[field], (key, candidate_id, field)
                    assert error[field] == 0, (key, candidate_id, field, error[field])
            considered = [r for r in decision if r['event'] == 'candidate_considered']
            if case in (1, 2):
                assert all(r['prediction']['action_cost'] == r['actual']['action_cost']
                           for r in actual.values()), key
            if case == 3:
                assert any(any(f['source'] == 'sense' for f in s['evidence'])
                           for s in selected.values()), key
            if case == 4:
                assert list(selected) == sorted(selected), key
                assert all(s['prediction'] == actual[sid]['prediction']
                           for sid, s in selected.items()), key
            if case == 5:
                assert any(s['prediction']['constraint_loss'] == 1
                           for s in selected.values()), key
                assert all(s['prediction']['maintained_constraints'] == 0
                           for s in list(selected.values())[1:]), key
            if case == 6:
                give = [r for r in considered if r['task_label'] == 'give']
                assert give and all(not r['eligible'] and r['prediction']['utility'] < 0
                                    for r in give), key
            current_events = events(run_dir / 'server.log')
            old_events = events(baseline / key / 'server.log')
            assert current_events == old_events, key
            assert result['status'] == 'ok', (key, result['status'])
            baseline_summary = json.loads((baseline / key / 'summary.json').read_text())
            assert result['final_goals'] == baseline_summary['final_goals'], key
            assert result['credited_constraints'] == baseline_summary['credited_constraints'], key
            row = dict(case=number, mode=mode, status=result['status'],
                       official_score=result['raw_score'], goals=result['final_goals'],
                       constraints=result['credited_constraints'],
                       action_count=len(current_events),
                       action_cost=sum(4 if e[0] == 'Move' else 1 if e[0] == 'Sense' else 2
                                       for e in current_events),
                       candidate_count=len(selected),
                       prediction_errors=[r['error'] for r in actual.values()],
                       considered_count=len(considered),
                       events_equal_baseline=True)
            rows.append(row)
            (output / 'results.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
            print(key, 'ok', row['goals'], row['constraints'], row['action_count'],
                  'candidates', row['candidate_count'], flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
