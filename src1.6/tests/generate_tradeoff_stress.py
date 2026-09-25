#!/usr/bin/env python3
"""Build a 40-case metamorphic Stage 1/2 decision-stress suite."""

import json
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / '题目' / 'constraint_tradeoff_highscore_2026'
DEST = ROOT / '题目' / 'constraint_tradeoff_stress_2026'
PROTOTYPES = ('P01', 'P05', 'P07', 'N01', 'N03', 'N05',
              'N06', 'N07', 'N08', 'C01', 'C05', 'C09')
TWO_CONSTRAINTS = frozenset(('N05', 'N06', 'N07', 'N08'))


def split_instructions(element):
    lines = [line.strip() for line in element.text.splitlines() if line.strip()]
    assert lines[0] == '(:ins' and lines[-1] == ')'
    return lines[1:-1]


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    source = {row['id']: row for row in json.loads(
        (SOURCE / 'manifest.json').read_text(encoding='utf-8'))}
    manifest = []
    for prototype in PROTOTYPES:
        original = source[prototype]
        for variant in range(4 if prototype in TWO_CONSTRAINTS else 3):
            name = 'S{:02d}'.format(len(manifest) + 1)
            tree = ET.parse(str(SOURCE / (prototype + '.xml')))
            root = tree.getroot()
            env = root.find('env')
            info = root.find('env/info')
            instructions = split_instructions(root.find('instr'))
            nl = [line.strip() for line in root.find('nl').text.splitlines()
                  if line.strip()]
            assert len(instructions) == len(nl)
            paired = list(zip(instructions, nl))
            constraints = [pair for pair in paired if pair[0].startswith('(:cons_')]
            goals = [pair for pair in paired if pair[0].startswith('(:task ')]
            assert len(constraints) == 1 and len(goals) >= 21

            if variant == 0:
                # Swap known/unknown initial state without changing the goal.
                stage = 3 - original['stage']
                transformation = 'stage_swap'
            elif variant == 1:
                # Reverse task priority while preserving the objective set.
                stage = original['stage']
                goals.reverse()
                transformation = 'reverse_goals'
            elif variant == 2:
                # Swap stage and make one formerly completed anchor a genuine
                # outstanding goal; its object remains an intentional distractor.
                stage = 3 - original['stage']
                transformation = 'stage_swap_one_anchor_unfinished'
                anchor_id = 20 + original['anchor_goals'] - 1
                old = '(at {} 2)'.format(anchor_id)
                new = '(at {} 3)'.format(anchor_id)
                assert old in info.text
                info.text = info.text.replace(old, new, 1)
            else:
                # Restore the second protected fact from the unpadded source.
                # The task action can now damage multiple scored relations.
                stage = original['stage']
                transformation = 'two_constraints'
                low = ET.parse(str(ROOT / '题目' /
                                   'constraint_tradeoff_challenge_2026' /
                                   (prototype + '.xml'))).getroot()
                low_pairs = list(zip(split_instructions(low.find('instr')),
                                     [line.strip() for line in
                                      low.find('nl').text.splitlines()
                                      if line.strip()]))
                extra_constraints = [pair for pair in low_pairs
                                     if pair[0].startswith('(:cons_')]
                assert len(extra_constraints) == 2
                constraints.append(extra_constraints[1])

            flags = 'on' if stage == 2 else 'off'
            for key in ('mis', 'err', 'ans'):
                env.set(key, flags)
            root.find('instr').text = '\n(:ins\n' + '\n'.join(
                pair[0] for pair in constraints + goals) + '\n)\n'
            root.find('nl').text = '\n' + '\n'.join(
                pair[1] for pair in constraints + goals) + '\n'
            theoretical_max = 40 * len(goals) + 20 * len(constraints)
            assert 800 <= theoretical_max <= 1000
            manifest.append(dict(id=name, origin=prototype,
                                 category=original['category'],
                                 family=original['family'], stage=stage,
                                 transformation=transformation,
                                 goals=len(goals), constraints=len(constraints),
                                 theoretical_max=theoretical_max))
            tree.write(str(DEST / (name + '.xml')), encoding='utf-8',
                       xml_declaration=True)
    assert len(manifest) == 40
    (DEST / 'manifest.json').write_text(json.dumps(
        manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('generated', len(manifest), 'cases; score range',
          min(row['theoretical_max'] for row in manifest),
          max(row['theoretical_max'] for row in manifest))


if __name__ == '__main__':
    main()
