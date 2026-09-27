#!/usr/bin/env python3
"""Consolidate a complete challenge run and one verified retry, if needed."""

import argparse
import json
from pathlib import Path


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def write(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n',
                          encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True)
    parser.add_argument('--retry', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rows = read(args.run / 'results.json')
    metadata = read(args.run / 'metadata.json')
    expected = len(metadata['cases']) * len(metadata['modes'])
    assert len(rows) == expected
    retries = []
    if args.retry:
        retry_meta = read(args.retry / 'metadata.json')
        for key in ('old_executable_sha256', 'new_executable_sha256',
                    'sdk_cserver_sha256', 'seed', 'time_limit_ms'):
            assert metadata[key] == retry_meta[key], key
        retry_rows = read(args.retry / 'results.json')
        for retry in retry_rows:
            matches = [i for i, row in enumerate(rows)
                       if (row['id'], row['mode']) == (retry['id'], retry['mode'])]
            assert len(matches) == 1
            original = rows[matches[0]]
            assert original['verdict'] == 'invalid' and retry['verdict'] != 'invalid'
            assert original['control']['case_sha256'] == retry['control']['case_sha256']
            rows[matches[0]] = retry
            retries.append(dict(id=retry['id'], mode=retry['mode'],
                                original_status=original['new']['status'],
                                rerun_status=retry['new']['status']))
    assert len({(r['id'], r['mode']) for r in rows}) == expected
    summary = dict(case_count=len(metadata['cases']), runs=len(rows), retries=retries,
                   all_evaluator_valid=all(r['verdict'] != 'invalid' for r in rows),
                   max_theoretical_score=max(r['theoretical_max'] for r in rows),
                   max_observed_official_score=max(
                       r[version]['official'] for r in rows
                       for version in ('control', 'old', 'new')),
                   categories={})
    for category in ('positive', 'negative', 'normal'):
        selected = [r for r in rows if r['category'] == category]
        summary['categories'][category] = dict(
            verdicts={v: sum(r['verdict'] == v for r in selected)
                      for v in sorted({r['verdict'] for r in selected})},
            improved=sum(r['base_delta'] > 0 for r in selected),
            unchanged=sum(r['base_delta'] == 0 for r in selected),
            declined=sum(r['base_delta'] < 0 for r in selected),
            total_base_delta=sum(r['base_delta'] for r in selected),
            total_official_delta=sum(r['official_delta'] for r in selected))
    args.output.mkdir(parents=True, exist_ok=False)
    write(args.output / 'results.json', rows)
    write(args.output / 'metadata.json', metadata)
    write(args.output / 'summary.json', summary)
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
