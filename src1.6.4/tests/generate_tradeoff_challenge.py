#!/usr/bin/env python3
"""Generate thirty deterministic, independently scored constraint-planning cases."""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / '题目' / 'constraint_tradeoff_challenge_2026'

OBJECTS = [('book', 'red'), ('cup', 'blue'), ('bottle', 'white'),
           ('can', 'green')]


def obj(i, kind, color, location):
    return '(sort {0} {1}) (size {0} small) (color {0} {2}) {3}'.format(
        i, kind, color, location)


def task(verb, kind=None, color=None, target=None):
    clauses = []
    if kind:
        clauses += ['(sort X {})'.format(kind)]
    if color:
        clauses += ['(color X {})'.format(color)]
    if target:
        clauses += ['(sort Y {})'.format(target)]
    return '(:task ({}) (:cond {}))'.format(verb, ' '.join(clauses))


def render_nl(line):
    def noun(var):
        kind = re.search(r'\(sort {} ([a-z]+)\)'.format(var), line).group(1)
        color_match = re.search(r'\(color {} ([a-z]+)\)'.format(var), line)
        return (color_match.group(1) + ' ' if color_match else '') + kind

    if line.startswith('(:cons_'):
        relation = re.search(r'\(:info \(([a-z]+) ', line).group(1)
        if relation == 'closed':
            return 'The {} must be closed.'.format(noun('X'))
        if relation == 'near':
            return 'The {} must be near the {}.'.format(noun('X'), noun('Y'))
        if relation == 'inside':
            return 'The {} must be in the {}.'.format(noun('X'), noun('Y'))
    verb = re.search(r'\(:task \(([^)]+)\)', line).group(1)
    if verb == 'close X':
        return 'Close the {}.'.format(noun('X'))
    if verb == 'goto X':
        return 'Go to the {}.'.format(noun('X'))
    if verb == 'pickup X':
        return 'Pick up the {}.'.format(noun('X'))
    if verb == 'putin X Y':
        return 'Put the {} in the {}.'.format(noun('X'), noun('Y'))
    if verb == 'puton X Y':
        return 'Put the {} on the {}.'.format(noun('X'), noun('Y'))
    if verb == 'give human X':
        return 'Give the {} to me.'.format(noun('X'))
    raise ValueError('unrecognized instruction: ' + line)


def case(name, category, family, stage, facts, instructions, explanation):
    flags = 'on' if stage == 2 else 'off'
    nl = '\n'.join(render_nl(line) for line in instructions)
    xml = '''<?xml version="1.0" encoding="UTF-8"?>
<test>
<env mis="{flags}" err="{flags}" ans="{flags}">
<info>
{facts}
</info>
<mis></mis><err><r></r><w></w></err><extra></extra>
</env>
<instr>
(:ins
{instructions}
)
</instr>
<nl>
{nl}
</nl>
</test>
'''.format(flags=flags, facts='\n'.join(facts),
           instructions='\n'.join(instructions), nl=nl)
    (DEST / (name + '.xml')).write_text(xml, encoding='utf-8')
    goals = sum(line.startswith('(:task ') for line in instructions)
    constraints = sum(line.startswith('(:cons_') for line in instructions)
    theoretical_max = 40 * goals + 20 * constraints
    assert theoretical_max <= 1000
    return dict(id=name, category=category, family=family, stage=stage,
                goals=goals, constraints=constraints,
                theoretical_max=theoretical_max, explanation=explanation)


def closed(name, category, n, order, stage, holding, container='cupboard'):
    facts = ['(hold {}) (plate {}) (at 0 2)'.format(0, 4 if holding else 0),
             '(sort 1 human) (size 1 big) (at 1 1)',
             '(sort 2 {0}) (size 2 big) (at 2 2) (type 2 container) (closed 2)'.format(container)]
    for j, (kind, color) in enumerate(OBJECTS[:n]):
        i = 4 + j
        facts.append(obj(i, kind, color, '' if holding and j == 0 else '(at {} 2)'.format(i)))
    cons = '(:cons_notnot (:info (closed X) (:cond (sort X {}))))'.format(container)
    put = [task('putin X Y', kind, color, container) for kind, color in OBJECTS[:n]]
    if category == 'positive':
        goals = [task('close X', container), task('goto X', container)] + put
    else:
        goals = [task('pickup X', 'book', 'red'), task('goto X', container)] + put
    goals = [goals[i] for i in order]
    return case(name, category, 'closed-container', stage, facts, [cons] + goals,
                'Closed-container trade: {} object(s), task order {}.'.format(n, order))


def near(name, category, extra, order, stage, anchor='table', destination='chair'):
    # Going to the destination satisfies several location goals only in the positive variants.
    facts = ['(hold 0) (plate 0) (at 0 2)',
             '(sort 1 human) (size 1 big) (at 1 1)',
             '(sort 2 {}) (size 2 big) (at 2 2)'.format(anchor),
             '(sort 3 {}) (size 3 big) (at 3 3)'.format(destination),
             obj(4, 'book', 'red', '(at 4 2)')]
    for j, (kind, color) in enumerate(OBJECTS[1:extra + 1]):
        facts.append(obj(5 + j, kind, color, '(at {} 3)'.format(5 + j)))
    cons = '(:cons_notnot (:info (near X Y) (:cond (sort X book) (color X red) (sort Y {}))))'.format(anchor)
    if category == 'negative':
        other = 'desk' if anchor == 'table' else 'table'
        facts.append('(sort 8 {}) (size 8 big) (at 8 2)'.format(other))
        cons2 = '(:cons_notnot (:info (near X Y) (:cond (sort X book) (color X red) (sort Y {}))))'.format(other)
    else:
        cons2 = None
    goals = [task('goto X', anchor), task('puton X Y', 'book', 'red', destination)]
    if category == 'positive':
        goals += [task('goto X', destination), task('goto X', 'book', 'red')]
        goals += [task('goto X', kind, color) for kind, color in OBJECTS[1:extra + 1]]
    goals = [goals[i] for i in order]
    return case(name, category, 'near-and-location', stage, facts,
                [cons] + ([cons2] if cons2 else []) + goals,
                'Moving the red book away from {} changes nearby and location goals.'.format(anchor))


def inside(name, category, extra, order, stage, container='cupboard'):
    facts = ['(hold 0) (plate 0) (at 0 2)',
             '(sort 1 human) (size 1 big) (at 1 1)',
             '(sort 2 {0}) (size 2 big) (at 2 2) (type 2 container) (closed 2)'.format(container),
             obj(3, 'book', 'red', '(inside 3 2)')]
    for j, (kind, color) in enumerate(OBJECTS[1:extra + 1]):
        facts.append(obj(4 + j, kind, color, '(at {} 1)'.format(4 + j)))
    cons = '(:cons_notnot (:info (inside X Y) (:cond (sort X book) (color X red) (sort Y {}))))'.format(container)
    goals = [task('goto X', container),
             '(:task (give human X) (:cond (sort X book) (color X red)))']
    if category == 'positive':
        goals += [task('goto X', 'human')]
        goals += [task('goto X', kind, color) for kind, color in OBJECTS[1:extra + 1]]
    goals = [goals[i] for i in order]
    if category == 'negative':
        cons2 = '(:cons_notnot (:info (closed X) (:cond (sort X {}))))'.format(container)
    else:
        cons2 = None
    return case(name, category, 'inside-and-delivery', stage, facts,
                [cons] + ([cons2] if cons2 else []) + goals,
                'Delivering a book from {} sacrifices its inside constraint.'.format(container))


def normal(name, variant, stage):
    base = ['(hold 0) (plate 0) (at 0 2)',
            '(sort 1 human) (size 1 big) (at 1 1)',
            '(sort 2 table) (size 2 big) (at 2 2)',
            '(sort 3 chair) (size 3 big) (at 3 3)',
            obj(4, 'book', 'red', '(at 4 2)'),
            obj(5, 'cup', 'blue', '(at 5 3)'),
            obj(6, 'bottle', 'white', '(at 6 2)')]
    goto_table = task('goto X', 'table')
    goto_chair = task('goto X', 'chair')
    pickup = task('pickup X', 'book', 'red')
    puton = task('puton X Y', 'book', 'red', 'table')
    give = '(:task (give human X) (:cond (sort X book) (color X red)))'
    plans = [
        [pickup],
        [goto_table, pickup],
        [puton, goto_table],
        [give],
        [goto_table, puton],
        [goto_chair, task('goto X', 'cup', 'blue')],
        [pickup, task('goto X', 'bottle', 'white')],
        [task('goto X', 'book', 'red'), goto_table],
        [goto_chair, task('goto X', 'cup', 'blue'), pickup],
        [puton, task('goto X', 'bottle', 'white'), goto_table],
    ]
    # In these controls, the closed-container constraint is unrelated to all tasks.
    base.append('(sort 7 cupboard) (size 7 big) (at 7 4) (type 7 container) (closed 7)')
    cons = '(:cons_notnot (:info (closed X) (:cond (sort X cupboard))))'
    return case(name, 'normal', 'constraint-preserving', stage, base,
                [cons] + plans[variant],
                'Normal task group {} preserves the already-closed cupboard.'.format(variant + 1))


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    rows = []
    rows += [
        closed('P01', 'positive', 2, [2, 0, 3, 1], 1, False),
        closed('P02', 'positive', 2, [0, 1, 2, 3], 1, False),
        closed('P03', 'positive', 3, [4, 3, 2, 1, 0], 1, False),
        closed('P04', 'positive', 4, [5, 2, 4, 3, 0, 1], 1, False),
        near('P05', 'positive', 1, [1, 0, 2, 3, 4], 1),
        near('P06', 'positive', 3, [6, 5, 4, 3, 2, 1, 0], 1),
        inside('P07', 'positive', 1, [1, 0, 2, 3], 1),
        inside('P08', 'positive', 3, [5, 4, 3, 2, 1, 0], 1),
        closed('P09', 'positive', 2, [3, 2, 1, 0], 2, False),
        near('P10', 'positive', 2, [4, 1, 3, 2, 0, 5], 2),
    ]
    rows += [
        closed('N01', 'negative', 1, [0, 1, 2], 1, True),
        closed('N02', 'negative', 1, [2, 1, 0], 1, True),
        closed('N03', 'negative', 1, [1, 2, 0], 2, True),
        closed('N04', 'negative', 1, [0, 2, 1], 1, True, 'refrigerator'),
        near('N05', 'negative', 0, [0, 1], 1),
        near('N06', 'negative', 0, [1, 0], 2),
        inside('N07', 'negative', 0, [0, 1], 1),
        inside('N08', 'negative', 0, [1, 0], 2),
        closed('N09', 'negative', 1, [2, 0, 1], 2, True, 'refrigerator'),
        near('N10', 'negative', 0, [0, 1], 1, 'desk', 'sofa'),
    ]
    rows += [normal('C{:02d}'.format(i + 1), i, 2 if i in (4, 8) else 1)
             for i in range(10)]
    assert len(rows) == 30 and len({r['id'] for r in rows}) == 30
    (DEST / 'manifest.json').write_text(json.dumps(rows, indent=2, ensure_ascii=False) + '\n',
                                        encoding='utf-8')
    print('generated', len(rows), 'cases in', DEST)


if __name__ == '__main__':
    main()
