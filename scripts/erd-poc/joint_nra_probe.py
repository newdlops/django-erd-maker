#!/usr/bin/env python3
"""Bounded Captain experiment: simultaneous real x/y and four-side ports.

The model is deliberately a relaxation until external whole-product feedback
has been added. A SAT answer is a candidate, never a visual-crossing result.
Every original card/relation is preserved when the candidate is materialized.
Run only through run_memory_bounded.py. No product code or cache is modified.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / '.tmp/captain-improvement/joint-nra'
sys.path.insert(0, str(HERE / 'deps'))
import z3

SCALE = 1000
BOUNDARY_TOL = .011 / SCALE


def rational(v):
    # Retain the input decimal rather than snapping cards or endpoint slots.
    return z3.RealVal(str(v))


def scaled(v):
    return rational(v) / SCALE


def orient(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def separated_segments(a, b, c, d):
    # Complement of proper crossing in ideal real geometry. Degenerate
    # contacts are not accepted: the product audit finds them and adds a
    # strict-contact constraint. This avoids artificial quartic products.
    o1, o2, o3, o4 = orient(a, b, c), orient(a, b, d), orient(c, d, a), orient(c, d, b)
    return z3.Or(z3.And(o1 >= 0, o2 >= 0), z3.And(o1 <= 0, o2 <= 0),
                 z3.And(o3 >= 0, o4 >= 0), z3.And(o3 <= 0, o4 <= 0))


def clear_rect(a, b, rect, pad=10 / SCALE):
    # Separating axes for a segment and an OPEN axis-aligned rectangle.
    # A zero-length segment is excluded by endpoint/card constraints.
    x, y, w, h = rect
    l, r, t, bottom = x - pad, x + w + pad, y - pad, y + h + pad
    os = [orient(a, b, p) for p in [(l, t), (r, t), (r, bottom), (l, bottom)]]
    return z3.Or(z3.And(a[0] <= l, b[0] <= l), z3.And(a[0] >= r, b[0] >= r),
                 z3.And(a[1] <= t, b[1] <= t), z3.And(a[1] >= bottom, b[1] >= bottom),
                 z3.And(*[o >= 0 for o in os]), z3.And(*[o <= 0 for o in os]))


def boundary_exit(p, other, rect, tolerance=BOUNDARY_TOL):
    x, y, w, h = rect
    tol = rational(tolerance)
    return z3.And(p[0] >= x - tol, p[0] <= x + w + tol,
                  p[1] >= y - tol, p[1] <= y + h + tol,
                  z3.Or(z3.And(p[0] >= x - tol, p[0] <= x + tol, other[0] <= p[0]),
                        z3.And(p[0] >= x + w - tol, p[0] <= x + w + tol, other[0] >= p[0]),
                        z3.And(p[1] >= y - tol, p[1] <= y + tol, other[1] <= p[1]),
                        z3.And(p[1] >= y + h - tol, p[1] <= y + h + tol, other[1] >= p[1])))


def separated_cards(a, b):
    x, y, w, h = a
    u, v, w2, h2 = b
    return z3.Or(x + w + scaled(55.99) <= u, u + w2 + scaled(55.99) <= x,
                 y + h + scaled(41.99) <= v, v + h2 + scaled(41.99) <= y)


def no_point_contact(p, a, b):
    o = orient(a, b, p)
    return z3.Or(o > 0, o < 0,
                 z3.And(p[0] < a[0], p[0] < b[0]), z3.And(p[0] > a[0], p[0] > b[0]),
                 z3.And(p[1] < a[1], p[1] < b[1]), z3.And(p[1] > a[1], p[1] > b[1]))


def numeric_cross(a, b, c, d):
    return orient(a, b, c) * orient(a, b, d) < -1e-9 and orient(c, d, a) * orient(c, d, b) < -1e-9


def numeric_hit(a, b, n, padding=10):
    l, r, t, bottom = n['x'] - padding, n['x'] + n['w'] + padding, n['y'] - padding, n['y'] + n['h'] + padding
    if max(a[0], b[0]) <= l or min(a[0], b[0]) >= r or max(a[1], b[1]) <= t or min(a[1], b[1]) >= bottom:
        return False
    os = [orient(a, b, p) for p in [(l, t), (r, t), (r, bottom), (l, bottom)]]
    return not (all(o >= 0 for o in os) or all(o <= 0 for o in os))


def interval_sub(a, b):
    return math.nextafter(a[0] - b[1], -math.inf), math.nextafter(a[1] - b[0], math.inf)


def interval_mul(a, b):
    vs = [x * y for x in a for y in b]
    return math.nextafter(min(vs), -math.inf), math.nextafter(max(vs), math.inf)


def interval_orient(a, b, c):
    return interval_sub(interval_mul(interval_sub(b[0], a[0]), interval_sub(c[1], a[1])),
                        interval_mul(interval_sub(b[1], a[1]), interval_sub(c[0], a[0])))


def guaranteed_cross(a, b, c, d):
    os = [interval_orient(a, b, c), interval_orient(a, b, d), interval_orient(c, d, a), interval_orient(c, d, b)]
    opposite = lambda p, q: (p[0] > .01 and q[1] < -.01) or (p[1] < -.01 and q[0] > .01)
    return opposite(os[0], os[1]) and opposite(os[2], os[3])


def write_json(path, data):
    Path(path).write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n')


class JointModel:
    def __init__(self, data, group, args):
        self.data, self.args = data, args
        self.selected = set(group['nodeIndices'])
        self.active = set(group['activeEdges'])
        self.group = group
        self.s = z3.Tactic('smt').solver() if args.engine == 'smt' else z3.SolverFor('QF_NRA')
        self.s.set(timeout=int(args.seconds * 1000), max_memory=160, random_seed=170)
        if args.phase is not None:
            self.s.set(phase_selection=args.phase)
        self.initial = []
        self.rects, self.routes = [], []
        self.node_bounds, self.port_bounds = [], []
        self.interval_fixed_crosses = 0
        self.costs = {'cross': {}, 'hit': {}}
        self.hard = {'spacing': set(), 'adjacent': set(), 'contact': set(), 'entry': set()}
        self.pinned_failures = []
        for i, n in enumerate(data['nodes']):
            if i in self.selected:
                x, y = z3.Reals(f'x{i} y{i}')
                self.initial += [(x, scaled(n['x'])), (y, scaled(n['y']))]
                radius = args.radius
                minx, maxx = max(0, n['x'] - radius) if radius else 0, min(38720 - n['w'], n['x'] + radius) if radius else 38720 - n['w']
                miny, maxy = max(0, n['y'] - radius) if radius else 0, min(38720 - n['h'], n['y'] + radius) if radius else 38720 - n['h']
                self.s.add(x >= scaled(minx), x <= scaled(maxx), y >= scaled(miny), y <= scaled(maxy))
            else:
                x, y = scaled(n['x']), scaled(n['y'])
                minx = maxx = n['x']
                miny = maxy = n['y']
            self.rects.append((x, y, scaled(n['w']), scaled(n['h'])))
            self.node_bounds.append(((minx, maxx), (miny, maxy)))
        for i, e in enumerate(data['edges']):
            ps, boxes = [], []
            for j, p in enumerate(e['p']):
                if i in self.active:
                    nidx = e['u'] if j == 0 else e['v']
                    n, (xb, yb) = data['nodes'][nidx], self.node_bounds[nidx]
                    if args.ports == 'perimeter':
                        t = z3.Real(f'e{i}p{j}t')
                        horizontal, low = z3.Bools(f'e{i}p{j}horizontal e{i}p{j}low')
                        self.s.add(t >= 0, t <= 1)
                        rx, ry, w, h = self.rects[nidx]
                        x = rx + z3.If(horizontal, t * w, z3.If(low, 0, w))
                        y = ry + z3.If(horizontal, z3.If(low, 0, h), t * h)
                        distances = [abs(p[0]-n['x']), abs(p[0]-n['x']-n['w']), abs(p[1]-n['y']), abs(p[1]-n['y']-n['h'])]
                        side = min(range(4), key=lambda k: distances[k])
                        raw_t = (rational(p[0])-rational(n['x']))/rational(n['w']) if side >= 2 else (rational(p[1])-rational(n['y']))/rational(n['h'])
                        raw_number = (p[0]-n['x'])/n['w'] if side >= 2 else (p[1]-n['y'])/n['h']
                        if raw_number < 0: raw_t = rational(0)
                        if raw_number > 1: raw_t = rational(1)
                        self.initial += [(t, raw_t), (horizontal, z3.BoolVal(side >= 2)), (low, z3.BoolVal(side in [0, 2]))]
                    else:
                        x, y = z3.Reals(f'e{i}p{j}x e{i}p{j}y')
                        ix, iy = scaled(p[0]), scaled(p[1])
                        if args.ports == 'exact-cartesian':
                            distances = [abs(p[0]-n['x']), abs(p[0]-n['x']-n['w']), abs(p[1]-n['y']), abs(p[1]-n['y']-n['h'])]
                            side = min(range(4), key=lambda k: distances[k])
                            if p[0] < n['x']: ix = scaled(n['x'])
                            if p[0] > n['x'] + n['w']: ix = scaled(n['x']) + scaled(n['w'])
                            if p[1] < n['y']: iy = scaled(n['y'])
                            if p[1] > n['y'] + n['h']: iy = scaled(n['y']) + scaled(n['h'])
                            if side == 0: ix = scaled(n['x'])
                            elif side == 1: ix = scaled(n['x']) + scaled(n['w'])
                            elif side == 2: iy = scaled(n['y'])
                            else: iy = scaled(n['y']) + scaled(n['h'])
                        self.initial += [(x, ix), (y, iy)]
                    ps.append((x, y))
                    # These redundant bounds help arithmetic propagation.
                    # Outward rounding also makes interval elimination safe.
                    bx = (math.nextafter(xb[0] - .011, -math.inf), math.nextafter(xb[1] + n['w'] + .011, math.inf))
                    by = (math.nextafter(yb[0] - .011, -math.inf), math.nextafter(yb[1] + n['h'] + .011, math.inf))
                    boxes.append((bx, by))
                    if args.radius:
                        self.s.add(x >= scaled(bx[0]), x <= scaled(bx[1]), y >= scaled(by[0]), y <= scaled(by[1]))
                else:
                    ps.append((scaled(p[0]), scaled(p[1])))
                    boxes.append(((p[0], p[0]), (p[1], p[1])))
            self.routes.append(ps)
            self.port_bounds.append(boxes)
        for i in sorted(self.active):
            e, (a, b) = data['edges'][i], self.routes[i]
            tol = BOUNDARY_TOL if args.ports == 'band' else 0
            self.s.add(boundary_exit(a, b, self.rects[e['u']], tol), boundary_exit(b, a, self.rects[e['v']], tol))
        if args.all_adjacent:
            for i in sorted(self.active):
                e = data['edges'][i]
                for j, f in enumerate(data['edges']):
                    if i == j or (j in self.active and j < i):
                        continue
                    if {e['u'], e['v']} & {f['u'], f['v']}:
                        self.add_adjacent(i, j)
        for i in sorted(self.selected):
            a = data['nodes'][i]
            for j, b in enumerate(data['nodes']):
                if i == j or (j in self.selected and j < i):
                    continue
                # All selected/selected constraints, plus nearby fixed cards.
                # Farther obstacles remain in the full audit/refinement loop.
                if j in self.selected or (abs(a['x'] - b['x']) < 750 + max(a['w'], b['w']) and abs(a['y'] - b['y']) < 750 + max(a['h'], b['h'])):
                    self.add_spacing(i, j)
        for i, j in data['crossings']:
            if i in self.active or j in self.active:
                self.add_cross(i, j)
        for e, n in data['hits']:
            if e in self.active or n in self.selected:
                self.add_hit(e, n)
        if args.feedback:
            for file in args.feedback:
                feedback = json.loads(Path(file).read_text())
                for i, j in feedback['crossings']:
                    if i in self.active or j in self.active:
                        self.add_cross(i, j)
                for e, n in feedback['hits']:
                    if e in self.active or n in self.selected:
                        self.add_hit(e, n)
                for i, j in feedback['spacing']:
                    self.add_spacing(i, j)
                for i, j in feedback['adjacent']:
                    self.add_adjacent(i, j)
                for i, j in feedback['contacts']:
                    self.add_contact(i, j)
                for e, n in feedback['entries']:
                    key = (e, n)
                    if key not in self.hard['entry']:
                        self.hard['entry'].add(key)
                        self.s.add(clear_rect(*self.routes[e], self.rects[n], pad=-.02 / SCALE))
        if args.mode == 'baseline':
            self.s.add(*[v == initial for v, initial in self.initial])
        elif args.engine == 'nlsat':
            # A hint is not a hard constraint; it cannot fix an axis or side.
            for v, initial in self.initial:
                self.s.set_initial_value(v, z3.simplify(initial))
        target_cross = 1402 if args.mode == 'baseline' else args.target_cross
        target_visual = 1818 if args.mode == 'baseline' else args.target_visual
        self.cross_limit = target_cross - group['immutableCross']
        self.total_limit = target_visual - group['immutableVisual']
        cross_flags, hit_flags = list(self.costs['cross'].values()), list(self.costs['hit'].values())
        self.s.add(z3.PbLe([(b, 1) for b in cross_flags], self.cross_limit))
        self.s.add(z3.PbLe([(b, 1) for b in cross_flags + hit_flags], self.total_limit))
        if args.allow_modeled_conflicts:
            # Regression drill: remove conflict minimization to get a geometry
            # proposal, then test that full-product counterexamples reject it.
            # This option cannot be used with the 100-count improvement gate.
            assert self.cross_limit >= len(cross_flags) and self.total_limit >= len(cross_flags + hit_flags)
            self.s.add(*(cross_flags + hit_flags))
        if args.pin_file:
            declarations = {v.decl().name(): v for v, _ in self.initial}
            self.s.add(z3.parse_smt2_string(Path(args.pin_file).read_text(), decls=declarations))

    def add_spacing(self, i, j):
        key = tuple(sorted((i, j)))
        if key not in self.hard['spacing']:
            self.hard['spacing'].add(key)
            self.s.add(separated_cards(self.rects[i], self.rects[j]))

    def add_cross(self, i, j):
        key = tuple(sorted((i, j)))
        if self.args.all_adjacent and key in self.hard['adjacent']:
            return  # These crossings are hard-forbidden, so their cost is zero.
        if key not in self.costs['cross']:
            if self.args.radius and guaranteed_cross(*self.port_bounds[i], *self.port_bounds[j]):
                self.costs['cross'][key] = z3.BoolVal(True)
                self.interval_fixed_crosses += 1
                return
            b = z3.Bool(f'cross_{key[0]}_{key[1]}')
            self.costs['cross'][key] = b
            self.s.add(z3.Implies(z3.Not(b), separated_segments(*self.routes[i], *self.routes[j])))

    def add_adjacent(self, i, j):
        key = tuple(sorted((i, j)))
        if key not in self.hard['adjacent']:
            self.hard['adjacent'].add(key)
            self.s.add(separated_segments(*self.routes[i], *self.routes[j]))

    def add_hit(self, e, n):
        key = (e, n)
        if key not in self.costs['hit']:
            b = z3.Bool(f'hit_{e}_{n}')
            self.costs['hit'][key] = b
            self.s.add(z3.Implies(z3.Not(b), clear_rect(*self.routes[e], self.rects[n])))

    def add_contact(self, i, j):
        key = tuple(sorted((i, j)))
        if key in self.hard['contact']:
            return
        self.hard['contact'].add(key)
        ei, ej = self.data['edges'][i], self.data['edges'][j]
        for e, f, ge, gf in [(i, j, ei, ej), (j, i, ej, ei)]:
            for k, n in enumerate([ge['u'], ge['v']]):
                p, (a, b) = self.routes[e][k], self.routes[f]
                alternatives = [no_point_contact(p, a, b)]
                for l, m in enumerate([gf['u'], gf['v']]):
                    if n == m:
                        q = self.routes[f][l]
                        alternatives.append(z3.And(p[0] == q[0], p[1] == q[1]))
                self.s.add(z3.Or(*alternatives))

    def materialize(self, model):
        def number(v):
            v = model.eval(v, model_completion=True)
            if z3.is_algebraic_value(v):
                v = v.approx(30)
            return v.numerator_as_long() / v.denominator_as_long() * SCALE
        layout = json.loads((HERE / 'baseline.layout.json').read_text())
        if self.args.mode == 'baseline' and self.args.ports == 'band':
            # All variables were constrained to these exact rational input
            # decimals. Preserve literal coordinates rather than re-rounding.
            return layout
        for i in self.selected:
            r = self.rects[i]
            layout['nodes'][i]['position'] = {'x': number(r[0]), 'y': number(r[1])}
        for e in self.active:
            layout['routedEdges'][e]['points'] = [{'x': number(p[0]), 'y': number(p[1])} for p in self.routes[e]]
        return layout


def check_primitives(data):
    began = time.monotonic()
    nodes, edges = data['nodes'], data['edges']
    crosses = [(i, j) for i, e in enumerate(edges) for j in range(i + 1, len(edges)) if numeric_cross(*e['p'], *edges[j]['p'])]
    hits = [(i, n) for i, e in enumerate(edges) for n, rect in enumerate(nodes) if n not in [e['u'], e['v']] and numeric_hit(*e['p'], rect)]
    assert crosses == [tuple(p) for p in data['crossings']]
    assert set(hits) == {tuple(p) for p in data['hits']}
    # Check symbolic predicates on nonparallel crossings, no crossing,
    # tangent/open-boundary contact, interior traversal, and non-crossing
    # diagonal SAT separating axes. These target actual modeling failure modes.
    pt = lambda x, y: (rational(x), rational(y))
    cases = [
        (separated_segments(pt(0, 0), pt(4, 4), pt(0, 4), pt(4, 0)), False),
        (separated_segments(pt(0, 0), pt(4, 0), pt(0, 2), pt(4, 2)), True),
        (clear_rect(pt(-1, 0), pt(3, 0), (0, 0, 2, 2), pad=0), True),
        (clear_rect(pt(-1, 1), pt(3, 1), (0, 0, 2, 2), pad=0), False),
        (clear_rect(pt(-1, 0), pt(0, -1), (0, 0, 2, 2), pad=0), True),
        (clear_rect(pt(.5, .5), pt(1, 1), (0, 0, 2, 2), pad=0), False),
    ]
    for f, expected in cases:
        assert z3.is_true(z3.simplify(f)) == expected
    # A geometrical witness requires both axes to change and an endpoint to
    # move from the top edge to the right edge. This is a model capability
    # check on two artificial cards, not a Captain improvement.
    x, y, px, py = z3.Reals('test_x test_y test_px test_py')
    s = z3.SolverFor('QF_NRA')
    s.set(timeout=5000)
    s.add(x >= 3, x <= 4, y >= 3, y <= 4,
          boundary_exit((px, py), pt(10, 4), (x, y, 2, 2)), px == x + 2, py > y, py < y + 2,
          separated_cards((x, y, 2, 2), (10, 3, 2, 2)))
    assert s.check() == z3.sat
    result = {'z3': z3.get_full_version(), 'crossings': len(crosses), 'hits': len(hits),
              'symbolicCases': len(cases), 'bothAxesAndSideWitness': str(s.model()),
              'elapsedSeconds': time.monotonic() - began, 'passed': True}
    write_json(HERE / 'primitive-check.json', result)
    print(json.dumps(result), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', choices=['check', 'baseline', 'target'], required=True)
    parser.add_argument('--group', default='coverage16-e140')
    parser.add_argument('--group-file')
    parser.add_argument('--prefix', default='joint16')
    parser.add_argument('--seconds', type=float, default=90)
    parser.add_argument('--radius', type=float, default=0, help='0 permits the whole 38720 square')
    parser.add_argument('--ports', choices=['band', 'perimeter', 'exact-cartesian'], default='band')
    parser.add_argument('--engine', choices=['nlsat', 'smt'], default='nlsat')
    parser.add_argument('--phase', choices=[0, 1], type=int, help='SMT Boolean phase preference; changes no geometric constraint')
    parser.add_argument('--all-adjacent', action='store_true', help='Preload every affected shared-card edge pair as a hard noncrossing constraint')
    parser.add_argument('--no-formula-dump', action='store_true', help='Avoid pre-solve pretty-printer memory; source, inputs and assertion digest still saved')
    parser.add_argument('--feedback', action='append')
    parser.add_argument('--pin-file', help='Exact saved solver assignment, for feedback regression checks only')
    parser.add_argument('--allow-modeled-conflicts', action='store_true', help='Geometry-only regression drill; never an improvement search')
    parser.add_argument('--target-cross', type=int, default=1302)
    parser.add_argument('--target-visual', type=int, default=1718)
    args = parser.parse_args()
    z3.set_param('memory_max_size', 160)
    data = json.loads((HERE / 'input.json').read_text())
    if args.mode == 'check':
        check_primitives(data)
        return
    group = json.loads(Path(args.group_file).read_text()) if args.group_file else next(g for g in data['groups'] if g['name'] == args.group)
    assert group['affectedCross'] >= 1402 - args.target_cross or args.mode == 'baseline'
    began = time.monotonic()
    result = {'command': sys.argv, 'version': z3.get_full_version(), 'group': group,
              'baselineSha256': data['baselineSha256'], 'sourceSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'radius': args.radius, 'timeLimitSeconds': args.seconds, 'z3MemoryLimitMiB': 160,
              'portRepresentation': args.ports,
              'engine': args.engine,
              'phase': args.phase,
              'allAdjacentPreloaded': args.all_adjacent,
              'pinFile': args.pin_file,
              'geometryOnlyDrill': args.allow_modeled_conflicts,
              'targetCross': args.target_cross, 'targetVisual': args.target_visual, 'mode': args.mode,
              'status': 'building', 'productVerified': False, 'fullGeometryConstraintsLoaded': False,
              'geometrySemantics': 'ideal real proper crossings and open padded rectangles; current product audit is authoritative'}
    file = HERE / (args.prefix + '.result.json')
    write_json(file, result)
    try:
        joint = JointModel(data, group, args)
        result.update(buildSeconds=time.monotonic() - began, realVariables=sum(z3.is_real(v) for v,_ in joint.initial),
                      boundaryChoiceBooleans=sum(z3.is_bool(v) for v,_ in joint.initial),
                      modeledCrossPairs=len(joint.costs['cross']), modeledHits=len(joint.costs['hit']),
                      intervalFixedCrosses=joint.interval_fixed_crosses,
                      hardConstraints={k: len(v) for k, v in joint.hard.items()}, assertions=len(joint.s.assertions()), status='solving')
        result['z3AllocatedMiBAfterBuild'] = z3.z3core.Z3_get_estimated_alloc_size() / 1024**2
        result['assertionDigest'] = hashlib.sha256(json.dumps([f.hash() for f in joint.s.assertions()]).encode()).hexdigest()
        # A reproducible initial formula and hashes are saved before the call.
        if not args.no_formula_dump:
            formula = joint.s.sexpr()
            formula_path = HERE / (args.prefix + '.smt2')
            formula_path.write_text(formula + '\n(check-sat)\n')
            result['formulaSha256'] = hashlib.sha256(formula_path.read_bytes()).hexdigest()
            result['formulaBytes'] = formula_path.stat().st_size
            del formula
        result['z3AllocatedMiBBeforeSolve'] = z3.z3core.Z3_get_estimated_alloc_size() / 1024**2
        result['formulaDumped'] = not args.no_formula_dump
        write_json(file, result)
        print(json.dumps({k: v for k, v in result.items() if k not in ['group', 'command']}), flush=True)
        started = time.monotonic()
        status = joint.s.check()
        result.update(status=str(status), solveSeconds=time.monotonic() - started, reasonUnknown=joint.s.reason_unknown(),
                      statistics={k: v for k, v in joint.s.statistics()})
        if status == z3.sat:
            model = joint.s.model()
            candidate = HERE / (args.prefix + '.layout.json')
            write_json(candidate, joint.materialize(model))
            pins = '\n'.join('(assert ' + (v == model.eval(v, model_completion=True)).sexpr() + ')' for v, _ in joint.initial)
            (HERE / (args.prefix + '.pins.smt2')).write_text(pins + '\n')
            flags = {kind: sum(z3.is_true(model.eval(b, model_completion=True)) for b in costs.values()) for kind, costs in joint.costs.items()}
            result.update(candidate=str(candidate.relative_to(ROOT)), modeledFlags=flags,
                          optimisticCross=group['immutableCross'] + flags['cross'],
                          optimisticVisual=group['immutableVisual'] + sum(flags.values()))
    except z3.Z3Exception as e:
        result.update(status='solver-error', error=str(e))
    result['elapsedSeconds'] = time.monotonic() - began
    write_json(file, result)
    print(json.dumps({k: v for k, v in result.items() if k not in ['group', 'command', 'statistics']}), flush=True)


if __name__ == '__main__':
    main()
