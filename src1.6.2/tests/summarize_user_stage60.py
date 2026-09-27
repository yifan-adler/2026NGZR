#!/usr/bin/env python3
"""Summarize paired original-1.6 versus current shadow runs on 60 user cases."""

import argparse
import csv
import hashlib
import json
import re
import statistics
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


def actions(path):
    text = path.read_text(encoding='utf-8', errors='replace')
    return re.findall(r'^\s*\[([^|\]]+)\|', text, re.M)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True)
    parser.add_argument('--previous', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--replace', action='store_true',
                        help='Regenerate files in an output directory created by this script')
    args = parser.parse_args()
    run = args.run.resolve()
    rows = json.loads((run / 'results.json').read_text(encoding='utf-8'))
    metadata = json.loads((run / 'metadata.json').read_text(encoding='utf-8'))
    previous_rows = json.loads(args.previous.read_text(encoding='utf-8'))
    previous = {(r['stage'], r['id'], r['mode']): r for r in previous_rows}
    if len(rows) != 120 or len(previous) != 120:
        raise ValueError('Expected 120 Stage 1/2 IT/NT pairs')
    records = []
    for row in rows:
        key = (row['stage'], row['id'], row['mode'])
        if key not in previous:
            raise ValueError('Missing previous result: {}'.format(key))
        if row['case_sha256'] != previous[key]['case_sha256']:
            raise ValueError('Input hash changed: {}'.format(key))
        prefix = 'stage{}-{}-{}-'.format(*key)
        original_actions = actions(run / (prefix + 'v15') / 'server.log')
        current_actions = actions(run / (prefix + 'v16') / 'server.log')
        client = (run / (prefix + 'v16') / 'client.log').read_text(
            encoding='utf-8', errors='replace')
        search_logs = re.findall(r'\[TaskGroupShadow\] ([^\n]+)', client)
        search_total_ms = 0
        if search_logs:
            match = re.search(r'total_ms=(\d+)', search_logs[-1])
            if match:
                search_total_ms = int(match.group(1))
        records.append({
            'stage': row['stage'], 'case': row['id'], 'mode': row['mode'],
            'valid': row['valid'], 'case_sha256': row['case_sha256'],
            'original_base': row['v15'].get('base'),
            'current_base': row['v16'].get('base'),
            'base_delta': row['base_delta'],
            'v15_base_from_previous_report': previous[key]['v15']['base'],
            'v16_base_from_previous_report': previous[key]['v16']['base'],
            'original_official': row['v15'].get('official'),
            'current_official': row['v16'].get('official'),
            'official_delta': row['official_delta'],
            'original_goals': row['v15'].get('goals'),
            'current_goals': row['v16'].get('goals'),
            'original_constraints': row['v15'].get('constraints'),
            'current_constraints': row['v16'].get('constraints'),
            'same_actions': original_actions == current_actions,
            'original_actions': original_actions,
            'current_actions': current_actions,
            'shadow_search_calls': len(search_logs),
            'shadow_search_total_ms': search_total_ms,
            'original_seconds': row['v15'].get('seconds'),
            'current_seconds': row['v16'].get('seconds'),
        })
    if not all(record['valid'] for record in records):
        raise ValueError('At least one official pair failed; inspect raw results')

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=args.replace)
    (output / 'results.json').write_text(json.dumps(records, ensure_ascii=False, indent=2) + '\n',
                                         encoding='utf-8')
    fields = [key for key in records[0] if key not in ('original_actions', 'current_actions')]
    with (output / 'results.csv').open('w', newline='', encoding='utf-8-sig') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows({key: record[key] for key in fields} for record in records)

    metadata['mode_for_current'] = 'shadow'
    metadata['original_version'] = 'src1.6 commit 6a0fdc6'
    previous_metadata_path = args.previous.with_suffix('.metadata.json')
    if previous_metadata_path.exists():
        previous_metadata = json.loads(previous_metadata_path.read_text(encoding='utf-8'))
        metadata['input_archives_sha256'] = previous_metadata.get('input_archives_sha256')
    metadata['current_source_sha256'] = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(args.source.iterdir())
        if path.is_file() and path.suffix in ('.cpp', '.hpp', '.txt')}
    (output / 'metadata.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + '\n',
                                          encoding='utf-8')
    with ZipFile(str(output / 'official-logs.zip'), 'w', ZIP_DEFLATED) as archive:
        for directory in sorted(run.glob('stage*-*-*-*')):
            if not directory.is_dir():
                continue
            for name in ('server.log', 'client.log', 'summary.json'):
                path = directory / name
                if path.is_file():
                    archive.write(str(path), '{}/{}'.format(directory.name, name))

    lines = [
        '# 用户 60 题：原始 1.6 与当前 shadow 搜索对照', '',
        '使用与上次配对相同的 60 道 XML、SDK、词典、随机种子 `20260924` 和 5000 ms 时限。'
        '每题分别运行 IT／NT，原始 1.6 与当前代码交替先后执行。当前代码设置 '
        '`RDFW_TASK_GROUP_MODE=shadow`，搜索仅旁路记录，真实动作仍由 1.6 策略选择。', '',
        '| 阶段 | 输入 | 有效配对 | 原始 1.6 基础分 | 当前基础分 | 提高/持平/下降 | '
        '原始官方分 | 当前官方分 | 动作一致 |',
        '| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |',
    ]
    for stage in (1, 2):
        for mode in ('it', 'nt'):
            group = [r for r in records if r['stage'] == stage and r['mode'] == mode]
            lines.append('| Stage {} | {} | {} | {} | {} | {}/{}/{} | {} | {} | {}/{} |'.format(
                stage, mode.upper(), len(group),
                sum(r['original_base'] for r in group),
                sum(r['current_base'] for r in group),
                sum(r['base_delta'] > 0 for r in group),
                sum(r['base_delta'] == 0 for r in group),
                sum(r['base_delta'] < 0 for r in group),
                sum(r['original_official'] for r in group),
                sum(r['current_official'] for r in group),
                sum(r['same_actions'] for r in group), len(group)))
    improved = [r for r in records if r['base_delta'] > 0]
    regressed = [r for r in records if r['base_delta'] < 0]
    changed_actions = [r for r in records if not r['same_actions']]
    calls = sum(r['shadow_search_calls'] for r in records)
    search_times = sorted(r['shadow_search_total_ms'] for r in records)
    official_delta_total = sum(r['official_delta'] for r in records)
    versus_15 = {(stage, mode): sum(
        r['current_base'] - r['v15_base_from_previous_report']
        for r in records if r['stage'] == stage and r['mode'] == mode)
        for stage in (1, 2) for mode in ('it', 'nt')}
    lines += ['',
        '基础分提高 {} 组、持平 {} 组、下降 {} 组；真实动作不同 {} 组。'
        '旁路搜索共记录 {} 次调用。'.format(
            len(improved), len(records) - len(improved) - len(regressed),
            len(regressed), len(changed_actions), calls),
        '官方总分合计变化 {:+d}；逐题提高 {} 组、持平 {} 组、下降 {} 组。'
        '旁路搜索累计耗时每题 P50 {} ms、P95 {} ms、最大 {} ms；无执行超时。'.format(
            official_delta_total,
            sum(r['official_delta'] > 0 for r in records),
            sum(r['official_delta'] == 0 for r in records),
            sum(r['official_delta'] < 0 for r in records),
            statistics.median(search_times), search_times[int(.95 * (len(search_times) - 1))],
            search_times[-1]),
        '官方总分包含时间奖励，单次运行的差值不能单独证明策略收益；'
        '以基础分、终态和动作序列判断本批题的策略变化。', '',
        '与上次报告中的 1.5 基础分相比，本次当前版合计差值：'
        'Stage 1 IT {:+d}、NT {:+d}；Stage 2 IT {:+d}、NT {:+d}。'.format(
            versus_15[(1, 'it')], versus_15[(1, 'nt')],
            versus_15[(2, 'it')], versus_15[(2, 'nt')]), '',
        '逐题数据：[CSV](results.csv)、[JSON](results.json)、'
        '[二进制与输入哈希](metadata.json)、[官方原始日志](official-logs.zip)。',
    ]
    (output / 'REPORT.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('\n'.join(lines[-7:]))


if __name__ == '__main__':
    main()
