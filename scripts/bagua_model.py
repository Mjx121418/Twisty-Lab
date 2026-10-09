#!/usr/bin/env python3
"""Author the experimental Bagua package using exact Q(sqrt(2)) arithmetic.

All coordinates live in this offline tool and separate realization documents.
The C++ core receives finite placement relations and capacity-one resources.
"""
import argparse
from dataclasses import dataclass
from fractions import Fraction as F
from functools import total_ordering
from itertools import combinations, permutations, product
from math import sqrt, gcd, lcm, pi, acos
from pathlib import Path
import json
import resource
import subprocess
import sys


@total_ordering
@dataclass(frozen=True)
class Q2:
    """Exact a + b*sqrt(2); floats are used only when exporting realizations."""

    a: F = F(0)
    b: F = F(0)

    def __init__(self, a=0, b=0):
        object.__setattr__(self, 'a', F(a))
        object.__setattr__(self, 'b', F(b))

    def __add__(self, other):
        other = asq(other)
        return Q2(self.a + other.a, self.b + other.b)

    __radd__ = __add__

    def __neg__(self):
        return Q2(-self.a, -self.b)

    def __sub__(self, other):
        return self + -asq(other)

    def __rsub__(self, other):
        return asq(other) + -self

    def __mul__(self, other):
        other = asq(other)
        return Q2(self.a * other.a + 2 * self.b * other.b,
                  self.a * other.b + self.b * other.a)

    __rmul__ = __mul__

    def __truediv__(self, other):
        other = asq(other)
        denominator = other.a * other.a - 2 * other.b * other.b
        return self * Q2(other.a / denominator, -other.b / denominator)

    def __rtruediv__(self, other):
        return asq(other) / self

    def __eq__(self, other):
        if not isinstance(other, (Q2, int, F)):
            return False
        other = asq(other)
        return self.a == other.a and self.b == other.b

    def __hash__(self):
        return hash(self.a) if not self.b else hash((self.a, self.b))

    def sign(self):
        a, b = self.a, self.b
        if not a:
            return (b > 0) - (b < 0)
        if not b or (a > 0) == (b > 0):
            return (a > 0) - (a < 0)
        difference = a * a - 2 * b * b
        return ((difference > 0) - (difference < 0)) * ((a > 0) - (a < 0))

    def __lt__(self, other):
        return (self - asq(other)).sign() < 0

    def __bool__(self):
        return bool(self.a or self.b)

    def __float__(self):
        return float(self.a) + float(self.b) * sqrt(2)


def asq(value):
    return value if isinstance(value, Q2) else Q2(value)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def det(matrix):
    return dot(matrix[0], cross(matrix[1], matrix[2]))


def apply(matrix, vector):
    return tuple(dot(row, vector) for row in matrix)


def tr(matrix):
    return tuple(zip(*matrix))


def mul(a, b):
    return tuple(tuple(dot(row, column) for column in tr(b)) for row in a)


def shape(matrix, polyhedron):
    return tuple(sorted(apply(matrix, vertex) for vertex in polyhedron))


I = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
h = Q2(F(9, 20))
c = h * Q2(0, 1)
# The 24 proper cube rotations generate solved copies of each prototype.
Gs = []
for permutation in permutations(range(3)):
    for signs in product([-1, 1], repeat=3):
        matrix = tuple(tuple(signs[i] if permutation[i] == j else 0 for j in range(3))
                       for i in range(3))
        if det(matrix) == 1:
            Gs.append(matrix)
# Positive 45-degree rotations about x, y and z, in Q(sqrt(2)).
Rs = []
for axis in range(3):
    j, k = (axis + 1) % 3, (axis + 2) % 3
    matrix = [[0, 0, 0] for _ in range(3)]
    matrix[axis][axis] = 1
    matrix[j][j] = matrix[k][k] = Q2(0, F(1, 2))
    matrix[j][k] = -Q2(0, F(1, 2))
    matrix[k][j] = Q2(0, F(1, 2))
    Rs.append(tuple(tuple(row) for row in matrix))


def vertices(planes):
    result = set()
    for (a, x), (b, y), (c, z) in combinations(planes, 3):
        denominator = dot(a, cross(b, c))
        if not denominator:
            continue
        vertex = tuple((x * p + y * q + z * r) / denominator
                       for p, q, r in zip(cross(b, c), cross(c, a), cross(a, b)))
        if all(dot(normal, vertex) <= offset for normal, offset in planes):
            result.add(vertex)
    return tuple(sorted(result))


def split(parts, plane):
    normal, offset = plane
    result = []
    for constraints, polyhedron in parts:
        lo = min(dot(normal, vertex) for vertex in polyhedron)
        hi = max(dot(normal, vertex) for vertex in polyhedron)
        if lo < offset < hi:
            for sign in [1, -1]:
                half = constraints + [(tuple(sign * x for x in normal), sign * offset)]
                divided = vertices(half)
                if len(divided) >= 4:
                    result.append((half, divided))
        else:
            # Keep the side of every cut, including redundant bounds. The
            # spherical interpretation extends the exterior faces afterward.
            sign = -1 if lo >= offset else 1
            result.append((constraints + [(tuple(sign * x for x in normal), sign * offset)], polyhedron))
    return result

ROOT = Path(__file__).resolve().parent.parent
FACES = {'U': (0, 1, 0), 'D': (0, -1, 0), 'R': (1, 0, 0),
         'L': (-1, 0, 0), 'F': (0, 0, 1), 'B': (0, 0, -1)}
KINDS = ['corner', 'center', 'triangle-center', 'triangle-edge',
         'sliver', 'kite-left', 'kite-right', 'edge']
REFERENCES = [
    'https://www.researchgate.net/publication/380721263_Discover_and_Design_Sun_Twisty_Puzzles',
    'https://twistypuzzles.com/forum/viewtopic.php?p=437998#p437998',
    'https://sites.google.com/site/easytutorial3x3x3/bagua',
    'https://spencerparkin.github.io/twisty-puzzle-solutions/puzzle_bagua.html',
]


def role(vs, axis):
    values = [dot(axis, v) for v in vs]
    if min(values) < h < max(values):
        return 2
    return 1 if min(values) >= h and max(values) > h else 0


# Integer pairs represent a + b*sqrt(2). Hull tests share one coordinate
# denominator, so their projection comparisons need no division or tolerance.
def pair_sign(v):
    a, b = v
    if not a:
        return (b > 0) - (b < 0)
    if not b or (a > 0) == (b > 0):
        return (a > 0) - (a < 0)
    d = a * a - 2 * b * b
    return ((d > 0) - (d < 0)) * ((a > 0) - (a < 0))


def pair_sub(a, b):
    return a[0] - b[0], a[1] - b[1]


def pair_mul(a, b):
    return a[0] * b[0] + 2 * a[1] * b[1], a[0] * b[1] + a[1] * b[0]


def pair_dot(a, b):
    terms = [pair_mul(x, y) for x, y in zip(a, b)]
    return sum(t[0] for t in terms), sum(t[1] for t in terms)


def direction(v):
    divisor = gcd(*(abs(x) for pair in v for x in pair))
    if not divisor:
        return None
    sign = next(pair_sign(p) for p in v if pair_sign(p))
    return tuple((a // divisor * sign, b // divisor * sign) for a, b in v)


def pair_cross(a, b):
    return direction(tuple(pair_sub(pair_mul(a[i], b[j]), pair_mul(a[j], b[i]))
                           for i, j in [(1, 2), (2, 0), (0, 1)]))


class Hull:
    def __init__(self, vs, scale):
        self.vertices = tuple(tuple((int(x.a * scale), int(x.b * scale)) for x in v) for v in vs)
        self.normals = set()
        self.edges = set()
        facets = set()
        for a, b, c in combinations(range(len(vs)), 3):
            edges = [tuple(pair_sub(x, y) for x, y in zip(self.vertices[i], self.vertices[a]))
                     for i in (b, c)]
            normal = pair_cross(*edges)
            if normal is None:
                continue
            projection = [pair_dot(normal, v) for v in self.vertices]
            signs = [pair_sign(pair_sub(p, projection[a])) for p in projection]
            if min(signs) < 0 < max(signs):
                continue
            self.normals.add(normal)
            facets.add(tuple(i for i, sign in enumerate(signs) if sign == 0))
        for i, j in combinations(range(len(vs)), 2):
            if sum(i in f and j in f for f in facets) >= 2:
                self.edges.add(direction(tuple(pair_sub(x, y) for x, y in
                                                zip(self.vertices[i], self.vertices[j]))))
        self.intervals = {}
        self.box = [self.interval(tuple((int(i == j), 0) for j in range(3))) for i in range(3)]

    def interval(self, axis):
        if axis not in self.intervals:
            values = [pair_dot(axis, v) for v in self.vertices]
            lo = hi = values[0]
            for p in values[1:]:
                if pair_sign(pair_sub(p, lo)) < 0:
                    lo = p
                if pair_sign(pair_sub(p, hi)) > 0:
                    hi = p
            # Cross-edge axes vary between pairs. An unbounded cache here
            # accumulates projections for the entire catalog and wastes RAM.
            if len(self.intervals) >= 16:
                self.intervals.pop(next(iter(self.intervals)))
            self.intervals[axis] = lo, hi
        return self.intervals[axis]

    @staticmethod
    def separated(a, b):
        return pair_sign(pair_sub(a[1], b[0])) <= 0 or pair_sign(pair_sub(b[1], a[0])) <= 0

    def overlaps(self, other):
        if any(self.separated(a, b) for a, b in zip(self.box, other.box)):
            return False
        checked = set()
        for axis in self.normals | other.normals:
            checked.add(axis)
            if self.separated(self.interval(axis), other.interval(axis)):
                return False
        for a in self.edges:
            for b in other.edges:
                axis = pair_cross(a, b)
                if axis is None or axis in checked:
                    continue
                checked.add(axis)
                if self.separated(self.interval(axis), other.interval(axis)):
                    return False
        return True


class AuthoringModel:
    def __init__(self):
        self.prototypes = []
        self.prototype_planes = []
        self.homes = []
        candidates = Gs + [mul(g, r) for g in Gs for r in Rs]
        # Construct representative center, edge, and corner blocks. Refine a
        # block only by the cuts of its incident face layers. Interior trims
        # leave room for the mechanism and prevent extra uncolored fragments.
        for cell in [(1, 1, 1), (0, 1, 0), (1, 0, 1)]:
            planes = []
            for i, x in enumerate(cell):
                lo, hi = (h, Q2(1)) if x else (-h, h)
                n = tuple(int(i == j) for j in range(3))
                planes.extend([(n, hi), (tuple(-x for x in n), -lo)])
            if sum(cell) == 1:
                planes.append(((0, -1, 0), Q2(F(-9, 10))))
            if sum(cell) == 2:
                planes.append(((-1, 0, -1), Q2(F(-27, 20))))
            parts = [(planes, vertices(planes))]
            for i, x in enumerate(cell):
                if not x:
                    continue
                j, k = [a for a in range(3) if a != i]
                for sj, sk in product((-1, 1), repeat=2):
                    n = tuple(sj if a == j else sk if a == k else 0 for a in range(3))
                    parts = split(parts, (n, c))
            assert len(parts) == {3: 1, 1: 5, 2: 9}[sum(cell)]
            for constraints, vs in parts:
                found = next(((kind, g) for kind, base in enumerate(self.prototypes)
                              for g in candidates if shape(g, base) == vs), None)
                if found is None:
                    found = len(self.prototypes), I
                    self.prototypes.append(vs)
                    self.prototype_planes.append(constraints)
                    self.homes.append({})
                kind, frame = found
                for g in Gs:
                    placed = shape(g, vs)
                    self.homes[kind].setdefault(placed, mul(g, frame))
        assert [len(h) for h in self.homes] == [8, 6, 24, 24, 24, 24, 24, 12]
        self.frames = []
        self.shapes = []
        for kind, prototype in enumerate(self.prototypes):
            catalog = set(self.homes[kind].values())
            frontier = list(sorted(catalog))
            for frame in frontier:
                vs = shape(frame, prototype)
                for axis, rotation in zip(FACES.values(), self.rotations()):
                    if role(vs, axis) != 1:
                        continue
                    target = mul(rotation, frame)
                    if target not in catalog:
                        catalog.add(target)
                        frontier.append(target)
                        if len(catalog) > 4096:
                            raise RuntimeError('The proposed finite catalog did not close.')
            self.frames.append(sorted(catalog))
            self.shapes.append([shape(g, prototype) for g in self.frames[-1]])
        self.geometry = sorted(set(vs for group in self.shapes for vs in group))
        self.geometry_ids = {vs: i for i, vs in enumerate(self.geometry)}
        self.keys = [{g: f'{kind}/q{i:04}' for i, g in enumerate(frames)}
                     for kind, frames in zip(KINDS, self.frames)]
        self.ports = []
        for prototype in self.prototypes:
            self.ports.append({str(i): (n, [j for j, v in enumerate(prototype) if dot(n, v) == 1])
                               for i, n in enumerate(n for n in FACES.values()
                                                     if sum(dot(n, v) == 1 for v in prototype) >= 3)})
        print('Bagua catalog:', dict(zip(KINDS, map(len, self.frames))), flush=True)

    @staticmethod
    def rotations():
        # Clockwise about each outward face normal; exact multiples of 45°.
        return [tr(Rs[1]), Rs[1], tr(Rs[0]), Rs[0], tr(Rs[2]), Rs[2]]

    def definition(self, exclusions=True):
        resources = [[f'location/{i:04}'] for i in range(len(self.geometry))]
        if exclusions:
            self.add_exclusions(resources)
        self.resources = resources
        domains, types, pieces = [], [], []
        for kind, name in enumerate(KINDS):
            entries = []
            for frame, vs in zip(self.frames[kind], self.shapes[kind]):
                location = self.geometry_ids[vs]
                ports = {port: {'cell': f'location/{location:04}', 'attachment': f'socket/{port}'}
                         for port in self.ports[kind]}
                entries.append({'key': self.keys[kind][frame], 'footprint': resources[location],
                                'portAttachment': ports})
            domains.append({'id': name, 'placements': entries})
            types.append({'id': name, 'placementDomainId': name, 'localPorts': list(self.ports[kind])})
            face_names = {n: label for label, n in FACES.items()}
            for i, (_, frame) in enumerate(sorted(self.homes[kind].items())):
                labels = {port: face_names[apply(frame, n)] for port, (n, _) in self.ports[kind].items()}
                pieces.append({'id': f'{name}/{i:02}', 'type': name,
                               'homePlacement': self.keys[kind][frame], 'portLabels': labels})
        operations = []
        for (face, axis), rotation in zip(FACES.items(), self.rotations()):
            for suffix, power, inverse in [('+', 1, '-'), ('-', 7, '+'), ('', 2, '_inv'),
                                            ('_inv', 6, ''), ('_half', 4, '_half')]:
                r = I
                for _ in range(power):
                    r = mul(rotation, r)
                rules = {}
                for kind, name in enumerate(KINDS):
                    blocked, transports = [], {}
                    for frame, vs in zip(self.frames[kind], self.shapes[kind]):
                        current = self.keys[kind][frame]
                        participation = role(vs, axis)
                        if participation == 2:
                            blocked.append(current)
                        elif participation == 1:
                            target = mul(r, frame)
                            assert target in self.keys[kind]
                            transports[current] = self.keys[kind][target]
                    rules[name] = {'blocked': blocked, 'transports': transports}
                operations.append({'id': face + suffix, 'inverse': face + inverse,
                                   'family': face, 'transport': f'turn/{face}/{power}',
                                   'placementRules': rules})
        self.operations = operations
        self.pieces = pieces
        return {'schemaVersion': 1, 'semanticsVersion': 'bagua-planar-v1',
                'kind': 'finite-definition', 'puzzleId': 'bagua-experimental-v1',
                'ruleModule': 'finite-placement-relations@1',
                'cells': sorted({r for rs in resources for r in rs}),
                'pieceTypes': types, 'pieces': pieces, 'placementDomains': domains,
                'operations': operations, 'goal': {'kind': 'home'},
                'initialState': {'placementOf': {p['id']: p['homePlacement'] for p in pieces},
                                 'mechanism': {}},
                'provenance': {'authoringTool': 'scripts/bagua_model.py', 'status': 'experimental',
                               'references': REFERENCES,
                               'labelledPlacements': dict(zip(KINDS, map(len, self.frames))),
                               'geometricLocations': len(self.geometry),
                               'overlappingLocationPairs': getattr(self, 'overlap_pairs', 0),
                               'occupancyExclusions': getattr(self, 'exclusion_count', 0)}}

    def add_exclusions(self, resources):
        scale = lcm(*(x.a.denominator for vs in self.geometry for v in vs for x in v),
                    *(x.b.denominator for vs in self.geometry for v in vs for x in v))
        hulls = [Hull(vs, scale) for vs in self.geometry]
        incompatible = [0] * len(hulls)
        pairs = 0
        for i, a in enumerate(hulls):
            for j in range(i + 1, len(hulls)):
                if a.overlaps(hulls[j]):
                    incompatible[i] |= 1 << j
                    incompatible[j] |= 1 << i
                    pairs += 1
            if i % 100 == 0:
                status = Path('/proc/self/status')
                if status.exists():
                    # getrusage can inherit an ancestor's pre-exec high-water mark.
                    peak = next(line.split()[1] for line in status.read_text().splitlines()
                                if line.startswith('VmHWM:'))
                    mib = int(peak) / 1024
                else:
                    peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
                    mib = peak / (1024 * 1024 if sys.platform == 'darwin' else 1024)
                print(f'Occupancy: {i}/{len(hulls)} locations, {pairs} overlaps; '
                      f'peak RSS {mib:.0f} MiB', flush=True)
        self.overlap_pairs = pairs
        # Cover incompatible pairs with complete cliques. One capacity-one
        # resource per clique is equivalent to pairwise exclusions and keeps
        # the portable package comfortably within its document/memory budget.
        remaining = incompatible.copy()
        exclusions = 0
        for i in range(len(hulls)):
            while remaining[i]:
                members = [i]
                candidates = remaining[i]
                while candidates:
                    bit = candidates & -candidates
                    j = bit.bit_length() - 1
                    members.append(j)
                    candidates &= incompatible[j]
                token = f'exclusion/{exclusions:05}'
                for a in members:
                    resources[a].append(token)
                    for b in members:
                        remaining[a] &= ~(1 << b)
                exclusions += 1
        assert not any(remaining)
        self.exclusion_count = exclusions
        print(f'Occupancy: {pairs} exact overlaps, {exclusions} resources', flush=True)

    def realizations(self, digest):
        flat = lambda g: [float(x) for row in g for x in row]
        models = {name: {'vertices': [[float(x) for x in v] for v in self.prototypes[kind]],
                         'ports': {port: indices for port, (_, indices) in self.ports[kind].items()},
                         'symmetries': [flat(I)]} for kind, name in enumerate(KINDS)}
        tracks = {}
        for op in self.operations:
            power = int(op['transport'].rsplit('/', 1)[1])
            tracks[op['id']] = {'axis': list(FACES[op['family']]), 'fromStop': 0, 'toStop': power}
        common = {'schemaVersion': 1, 'compatibleDefinitionDigest': digest,
                  'requiredCapabilities': ['triangle-meshes', 'rigid-transforms'],
                  'models': models, 'modelByDomain': {name: name for name in KINDS},
                  'placementFrames': {self.keys[kind][g]: flat(g) for kind in range(len(KINDS))
                                      for g in self.frames[kind]},
                  'operationTracks': tracks, 'angularStops': [i * pi / 4 for i in range(8)],
                  'scale': 1.4, 'bodyInset': 0.025, 'stickerInset': 0.06, 'stickerLift': 0.0015,
                  'fidelity': 'experimental-planar-mechanism'}
        documents = {f'bagua-{kind}.json': {**common, 'id': f'bagua-{kind}-v1',
                                          'kind': f'polyhedral-{kind}'}
                     for kind in ['euclidean', 'port-diagram']}
        # Sample a sphere of radius 10/9 in the original cut coordinates, and
        # extend the cube's exterior faces to its tangent planes. Normalize
        # the result to the unit sphere: cuts become 81/200, center trims
        # 81/100, and edge trims x+z >= 243/200. The omitted core fits
        # strictly inside S², so those interior trims have no surface boundary.
        factor = Q2(F(9, 10))
        trims = {((0, -1, 0), Q2(F(-9, 10))),
                 ((-1, 0, -1), Q2(F(-27, 20)))}
        spherical_models = {}
        for kind, name in enumerate(KINDS):
            vs = vertices([(n, offset if offset == 1 else offset * factor)
                           for n, offset in self.prototype_planes[kind] if (n, offset) not in trims])
            spherical_models[name] = {
                **models[name], 'surfaceVertices': [[float(x) for x in v] for v in vs]}
        documents['bagua-spherical.json'] = {
            **common, 'id': 'bagua-spherical-v1', 'kind': 'polyhedral-spherical',
            'models': spherical_models, 'scale': 2.05, 'radius': 2.05,
            'diskAngleDegrees': acos(float(h * factor)) * 180 / pi,
            'angularSegments': 64, 'radialSegments': 5, 'portInset': 0.04,
            'surfaceLift': 0.02, 'fidelity': 'spherical-section-with-inherited-guards'}
        return documents


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--probe', action='store_true', help='Write an unreviewed candidate to build/.')
    args = parser.parse_args()
    # An authoring-process ceiling, independent of the container's 4 GiB cap.
    # Fail instead of consuming the memory needed by the editor and runtime.
    ceiling = 384 * 1024 * 1024
    current, hard = resource.getrlimit(resource.RLIMIT_AS)
    resource.setrlimit(resource.RLIMIT_AS, (min(current, ceiling) if current >= 0 else ceiling, hard))
    model = AuthoringModel()
    document = model.definition(exclusions=not args.probe)
    temporary = ROOT / 'build/bagua-source.json'
    temporary.write_text(json.dumps(document, separators=(',', ':')) + '\n')
    result = json.loads(subprocess.check_output([str(ROOT / 'build/native/twisty'), 'compile',
                                                '--definition', str(temporary), '--json'], text=True))
    assert result['status'] == 'Compiled', result
    compiled = result['definition']
    if args.probe:
        (ROOT / 'build/bagua-candidate.json').write_text(json.dumps(compiled, separators=(',', ':')) + '\n')
        return
    documents = {'definition.json': compiled}
    documents.update(model.realizations(compiled['definitionDigest']))
    for filename, data in documents.items():
        destination = ROOT / 'packages/bagua' / filename
        # Keep definitions below the kernel's 4 MiB document bound.
        serialized = json.dumps(data, sort_keys=True, separators=(',', ':')) + '\n'
        assert len(serialized.encode()) < 4 * 1024 * 1024
        if args.write:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(serialized)
        elif json.loads(destination.read_text()) != data:
            raise SystemExit(f'Bagua {filename} is stale; review it before using --write.')
    print(f'bagua: {compiled["definitionDigest"]}', flush=True)


if __name__ == '__main__':
    main()
