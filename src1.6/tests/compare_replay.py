#!/usr/bin/env python3
"""Replay official responses to both planners with the same test clock.

Every requested action must match the official tape. Stops the virtual clock at
the tape boundary. This isolates semantic/decision regressions from wall time;
it does not replace official scoring or a real-time performance experiment.
"""
import difflib
import json
from pathlib import Path
import re
import subprocess
import sys
from compare_legal import ROOT, VERSIONS, extract
official,parse,out=[Path(s).resolve() for s in sys.argv[1:4]]
out.mkdir(parents=True,exist_ok=False)
here=Path(__file__).resolve().parent
for v in VERSIONS:
    source=ROOT/v
    deadline=(source/'deadline_manager.cpp').read_text()
    old='return std::chrono::duration_cast<Duration>(Clock::now() - start_time_);'
    assert old in deadline
    deadline='#include "cserver/plug.hpp"\n'+deadline.replace(old,'return Duration(Replay().elapsed);')
    variant=out/(v+'-deadline.cpp');variant.write_text(deadline)
    command=['g++','-std=c++11','-O2','-I'+str(here/'replay'),'-I'+str(source),str(here/'replay_plan.cpp'),str(variant)]
    command += [str(p) for p in sorted(source.glob('*.cpp')) if p.name not in ('main.cpp','deadline_manager.cpp')]
    command += (['-DFIXED_VERSION'] if v.endswith('fixed') else [])+['-o',str(out/v)]
    with (out/(v+'-build.log')).open('w') as log:
        subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
rows=[]
for row in json.loads((official/'comparison.json').read_text()):
    case=row['id']
    if case in ('04','05'): continue
    server=(official/VERSIONS[0]/case/'server.log').read_text()
    tape=[]
    for request,reply in re.findall(r'^\s*\[([^|\]]+)\|([^\]]*)\]\s*$',server,re.M):
        tape.append(request.strip()+'|'+reply.strip())
    path=out/(case+'.tape');path.write_text('\n'.join(tape)+'\n')
    snapshots=[];decisions=[];exits=[]
    for v in VERSIONS:
        dest=out/v.replace('src','runs-')/case;dest.mkdir(parents=True)
        run=subprocess.run([str(out/v),str(ROOT/v/'words.txt'),str(parse/(case+'.env')),
                            str(parse/(case+'.it')),str(path),'it'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
        log=run.stdout.decode('utf-8',errors='replace')
        (dest/'client.log').write_text(log,encoding='utf-8')
        (dest/'server.log').write_text('')
        exits.append(run.returncode)
        snapshots.append([s for s in log.splitlines() if s.startswith(('REPLAY ','SNAP '))])
        decisions.append(extract(dest)['decisions'])
    for name,pair in [('state',snapshots),('decisions',decisions)]:
        (out/(case+'-'+name+'.diff')).write_text('\n'.join(difflib.unified_diff(*pair,lineterm='')),encoding='utf-8')
    result=dict(id=case,exits=exits,state_equal=snapshots[0]==snapshots[1],decisions_equal=decisions[0]==decisions[1])
    rows.append(result);print(result,flush=True)
(out/'comparison.json').write_text(json.dumps(rows,indent=2))
sys.exit(int(any(r['exits']!=[0,0] or not r['state_equal'] or not r['decisions_equal'] for r in rows)))
