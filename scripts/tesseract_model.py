"""Exact spherical 3^4 placements and cell turns; standard library only."""
from itertools import combinations, permutations, product
import math

ID = 'tesseract3'
DEPTH = 1/3
CELLS = tuple(s*i for i in range(1, 5) for s in (1, -1))
PALETTE = ('#e76561', '#eea76d', '#f1eee3', '#f1cf67', '#66c7a0', '#7fa5ed', '#c48be0', '#56cbd5')


def sign(v): return 1 if v > 0 else -1
def label(c): return 'XYZW'[abs(c)-1]+('p' if c > 0 else 'm')
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def unit(v): return tuple(x/math.sqrt(dot(v, v)) for x in v)
def determinant(g):
    return (-1)**sum(abs(g[i]) > abs(g[j]) for i in range(4) for j in range(i+1, 4))*math.prod(map(sign, g))
def image(g, a): return sign(a)*g[abs(a)-1]
def compose(g, h): return tuple(image(g, a) for a in h)
def basis(c): return tuple(float(i == abs(c)-1)*sign(c) for i in range(4))
def frame(g): return [x for c in g for x in basis(c)]
def cross4(a, b, c):
    result = []
    for j in range(4):
        rows = [i for i in range(4) if i != j]
        value = sum((-1)**sum(p[i] > p[k] for i in range(3) for k in range(i+1, 3))*
                    a[rows[p[0]]]*b[rows[p[1]]]*c[rows[p[2]]] for p in permutations(range(3)))
        result.append((-1)**(j+1)*value)
    return tuple(result)
def plane(c, axis):
    n = basis(c)
    e = min((basis(i) for i in range(1, 5)), key=lambda e: abs(dot(e, n))+abs(dot(e, axis)))
    u = unit(tuple(e[i]-dot(e, n)*n[i]-dot(e, axis)*axis[i] for i in range(4)))
    return u, unit(cross4(n, axis, u))
def rotation(u, v, angle):
    c, s = math.cos(angle)-1, math.sin(angle)
    return [[float(i == j)+c*(u[i]*u[j]+v[i]*v[j])+s*(v[i]*u[j]-u[i]*v[j])
             for i in range(4)] for j in range(4)]


class Model:
    def __init__(self):
        self.group = sorted((tuple(s*p for s, p in zip(ss, pp))
                            for pp in permutations(range(1, 5)) for ss in product((-1, 1), repeat=4)
                            if determinant(tuple(s*p for s, p in zip(ss, pp))) == 1),
                            key=lambda g: (g != (1, 2, 3, 4), g))
        assert len(self.group) == 192
        self.moves, self.groups = [], []
        for c in CELLS:
            categories = {kind: [] for kind in ('face', 'edge', 'vertex')}
            for a in product((-1, 0, 1), repeat=4):
                if a[abs(c)-1] or not any(a) or next(x for x in a if x) < 0:
                    continue
                k = sum(x != 0 for x in a)
                kind = ('face', 'edge', 'vertex')[k-1]
                axis_id = kind[0]+''.join('XYZW'[i]+('m' if v < 0 else '') for i, v in enumerate(a) if v)
                axis = unit(a); u, v = plane(c, axis)
                angles = {1: (90, -90, 180), 2: (180,), 3: (120, -120)}[k]
                turns = []
                for degrees in angles:
                    name = label(c)+'_'+axis_id+('_inv' if degrees < 0 else '_2' if degrees == 180 and k == 1 else '')
                    cols = rotation(u, v, math.radians(degrees))
                    assert all(abs(x-round(x)) < 1e-12 for col in cols for x in col)
                    g = tuple((next(i for i, x in enumerate(col) if abs(x) > .5)+1)*
                              sign(next(x for x in col if abs(x) > .5)) for col in cols)
                    assert g in self.group and image(g, c) == c
                    move = dict(id=name, cell=c, group=label(c), kind=kind, axis=axis,
                                planeU=u, planeV=v, angle=math.radians(degrees), matrix=g)
                    self.moves.append(move)
                    turns.append(dict(operation=name, angleDegrees=degrees))
                categories[kind].append(dict(id=axis_id, label=' / '.join(
                    ('+' if x > 0 else '−')+'XYZW'[i] for i, x in enumerate(a) if x),
                    direction=axis, turns=turns))
            self.groups.append(dict(id=label(c), label=('+' if c > 0 else '−')+'XYZW'[abs(c)-1],
                                    center=basis(c), categories=[dict(kind=k, axes=v) for k, v in categories.items()]))
        for move in self.moves:
            move['inverse'] = next(m['id'] for m in self.moves if m['cell'] == move['cell'] and
                                   compose(m['matrix'], move['matrix']) == (1, 2, 3, 4))
        assert len(self.moves) == 184
        self.domains = []
        for c in CELLS:
            g = next(g for g in self.group if g[0] == c)
            self.domains.append(dict(id='center-'+label(c), k=1, poses=[(c,)], frames=[g]))
        for k, name in ((2, 'face'), (3, 'edge'), (4, 'vertex')):
            representatives = {}
            for g in self.group: representatives.setdefault(g[:k], g)
            poses = sorted(representatives)
            self.domains.append(dict(id=name, k=k, poses=poses, frames=[representatives[p] for p in poses]))
        for domain in self.domains:
            domain['index'] = {p: q for q, p in enumerate(domain['poses'])}
            slots = {}
            for q, p in enumerate(domain['poses']): slots.setdefault(frozenset(p), q)
            domain['pieces'] = [dict(id=('center' if domain['k'] == 1 else domain['id'])+'/'+
                ''.join(label(c) for c in sorted(slot, key=abs)), home=q) for slot, q in slots.items()]
        assert sum(len(d['pieces']) for d in self.domains) == 80
        assert sum(len(d['poses']) for d in self.domains) == 440

    def table(self, domain, move):
        return {q: domain['index'][tuple(image(move['matrix'], c) for c in pose)]
                for q, pose in enumerate(domain['poses']) if move['cell'] in pose}

    def actions(self):
        result = []
        for g in self.group:
            action, offset = [], 0
            for d in self.domains:
                action += [offset+(q if d['k'] == 1 else d['index'][tuple(image(g, c) for c in pose)])
                           for q, pose in enumerate(d['poses'])]
                offset += len(d['poses'])
            result.append(action)
        return result
