"""Witnessed shared catalogs for the 152-piece spherical disk arrangement.

The planar exterior is a geometric realization; rules remain spherical.
"""
from .icosahedron_common import SphericalPiece, Placed, apply, transpose
from ..environment import data_file
import json
import math
from pathlib import Path

from .icosahedron_geometry import AXES, TAU, Arrangement, add, sub, scale, dot, cross, norm, unit
from .icosahedron_model import ALPHA, IDENTITY, GROUP, LABELS, STOPS, DELTAS, compose, rotation, key, canonical

H = 1/math.sqrt(3)
RADIUS = math.acos(H)
A = Arrangement(RADIUS)

class Piece(SphericalPiece):
    def __init__(self, face):
        super().__init__(face, A, H)


PIECES = [Piece(f) for f in A.faces]


class Model:
    kinds = ('wing', 'petal', 'corner', 'face')

    def __init__(self):
        remaining = PIECES.copy()
        self.prototypes = {}
        orbits = {}
        for kind in self.kinds:
            prototype = remaining[0]
            self.prototypes[kind] = prototype
            orbit = {A.mask(apply(g, prototype.center)) for g in GROUP}
            orbits[kind] = orbit
            remaining = [p for p in remaining if p.mask not in orbit]
        assert not remaining
        assert [len(orbits[k]) for k in self.kinds] == [60, 60, 12, 20]
        self.pieces = []
        for face in A.faces:
            kind = next(k for k in self.kinds if face['mask'] in orbits[k])
            prototype = self.prototypes[kind]
            home = next(g for g in GROUP if A.mask(apply(g, prototype.center)) == face['mask'])
            caps = [i for i in range(20) if face['mask'] & (1 << i)]
            self.pieces.append(dict(id=kind+'/'+('-'.join(LABELS[i] for i in caps)), kind=kind, frame=home, caps=caps))
        self.pairs = [[(s,t) for s,a in enumerate(STOPS) for t,b in enumerate(STOPS) if abs(math.remainder(b-a-delta,TAU)) < 1e-8] for delta in DELTAS]
        self.witness = json.loads(data_file('icosahedron-152-witness.json').read_text())
        seen = {kind: {} for kind in self.kinds}
        cache = {}
        state = [p['frame'] for p in self.pieces]
        def collect():
            for piece, r in zip(self.pieces, state):
                raw = key(r)
                if raw not in cache: cache[raw] = canonical(r)
                ck, cr = cache[raw]
                seen[piece['kind']].setdefault(ck, cr)
        collect()
        for axis, d in self.witness:
            roles = [self.prototypes[p['kind']].role(r, AXES[axis]) for p,r in zip(self.pieces,state)]
            assert 2 not in roles, 'Invalid catalog witness: the proposed cut crosses a piece'
            rot = rotation(AXES[axis], DELTAS[d])
            state = [compose(rot,r) if role == 1 else r for r,role in zip(state,roles)]
            collect()
        assert [len(seen[k]) for k in self.kinds] == [12, 8, 16, 12]
        self.poses = {}; self.lookups = {}; self.bins = {}
        for kind in self.kinds:
            poses = {key(compose(g,r)): compose(g,r) for r in seen[kind].values() for g in GROUP}
            self.poses[kind] = [poses[k] for k in sorted(poses)]
            self.lookups[kind] = {key(r): i for i,r in enumerate(self.poses[kind])}
            self.bins[kind] = {}
            for i,r in enumerate(self.poses[kind]):
                b = tuple(math.floor(x/1e-5) for x in r[0])
                self.bins[kind].setdefault(b, []).append(i)
        assert [len(self.poses[k]) for k in self.kinds] == [720,480,960,720]
        for piece in self.pieces: piece['home'] = self.index(piece['kind'],piece['frame'])
        self.roles = {kind: [[self.prototypes[kind].role(r,n) for n in AXES] for r in self.poses[kind]] for kind in self.kinds}
        self.maps = {}
        for axis,n in enumerate(AXES):
            for d,delta in enumerate(DELTAS):
                rot = rotation(n,delta)
                self.maps[axis,d] = {}
                for kind in self.kinds:
                    transport = {}
                    for q,r in enumerate(self.poses[kind]):
                        if self.roles[kind][q][axis] != 1: continue
                        target = self.index(kind,compose(rot,r))
                        if target is not None: transport[q] = target
                    self.maps[axis,d][kind] = transport
        self.locations = {}; self.location_of = {}; self.geometry = []
        for kind in self.kinds:
            prototype = self.prototypes[kind]
            stabilizer = [g for g in GROUP if A.mask(apply(g,prototype.center)) == prototype.mask]
            locations = {}
            for q,r in enumerate(self.poses[kind]):
                shape = min(key(compose(r,g)) for g in stabilizer)
                locations.setdefault(shape,[]).append(q)
            self.locations[kind] = list(locations.values())
            self.location_of[kind] = {q:i for i,qs in enumerate(self.locations[kind]) for q in qs}
            for i,qs in enumerate(self.locations[kind]):
                self.geometry.append((kind,i,prototype.placed(self.poses[kind][qs[0]])))
        assert [len(self.locations[k]) for k in self.kinds] == [720,480,192,240]
        self.conflicts = []
        for i,(kind,location,p) in enumerate(self.geometry):
            for other_kind,other_location,q in self.geometry[:i]:
                if p.overlaps(q): self.conflicts.append(((kind,location),(other_kind,other_location)))

    def index(self,kind,r):
        found = self.lookups[kind].get(key(r))
        if found is not None: return found
        from itertools import product
        b = tuple(math.floor(x/1e-5) for x in r[0])
        for offsets in product((-1,0,1),repeat=3):
            for i in self.bins[kind].get(tuple(a+d for a,d in zip(b,offsets)),[]):
                q = self.poses[kind][i]
                if max(abs(a-b) for x,y in zip(r,q) for a,b in zip(x,y)) < 1e-6: return i
        return None

    def initial(self): return [p['home'] for p in self.pieces]

    def available(self,state):
        return [i for i in range(20) if all(self.roles[p['kind']][q][i] != 2 for p,q in zip(self.pieces,state))]

    def turn(self,state,axis,d):
        if axis not in self.available(state): return None
        result = []
        for p,q in zip(self.pieces,state):
            if self.roles[p['kind']][q][axis] != 1: result.append(q); continue
            target = self.maps[axis,d][p['kind']].get(q)
            if target is None: return None
            result.append(target)
        return result
