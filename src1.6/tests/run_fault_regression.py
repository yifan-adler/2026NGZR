#!/usr/bin/env python3
"""Run marked-illegal fixtures with the explicit unique-goal contract."""
import csv
import json
from pathlib import Path
import shlex
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'src1.1.2 (x)'/'tools'))
from baseline import run_case
from compare_legal import extract

executable=Path(sys.argv[1]).resolve()
output=Path(sys.argv[2]).resolve()
output.mkdir(parents=True,exist_ok=False)
wrapper=output/'unique-goals.sh'
wrapper.write_text('#!/bin/sh\nexec '+shlex.quote(str(executable))+' -deduplicate 1 "$@"\n')
wrapper.chmod(0o755)
fixture=ROOT/'题目/illegal_fault_model_2026'
results=[]
for row in csv.DictReader((fixture/'manifest.csv').open()):
    out=output/row['id']
    r=run_case(Path('/home/yifan/env-release-2026'),ROOT/'src1.3.3-fixed',wrapper,
               fixture/row['file'],int(row['stage']),'it',out,5000,None)
    r.update(extract(out)); r['id']=row['id']; r['fault']=row['fault']
    results.append(r)
    (output/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    print(row['id'],r['status'],r['official_score'],r['preflight'],flush=True)
