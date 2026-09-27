#!/usr/bin/env python3
"""Make separate 04/05 test copies, adding only missing instruction ')' tokens."""
import difflib
import hashlib
import json
from pathlib import Path
import re
import sys
ROOT=Path(__file__).resolve().parents[2]
output=Path(sys.argv[1]).resolve();output.mkdir(parents=True,exist_ok=False)
rows=[]
for case in ('04','05'):
    original=ROOT/'题目/realcompetiton_2024'/(case+'.xml')
    raw=original.read_text(encoding='utf-8')
    match=re.search(r'(?<=<instr>)(.*?)(?=</instr>)',raw,re.S)
    assert match
    section=match.group(1)
    forms=[]; inserted=0
    for line in section.splitlines():
        stripped=line.strip()
        if stripped.startswith(('(:task','(:info','(:cons_not')):
            balance=stripped.count('(')-stripped.count(')')
            assert balance>=0
            forms.append(stripped+')'*balance)
            inserted+=balance
    assert forms
    replacement='\n(:ins\n    '+'\n    '.join(forms)+'\n)\n'
    inserted=replacement.count(')')-section.count(')')
    assert inserted>0
    # Check semantic leaves: only unmatched closing tokens and whitespace vary.
    stripped=lambda s: re.sub(r'[()\s]','',s)
    assert stripped(section)==stripped(replacement)
    repaired=raw[:match.start()]+replacement+raw[match.end():]
    destination=output/(case+'.xml');destination.write_text(repaired,encoding='utf-8')
    diff='\n'.join(difflib.unified_diff(section.splitlines(),replacement.splitlines(),
        fromfile=case+'.xml original instruction',tofile=case+'.xml repaired instruction',lineterm=''))
    (output/(case+'.diff')).write_text(diff+'\n',encoding='utf-8')
    rows.append(dict(id=case,original_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
                     repaired_sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
                     tokens_unchanged=True,inserted_closing_parentheses=inserted,
                     original=str(original),repaired=str(destination)))
(output/'manifest.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
