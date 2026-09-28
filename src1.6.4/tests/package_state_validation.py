#!/usr/bin/env python3
"""Collect version-local state audit evidence without changing source inputs."""
import difflib
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src1.6.4'
OUT = SOURCE / 'test-results/validation-20260927'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    summaries = {}
    differences = []
    timing_differences = []
    for label in ('full', 'target', 'off'):
        run = Path('/tmp/rdfw-164-release-final-' + label)
        rows = json.loads((run / 'results.json').read_text())
        assert len(rows)=={'full':86,'target':18,'off':14}[label], 'suite incomplete: '+label
        (OUT / (label + '-results.json')).write_text(json.dumps(rows, ensure_ascii=False, indent=2))
        summary = dict(pairs=len(rows), identical_base=0, identical_actions=0,
                       gains=0, losses=0, invalid=0, timeouts=0, cost_mismatches=0,
                       lower_exceeds_official=0)
        summary['raw_score_differences']=0
        summary['identical_goal_constraint_counts']=0
        for row in rows:
            a, b = row['pair']
            summary['identical_base'] += a['base'] == b['base']
            summary['identical_actions'] += a['trace']['actions'] == b['trace']['actions']
            summary['identical_goal_constraint_counts'] += (a['goals'],a['constraints']) == (b['goals'],b['constraints'])
            summary['gains'] += b['base'] > a['base']
            summary['losses'] += b['base'] < a['base']
            if a['official'] != b['official']:
                summary['raw_score_differences'] += 1
                timing_differences.append(dict(suite=label, case=row['case'],mode=row['mode'],
                    repeat=row['repeat'],base=[a['base'],b['base']],
                    official=[a['official'],b['official']],
                    time_reward=[a['official']-a['base'],b['official']-b['base']],
                    elapsed_ms=[a['elapsed_ms'],b['elapsed_ms']]))
            for p in row['pair']:
                summary['invalid'] += p['status'] != 'ok' or p['base'] is None
                summary['timeouts'] += bool(p['timed_out'])
                summary['cost_mismatches'] += p['internal_cost'] != p['cost']
                summary['lower_exceeds_official'] += p['internal_base'] > p['base']
            if a['base'] != b['base'] or a['trace']['actions'] != b['trace']['actions']:
                differences.append(dict(suite=label, case=row['case'], mode=row['mode'],
                                        repeat=row['repeat'], pair=row['pair']))
        summaries[label] = summary
        with zipfile.ZipFile(str(OUT / (label + '-evidence.zip')), 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in run.rglob('*'):
                if path.is_file():
                    archive.write(str(path), str(path.relative_to(run)))
    logs = ('ctest', 'asan-ctest', 'baseline-ctest', 'baseline-asan-ctest',
            'probe-ctest', 'before-ctest', 'partial-ctest', 'finalmove-before',
            'before-derived', 'before-unknown', 'before-open-inference',
            'build', 'asan-build', 'probe-build')
    for name in logs:
        path = Path('/tmp/rdfw-164-' + name + '.log')
        if path.exists(): shutil.copy2(str(path), str(OUT / (name + '.log')))
    for kind, directory in [('unit','/tmp/rdfw-164-unit'),('asan','/tmp/rdfw-164-asan')]:
        for filename in ('CMakeCache.txt', 'Testing/Temporary/LastTest.log'):
            shutil.copy2(str(Path(directory) / filename), str(OUT / (kind+'-'+Path(filename).name)))
    source_files = [p for p in SOURCE.iterdir() if p.suffix in ('.cpp','.hpp','.txt')]
    manifest = dict(source_sha256={p.name:digest(p) for p in source_files},
                    baseline_sha256={p.name:digest(ROOT/'src1.6.3'/p.name) for p in source_files})
    fallback={'src1.6.3':Path('/tmp/rdfw-164-verified-full/build-src1.6.3/build.json'),
              'src1.6.4':Path('/tmp/rdfw-164-release-final-prebuild/build-src1.6.4/build.json')}
    build_paths={v:(Path('/tmp/rdfw-164-release-final-full')/('build-'+v)/'build.json')
                 if (Path('/tmp/rdfw-164-release-final-full')/('build-'+v)/'build.json').exists()
                 else fallback[v] for v in fallback}
    builds={v:json.loads(build_paths[v].read_text())
            for v in ('src1.6.3','src1.6.4')}
    for v, build in builds.items():
        shutil.copy2(str(build_paths[v]),str(OUT/(v+'-build.json')))
    assert builds['src1.6.4']['source_sha256']==manifest['source_sha256']
    assert builds['src1.6.3']['source_sha256']==manifest['baseline_sha256']
    assert builds['src1.6.4']['sdk_sha256']==builds['src1.6.3']['sdk_sha256']
    manifest['build_matches_final_source']=True
    manifest['paired_sdk_hashes_equal']=True
    baseline_release=json.loads((ROOT/'src1.6.3/test-results/validation-20260927/src1.6.3-build.json').read_text())
    manifest['baseline_matches_163_release']=baseline_release['source_sha256']==manifest['baseline_sha256']
    assert manifest['baseline_matches_163_release']
    manifest['sdk_matches_163_release']=baseline_release['sdk_sha256']==builds['src1.6.3']['sdk_sha256']
    assert manifest['sdk_matches_163_release']
    old_rows=json.loads((ROOT/'src1.6.3/test-results/validation-20260927/full-results.json').read_text())
    old_hashes={(Path(r['case']).parent.name,Path(r['case']).name,r['mode']):r['sha256'] for r in old_rows}
    new_rows=json.loads((OUT/'full-results.json').read_text())
    manifest['original_case_hashes_unchanged']=all(old_hashes[(Path(r['case']).parent.name,
        Path(r['case']).name,r['mode'])]==r['sha256'] for r in new_rows)
    assert manifest['original_case_hashes_unchanged']
    manifest['source_changes']={}
    diff=[]
    for path in SOURCE.rglob('*'):
        if not path.is_file() or 'test-results' in path.parts or '__pycache__' in path.parts: continue
        rel=path.relative_to(SOURCE)
        old=ROOT/'src1.6.3'/rel
        if not old.exists(): manifest['source_changes'][str(rel)]='new'; continue
        if old.read_bytes()==path.read_bytes(): continue
        manifest['source_changes'][str(rel)]='changed'
        try:
            diff.extend(difflib.unified_diff(old.read_text(encoding='utf-8-sig').splitlines(True),
                         path.read_text(encoding='utf-8-sig').splitlines(True),
                         fromfile='src1.6.3/'+str(rel),tofile='src1.6.4/'+str(rel)))
        except UnicodeDecodeError: pass
    (OUT/'source.diff').write_text(''.join(diff),encoding='utf-8')
    (OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2))
    (OUT/'summary.json').write_text(json.dumps(summaries,ensure_ascii=False,indent=2))
    (OUT/'behavior-differences.json').write_text(json.dumps(differences,ensure_ascii=False,indent=2))
    (OUT/'timing-differences.json').write_text(json.dumps(timing_differences,ensure_ascii=False,indent=2))
    development=Path('/tmp/rdfw-164-final-full')
    dev_rows=[r for r in json.loads((development/'results.json').read_text()) if Path(r['case']).stem=='03']
    (OUT/'discarded-03-results.json').write_text(json.dumps(dev_rows,ensure_ascii=False,indent=2))
    with zipfile.ZipFile(str(OUT/'discarded-03-evidence.zip'),'w',zipfile.ZIP_DEFLATED) as archive:
        for r in dev_rows:
            for p in r['pair']:
                for f in Path(p['output']).rglob('*'):
                    if f.is_file(): archive.write(str(f),str(f.relative_to(development)))
        for version in ('src1.6.3','src1.6.4'):
            path=development/('build-'+version)/'build.json'
            archive.write(str(path),str(path.relative_to(development)))
    print(json.dumps(summaries,indent=2))
    print('final_source_and_baseline_hashes_verified')

if __name__ == '__main__': main()
