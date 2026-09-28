#!/usr/bin/env python3
"""Create a timing-only 1.6.2 snapshot; leave the original source untouched."""
import argparse
import json
from pathlib import Path
import re
import shutil
from compare_legal import build
from baseline import digest

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--sdk',type=Path,default=Path('/home/yifan/env-release-2026'))
    a=p.parse_args()
    root=Path(__file__).resolve().parents[2]
    a.output.mkdir(parents=True,exist_ok=False)
    source=a.output/'source'
    source.mkdir()
    for f in (root/'src1.6.2').iterdir():
        if f.is_file(): shutil.copy2(str(f),str(source/f.name))
    shutil.copy2(str(root/'src1.6.3/stage_timing.hpp'),str(source/'stage_timing.hpp'))
    f=source/'rdfw.cpp';s='#include "stage_timing.hpp"\n'+f.read_text()
    for name,phase in [('BuildTaskGroupPlan','PROJECTION'),('EvaluateShadowCandidates','CANDIDATES'),('UpdateConstraintLedger','LEDGER'),('ShouldStartConstraintTrade','TRADEOFF'),('TryGuardedDecision','GUARD')]:
        pattern=r'(\bRDFW::'+name+r'\([^{}]*?\)\s*(?:const\s*)?\{)'
        s,n=re.subn(pattern,r'\1\n    StageTimer phase_timer(StageTiming::'+phase+');',s,count=1)
        assert n==1,name
    s=s.replace('deadline_manager.reset(deadline_manager.timeLimit());','deadline_manager.reset(deadline_manager.timeLimit());\n    StageTiming::get().reset();\n    ScopeExit timing_report([this]() { StageTiming::get().report(deadline_manager.elapsed().count(), deadline_manager.remaining().count()); });',1)
    s=re.sub(r'Plug::(AskLoc|Sense|Move|TakeOut|PutIn|Close|Open|FromPlate|ToPlate|PutDown|PickUp)\(([^()]*)\)',lambda m:'TimedPlatformCall([&]() { return Plug::'+m[1]+'('+m[2]+'); })',s)
    start=s.index('{',s.index('void RDFW::ExecuteMultiGotoAggregation()'))+1
    s=s[:start]+'\n    StageTimer tail_timer(StageTiming::TAIL);'+s[start:]
    f.write_text(s)
    f=source/'terminal_checker.cpp';s='#include "stage_timing.hpp"\n'+f.read_text()
    s=s.replace('TerminalSummary TerminalChecker::evaluateAll(const RDFW& world) const {','TerminalSummary TerminalChecker::evaluateAll(const RDFW& world) const {\n    StageTimer timer(StageTiming::TERMINAL);')
    s=s.replace('const std::vector<std::shared_ptr<Object>>& planner_bindings) {','const std::vector<std::shared_ptr<Object>>& planner_bindings) {\n    StageTimer timer(StageTiming::BINDING);')
    f.write_text(s)
    binary=build(str(source.resolve()),a.sdk,a.output/'build')
    (a.output/'snapshot.json').write_text(json.dumps(dict(binary=str(binary),baseline={f.name:digest(f) for f in (root/'src1.6.2').iterdir() if f.is_file()},timing_header_sha256=digest(source/'stage_timing.hpp')),indent=2))
    print(binary)
if __name__=='__main__': main()
