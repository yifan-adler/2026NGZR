#!/usr/bin/env python3
"""Offline parse/binding snapshots; matches Platform::TestDesc::getEnv4Plug."""
import difflib
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve()
out.mkdir(parents=True,exist_ok=False)
here=Path(__file__).resolve().parent
versions=['src1.3.2','src1.3.3-fixed']
for v in versions:
    source=ROOT/v
    command=['g++','-std=c++11','-O2','-I'+str(here/'stubs'),'-I'+str(source),str(here/'parse_snapshot.cpp')]
    command += [str(p) for p in sorted(source.glob('*.cpp')) if p.name!='main.cpp']
    command += (['-DFIXED_VERSION'] if v.endswith('fixed') else [])+['-o',str(out/v)]
    with (out/(v+'-build.log')).open('w') as log:
        subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
rows=[]
for n in range(1,37):
    if n==2: continue
    case='{:02d}'.format(n)
    text=(ROOT/'题目/realcompetiton_2024'/(case+'.xml')).read_text(encoding='utf-8')
    text=re.sub(r'<!--.*?-->','',text,flags=re.S)
    flags=dict(re.findall(r'(mis|err|ans)="(on|off)"',text))
    def tag(t):
        m=re.search('<'+t+r'>(.*?)</'+t+'>',text,re.S)
        return re.sub(r'\s+',' ',m.group(1)).strip() if m else ''
    stage=1 if all(flags.get(k)=='off' for k in ('mis','err','ans')) else 2
    env='(:domain '+tag('info')+' '+('' if flags.get('mis')=='on' else tag('mis'))+' '+tag('w' if flags.get('err')=='on' else 'r')+')'
    (out/(case+'.env')).write_text(env,encoding='utf-8')
    for mode,t in [('it','instr'),('nt','nl')]:
        (out/(case+'.'+mode)).write_text(tag(t),encoding='utf-8')
        snapshots=[]; exits=[]
        for v in versions:
            command=[str(out/v),str(ROOT/v/'words.txt'),str(out/(case+'.env')),str(out/(case+'.'+mode)),str(stage),mode]
            run=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
            log=run.stdout.decode('utf-8',errors='replace')
            (out/(case+'-'+mode+'-'+v+'.log')).write_text(log,encoding='utf-8')
            snapshots.append([s for s in log.splitlines() if s.startswith('SNAP ')])
            exits.append(run.returncode)
        diff=list(difflib.unified_diff(*snapshots,fromfile=versions[0],tofile=versions[1],lineterm=''))
        (out/(case+'-'+mode+'.diff')).write_text('\n'.join(diff),encoding='utf-8')
        rows.append(dict(id=case,mode=mode,exits=exits,equal=snapshots[0]==snapshots[1]))
        print(case,mode,exits,'equal',not diff,flush=True)
(out/'comparison.json').write_text(json.dumps(rows,indent=2))
