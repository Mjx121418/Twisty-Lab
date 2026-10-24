#!/usr/bin/env python3
"""Fixed Melty A–D authoring models; no dependency on the research workspace.

The reviewed finite catalog stores analytic construction recipes, rather than
rounded rotation matrices. Generation reconstructs the rays and every rotation,
checks separation before any numerical lookup, and checks every solid vertex.
Tolerances are authoring checks, never runtime geometric rules. The catalog's
reachability evidence is numerical, not a formal completeness theorem.
"""
from collections import defaultdict
from itertools import combinations, permutations, product
import json
import math
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
OPPOSITE = (5, 3, 4, 1, 2, 0)
LABELS = ('U', 'R', 'F', 'L', 'B', 'D')
TOL = 1e-9
PLACEMENTS = dict(A=1466, B=1208, C=1692, D=2020)
WITNESSED = dict(A=1466, B=1208, C=1682, D=2020)
PHASE_COUNTS = dict(A=[7, 7, 7, 5, 5, 5], B=[7, 7, 5, 3, 5, 3],
                    C=[7, 5, 5, 7, 7, 5], D=[7, 7, 7, 7, 7, 7])


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def add(a, b):
    return tuple(x+y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


def scale(k, a):
    return tuple(k*x for x in a)


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def unit(a):
    return scale(1/math.sqrt(dot(a, a)), a)


def rotation(axis, angle, p):
    c, s = math.cos(angle), math.sin(angle)
    return add(add(scale(c, p), scale(s, cross(axis, p))), scale((1-c)*dot(axis, p), axis))


def reflect_third(p, a, b):
    # If p.a = p.b = c, its projection onto span(a,b) is c/(1+a.b)*(a+b).
    c = dot(p, a)
    assert abs(c-dot(p, b)) < 1e-12
    return sub(scale(2*c/(1+dot(a, b)), add(a, b)), p)


def rays(family, degrees):
    c = math.cos(math.radians(degrees))
    s = math.sqrt(1-c*c)
    x = (c-c*c)/s
    u, r, f = (0., 0., 1.), (s, 0., c), (x, math.sqrt(1-c*c-x*x), c)
    l, b = reflect_third(r, u, f), reflect_third(f, u, r)
    if family == 'A':
        d = reflect_third(u, r, f)
    elif family == 'B':
        d = reflect_third(u, r, b)
    elif family == 'C':
        d = reflect_third(u, l, b)
    elif family == 'D':
        axis = unit(add(add(u, r), f))
        h = dot(u, axis)
        radial = unit(sub(u, scale(h, axis)))
        azimuth = math.acos((c+h*h)/(1-h*h))
        l = add(scale(-h, axis), scale(math.sqrt(1-h*h), rotation(axis, -azimuth, radial)))
        b = rotation(axis, math.tau/3, l)
        d = rotation(axis, -math.tau/3, l)
    else:
        raise ValueError(family)
    result = [u, r, f, l, b, d]
    assert all(abs(dot(v, v)-1) < 1e-12 for v in result)
    return result


def intersection(a, b, c):
    u, v, w = a[0], b[0], c[0]
    determinant = dot(u, cross(v, w))
    if abs(determinant) < 1e-12:
        return None
    return scale(1/determinant, add(add(scale(a[1], cross(v, w)), scale(b[1], cross(w, u))),
                                    scale(c[1], cross(u, v))))


def polyhedron(planes):
    vertices = []
    for triple in combinations(planes, 3):
        p = intersection(*triple)
        if p is not None and all(dot(n, p) <= h+TOL for n, h in planes):
            if not any(math.dist(p, q) < 1e-8 for q in vertices):
                vertices.append(p)
    if len(vertices) < 4:
        return None
    center = scale(1/len(vertices), tuple(sum(p[i] for p in vertices) for i in range(3)))
    faces, volume = [], 0.
    for index, (n, h) in enumerate(planes):
        points = [p for p in vertices if abs(dot(n, p)-h) < 1e-8]
        if len(points) < 3:
            continue
        normal = unit(n)
        middle = scale(1/len(points), tuple(sum(p[i] for p in points) for i in range(3)))
        a = unit(sub(points[0], middle))
        b = cross(normal, a)
        points.sort(key=lambda p: math.atan2(dot(sub(p, middle), b), dot(sub(p, middle), a)))
        area = sum(dot(normal, cross(sub(points[i], middle), sub(points[(i+1)%len(points)], middle)))
                   for i in range(len(points)))/2
        volume += area*(h-dot(n, center))/math.sqrt(dot(n, n))/3
        faces.append(dict(plane=index, vertices=points))
    if volume < 1e-10:
        return None
    return dict(vertices=vertices, faces=faces, volume=volume)


def partition(axes, depth, shell):
    if shell == 'cube':
        exterior = [(tuple(float(s) if i == j else 0. for i in range(3)), 1.)
                    for j in range(3) for s in (-1, 1)]
    else:
        exterior = [(n, 1.) for n in axes]
    whole = polyhedron(exterior)
    assert whole is not None
    cells = []
    for signs in product((-1, 1), repeat=6):
        planes = exterior+[(scale(sign, n), sign*depth) for sign, n in zip(signs, axes)]
        # sign +1 is the inner halfspace, sign -1 the outer cap.
        cell = polyhedron(planes)
        if cell is not None:
            cell['caps'] = [i for i, sign in enumerate(signs) if sign == -1]
            cell['ports'] = [face['plane'] for face in cell['faces'] if face['plane'] < 6]
            cells.append(cell)
    assert abs(sum(cell['volume'] for cell in cells)-whole['volume']) < 1e-8
    return cells, whole['volume']


def role(vertices, axis, depth, tolerance=TOL):
    values = [dot(axis, v)-depth for v in vertices]
    if min(values) < -tolerance and max(values) > tolerance:
        return 'blocked'
    return 'moving' if min(values) >= -tolerance else 'stationary'


def mul(a,b):
    w,x,y,z=a;v,s,t,u=b
    q=(w*v-x*s-y*t-z*u,w*s+x*v+y*u-z*t,w*t-x*u+y*v+z*s,w*u+x*t-y*s+z*v)
    length=math.sqrt(sum(x*x for x in q))
    return tuple(x/length for x in q)


def apply(q,v):
    w,*x=q;t=scale(2,cross(x,v))
    return add(add(v,scale(w,t)),cross(x,t))


def turn(axis,angle):
    return (math.cos(angle/2),*scale(math.sin(angle/2),axis))


def key(q):
    sign=next(1 if x>0 else -1 for x in q if abs(x)>1e-6)
    return tuple(round(sign*x,7) for x in q)


def map_pair(a,b,c,d):
    cosine=dot(a,c)
    if cosine<-1+1e-12:
        perpendicular=unit(cross(a,(1,0,0) if abs(a[0])<.8 else (0,1,0)))
        q=turn(perpendicular,math.pi)
    else:
        raw=(1+cosine,*cross(a,c));norm=math.sqrt(sum(x*x for x in raw))
        q=tuple(x/norm for x in raw)
    moved=apply(q,b)
    angle=math.atan2(dot(c,cross(moved,d)),dot(moved,d)-dot(c,moved)*dot(c,d))
    result=mul(turn(c,angle),q)
    assert math.dist(apply(result,a),c)<1e-12 and math.dist(apply(result,b),d)<1e-12
    return result


def phases(axes,i):
    result=[0.]
    for j,k in permutations(range(6),2):
        if i in (j,k) or abs(dot(axes[i],axes[j])-dot(axes[i],axes[k]))>1e-12:
            continue
        a=sub(axes[j],scale(dot(axes[i],axes[j]),axes[i]))
        b=sub(axes[k],scale(dot(axes[i],axes[k]),axes[i]))
        angle=math.atan2(dot(axes[i],cross(a,b)),dot(a,b))%math.tau
        if not any(abs((angle-t+math.pi)%math.tau-math.pi)<1e-12 for t in result):result.append(angle)
    return sorted(result)


def signed(angle):
    return (angle + math.pi) % math.tau - math.pi


def frame(q):
    columns = [apply(q, tuple(float(i == j) for i in range(3))) for j in range(3)]
    return [v for col in columns for v in (*col, 0.)] + [0., 0., 0., 1.]


class Model:
    def __init__(self, family='A'):
        self.family = family
        self.id = 'melty-' + family.lower()
        seed = json.loads((ROOT / f'scripts/data/{self.id}-catalog.json').read_text())
        assert (seed['formatVersion'], seed['family'], seed['angleDegrees'], seed['cutDepth']) == (1, family, 85, '1/3')
        self.axes = rays(family, 85)
        # In the radius-sqrt(3) ball containing every rotated cube piece, the
        # opposite caps are disjoint throughout simultaneous continuous turns.
        assert all(2/9/(1+dot(self.axes[i], self.axes[OPPOSITE[i]])) > 3+1e-9 for i in range(3))
        self.phases = [phases(self.axes, i) for i in range(6)]
        assert list(map(len, self.phases)) == PHASE_COUNTS[family]
        self.cells, volume = partition(self.axes, 1/3, 'cube')
        assert len(self.cells) == 27 and abs(volume - 8) < 1e-10
        self.rotations = []
        for rotation in seed['rotations']:
            recipe = rotation['recipe']
            if recipe[0] == 'identity':
                q = (1., 0., 0., 0.)
            elif recipe[0] == 'phase':
                _, i, s = recipe
                q = turn(self.axes[i], self.phases[i][s])
            else:
                _, a, b, i, j, s, t = recipe
                assert abs(dot(self.axes[a], self.axes[b]) - dot(self.axes[i], self.axes[j])) < 1e-13
                q = mul(turn(self.axes[i], self.phases[i][t] - self.phases[i][s]),
                        map_pair(self.axes[a], self.axes[b], self.axes[i], self.axes[j]))
            self.rotations.append(q)
        # No small rounded-key collision may silently merge catalog identities.
        assert len({key(q) for q in self.rotations}) == len(self.rotations)
        self.rotation_index = {key(q): i for i, q in enumerate(self.rotations)}
        self.pieces = seed['pieces']
        self.centers = [next(p for p, c in enumerate(self.cells) if c['caps'] == [i]) for i in range(6)]
        self.vertices, self.roles = [], []
        for piece, cell in zip(self.pieces, self.cells):
            assert piece['caps'] == cell['caps']
            assert piece['id'] == (''.join(LABELS[i] for i in cell['caps']) or 'core')
            vertices = [[apply(self.rotations[q['rotation']], v) for v in cell['vertices']]
                        for q in piece['placements']]
            self.vertices.append(vertices)
            self.roles.append([[role(vs, axis, 1/3) for axis in self.axes] for vs in vertices])
        assert sum(len(p['placements']) for p in self.pieces) == PLACEMENTS[family]
        self.deltas = []
        self.phase_pairs = []
        for i, angles in enumerate(self.phases):
            values = sorted({round(signed(b-a), 10) for a in angles for b in angles if a != b})
            deltas, pairs = [], []
            for value in values:
                match = [(s, t) for s, a in enumerate(angles) for t, b in enumerate(angles)
                         if abs(signed(b-a)-value) < 1e-9]
                s, t = match[0]
                deltas.append(signed(angles[t]-angles[s]))
                pairs.append(match)
            assert len(deltas) > 0 and len(deltas) % 2 == 0
            self.deltas.append(deltas)
            self.phase_pairs.append(pairs)
        self.maps = {}
        for i in range(6):
            for delta, angle in enumerate(self.deltas[i]):
                rotation = turn(self.axes[i], angle)
                tables = []
                for p, piece in enumerate(self.pieces):
                    allowed = {q['rotation']: a for a, q in enumerate(piece['placements'])}
                    table = {}
                    for a, pose in enumerate(piece['placements']):
                        if self.roles[p][a][i] != 'moving':
                            continue
                        q = mul(rotation, self.rotations[pose['rotation']])
                        r = self.rotation_index.get(key(q))
                        if r is None:
                            nearest, error = min(((b, min(math.dist(q, v), math.dist(q, scale(-1, v))))
                                                  for b, v in enumerate(self.rotations)), key=lambda x: x[1])
                            if error < 1e-11:
                                r = nearest
                            else:
                                assert error > 1e-6, ('Unresolved missing rotation', i, delta, p, a, error)
                                continue
                        if r not in allowed:
                            continue
                        error = min(math.dist(q, self.rotations[r]), math.dist(q, scale(-1, self.rotations[r])))
                        assert error < 1e-11, ('Unresolved rotation lookup', i, delta, p, a, error)
                        table[a] = allowed[r]
                    tables.append(table)
                self.maps[i, delta] = tables

    def delta_index(self, grip, source, target):
        angle = signed(self.phases[grip][target] - self.phases[grip][source])
        return next(d for d, value in enumerate(self.deltas[grip]) if abs(value-angle) < 1e-11)

    def macros(self):
        """Both single grips and disjoint opposite caps, with signed winding."""
        if hasattr(self, '_macros'):
            return self._macros
        candidates = ([((i, d),) for i in range(6) for d in range(len(self.deltas[i]))] +
                      [((i, d), (OPPOSITE[i], e)) for i in range(3)
                       for d in range(len(self.deltas[i])) for e in range(len(self.deltas[OPPOSITE[i]]))])
        # Exclude only macros for which even the centers cannot free a third
        # grip. This necessary test does not rely on sampled whole states.
        result = []
        for macro in candidates:
            i = macro[0][0]
            driven = {g for g, _ in macro}
            for k in range(6):
                if k in (i, OPPOSITE[i]):
                    continue
                if not all(any(self.roles[self.centers[g]][t][k] != 'blocked'
                               for _, t in self.phase_pairs[g][d]) for g, d in macro):
                    continue
                if not all(any(all(self.roles[p][a][g] != 'blocked' for g in driven | {k})
                               for a in range(len(self.roles[p])))
                           for g, p in enumerate(self.centers) if g not in driven):
                    continue
                result.append(macro)
                break
        # A completed handoff has an intersecting free cut at both ends. A
        # one-sided center test can admit dead directions in the larger ray
        # systems; retain only the bidirectional necessary condition.
        possible = set(result)
        inverse = lambda macro: tuple((i, next(e for e, value in enumerate(self.deltas[i])
                                               if abs(value+self.deltas[i][d]) < 1e-10)) for i, d in macro)
        self._macros = tuple(macro for macro in result if inverse(macro) in possible)
        return self._macros

    def table(self, macro, p):
        result = {}
        for i, d in macro:
            for a, b in self.maps[i, d][p].items():
                assert a not in result, 'Opposite caps must have disjoint participants'
                result[a] = b
        return result

    def conflicts(self):
        """Check complete convex solids, including the fixed internal core."""
        lines = ['27']
        for p, (piece, cell) in enumerate(zip(self.pieces, self.cells)):
            normals, edges = {}, {}
            for face in cell['faces']:
                vs = face['vertices']
                n = unit(cross(sub(vs[1], vs[0]), sub(vs[2], vs[0])))
                normals[tuple(round(x, 8) for x in n)] = n
                for a, b in zip(vs, vs[1:] + vs[:1]):
                    e = unit(sub(b, a))
                    edges[tuple(round(x, 8) for x in e)] = e
            lines.append(str(len(piece['placements'])))
            for pose in piece['placements']:
                q = self.rotations[pose['rotation']]
                for values in (cell['vertices'], list(normals.values()), list(edges.values())):
                    lines.append(str(len(values)))
                    lines.extend(' '.join(format(x, '.17g') for x in apply(q, v)) for v in values)
        output = subprocess.check_output([str(ROOT / 'build/native/melty_solids')],
                                         input='\n'.join(lines), text=True)
        result = defaultdict(list)
        for line in output.splitlines():
            p, a, r, b = map(int, line.split())
            result[p, a, r].append(b)
        return result

    def verify_witnesses(self):
        """Replay solved-state paths for the catalog's declared witnessed subset."""
        data = json.loads((ROOT / f'scripts/data/{self.id}-witnesses.json').read_text())
        states = []
        covered = set()
        by_node = defaultdict(list)
        for p, a, node in data['witnesses']:
            by_node[node].append((p, a))
        paired = 0
        for node, (parent, i, j, t, u) in enumerate(data['nodes']):
            if parent < 0:
                assert node == 0
                state = [0] * 27
            else:
                assert parent < node
                before = states[parent]
                macro = [(i, self.delta_index(i, before[self.centers[i]], t))]
                if j >= 0:
                    assert j == OPPOSITE[i]
                    macro.append((j, self.delta_index(j, before[self.centers[j]], u)))
                    paired += 1
                assert tuple(macro) in self.macros(), (node, 'missing directed request')
                state = before.copy()
                for p, a in enumerate(before):
                    for grip, delta in macro:
                        r = self.roles[p][a][grip]
                        assert r != 'blocked', (node, p, 'source cut')
                        if r == 'moving':
                            state[p] = self.maps[grip, delta][p][a]
                assert any(k not in (i, OPPOSITE[i]) and all(
                    self.roles[p][a][k] != 'blocked' for p, a in enumerate(state)) for k in range(6)), (node, 'handoff')
                assert all(not piece['caps'] or any(state[self.centers[k]] == s for k, s in piece['placements'][a]['phaseGuards'])
                           for piece, a in zip(self.pieces, state)), (node, 'phase guard')
            for p, a in by_node[node]:
                assert state[p] == a, (node, p, a)
                covered.add((p, a))
            states.append(state)
        expected = {(p, a) for p, piece in enumerate(self.pieces) for a, pose in enumerate(piece['placements'])
                    if pose.get('reachabilityWitnessed', True)}
        assert covered == expected and len(covered) == WITNESSED[self.family]
        return dict(placements=len(covered), nodes=len(states), pairedMoves=paired)


