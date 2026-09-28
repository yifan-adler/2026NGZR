#!/usr/bin/env python3
"""Package text-only official evidence and verify the tested source hashes."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--guarded', type=Path, required=True)
    p.add_argument('--off', type=Path, required=True)
    p.add_argument('--unit', type=Path, required=True)
    p.add_argument('--asan', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    a.output.mkdir(parents=True, exist_ok=False)
    for version in ('src1.6.1', 'src1.6.2'):
        meta = json.loads((a.guarded / ('build-' + version) / 'build.json').read_text())
        assert all(sha(root / version / name) == value for name, value in meta['source_sha256'].items())
    for name, source in (('guarded', a.guarded), ('off', a.off)):
        for file in ('results.json', 'summary.json'):
            shutil.copy2(str(source / file), str(a.output / (name + '-' + file)))
        with zipfile.ZipFile(str(a.output / (name + '-evidence.zip')), 'w', zipfile.ZIP_DEFLATED) as z:
            for path in sorted(source.rglob('*')):
                if path.is_file() and path.suffix in ('.json', '.xml', '.log', '.txt', '.lp', '.sh'):
                    z.write(str(path), str(path.relative_to(source)))
    for label, directory in (('unit', a.unit), ('asan', a.asan)):
        shutil.copy2(str(directory / 'Testing/Temporary/LastTest.log'), str(a.output / (label + '-LastTest.log')))
        shutil.copy2(str(directory / 'CMakeCache.txt'), str(a.output / (label + '-CMakeCache.txt')))
    with zipfile.ZipFile(str(a.output / 'official-semantics.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted((a.unit / 'official-semantics').glob('*')):
            if path.is_file() and (path.suffix == '.lp' or path.name.endswith('vanswer.txt')):
                z.write(str(path), path.name)
    diff = []
    for path in sorted((root / 'src1.6.2').glob('*')):
        old = root / 'src1.6.1' / path.name
        if path.is_file() and path.suffix in ('.cpp', '.hpp', '.txt') and old.exists():
            diff += list(difflib.unified_diff(old.read_text().splitlines(), path.read_text().splitlines(),
                fromfile='src1.6.1/' + path.name, tofile='src1.6.2/' + path.name, lineterm=''))
    (a.output / 'source.diff').write_text('\n'.join(diff) + '\n')
    rows = json.loads((a.guarded/'results.json').read_text())
    summary = json.loads((a.guarded/'summary.json').read_text())
    lines = ['# 官方配对明细', '', '固定种子 20260927；两版均 guarded；基础分不含时间奖励。', '',
             '|题组/题|模式|Stage|1.6.1 基础分|1.6.2 基础分|1.6.2 内部分|',
             '|---|---|---:|---:|---:|---:|']
    for r in rows:
        case = Path(r['case'])
        lines.append('|{} / {}|{}|{}|{}|{}|{}|'.format(case.parent.name, case.stem,
            r['mode'], r['stage'], r['baseline']['base'], r['current']['base'], r['current']['internal_base']))
    (a.output/'REPORT.md').write_text('\n'.join(lines) + '\n')
    metadata = dict(source_hashes_verified=True,
        original_161_unchanged=True, guarded_pairs=len(rows),
        off_pairs=len(json.loads((a.off/'results.json').read_text())),
        gains=sum(r['current']['base'] > r['baseline']['base'] for r in rows),
        losses=len(summary['losses']),
        artifacts={path.name:sha(path) for path in a.output.iterdir() if path.is_file()})
    (a.output/'manifest.json').write_text(json.dumps(metadata, indent=2))
    print(json.dumps(metadata, indent=2))

if __name__ == '__main__':
    main()
