#!/usr/bin/env python3
"""Build high-score Stage 1/2 trade-off cases from the verified small cases."""

import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

from generate_tradeoff_challenge import ROOT, task, render_nl

SOURCE = ROOT / '题目' / 'constraint_tradeoff_challenge_2026'
DEST = ROOT / '题目' / 'constraint_tradeoff_highscore_2026'
COLORS = ('red', 'blue', 'white', 'green', 'black', 'yellow')
KINDS = ('book', 'cup', 'bottle', 'can')
RESERVED = {('book', 'red'), ('cup', 'blue'),
            ('bottle', 'white'), ('can', 'green')}
ANCHORS = [(kind, color) for kind in KINDS for color in COLORS
           if (kind, color) not in RESERVED]


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    source_rows = json.loads((SOURCE / 'manifest.json').read_text(encoding='utf-8'))
    output_rows = []
    for row in source_rows:
        tree = ET.parse(str(SOURCE / (row['id'] + '.xml')))
        root = tree.getroot()
        info = root.find('env/info')
        instr = root.find('instr')
        nl = root.find('nl')
        support = re.search(r'\(sort 2 ([a-z]+)\)', info.text).group(1)
        instruction_lines = instr.text.strip().splitlines()
        nl_lines = [line.strip() for line in nl.text.strip().splitlines() if line.strip()]

        # The earlier two-constraint controls were rejected by a hard risk
        # cutoff.  Replace the second constraint with a pre-completed goal
        # that the bait action would invalidate, retaining one constraint.
        if row['id'] in ('N05', 'N06', 'N07', 'N08', 'N10'):
            constraint_indexes = [i for i, line in enumerate(instruction_lines)
                                  if line.startswith('(:cons_')]
            assert len(constraint_indexes) == 2
            instruction_lines.pop(constraint_indexes[1])
            nl_cons = [i for i, line in enumerate(nl_lines)
                       if 'must be' in line.lower()]
            assert len(nl_cons) == 2
            nl_lines.pop(nl_cons[1])
            extra = (task('putin X Y', 'book', 'red', support)
                     if row['family'] == 'inside-and-delivery' else
                     task('puton X Y', 'book', 'red', support))
            instruction_lines.insert(-1, extra)
            nl_lines.append(render_nl(extra))

        existing_goals = sum(line.startswith('(:task ') for line in instruction_lines)
        desired_goals = min(existing_goals + len(ANCHORS),
                            21 + int(row['id'][1:]) % 3)
        needed = desired_goals - existing_goals
        assert 0 <= needed <= len(ANCHORS)
        facts = []
        padding_tasks = []
        for j, (kind, color) in enumerate(ANCHORS[:needed]):
            identifier = 20 + j
            facts.append('(sort {0} {1}) (size {0} small) (color {0} {2}) (at {0} 2)'.format(
                identifier, kind, color))
            padding_tasks.append(task('puton X Y', kind, color, support))
        info.text = info.text.rstrip() + '\n' + '\n'.join(facts) + '\n'
        instruction_lines[-1:-1] = padding_tasks
        nl_lines.extend(render_nl(line) for line in padding_tasks)
        instr.text = '\n' + '\n'.join(instruction_lines) + '\n'
        nl.text = '\n' + '\n'.join(nl_lines) + '\n'

        goal_count = sum(line.startswith('(:task ') for line in instruction_lines)
        constraint_count = sum(line.startswith('(:cons_') for line in instruction_lines)
        theoretical_max = 40 * goal_count + 20 * constraint_count
        assert goal_count == desired_goals and 800 <= theoretical_max <= 1000
        result = dict(row)
        result.update(goals=goal_count, constraints=constraint_count,
                      theoretical_max=theoretical_max,
                      origin=row['id'], anchor_goals=needed)
        output_rows.append(result)
        tree.write(str(DEST / (row['id'] + '.xml')),
                   encoding='utf-8', xml_declaration=True)
    assert len(output_rows) == 30
    (DEST / 'manifest.json').write_text(
        json.dumps(output_rows, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print('generated 30 cases, score bounds',
          min(r['theoretical_max'] for r in output_rows),
          max(r['theoretical_max'] for r in output_rows))


if __name__ == '__main__':
    main()
