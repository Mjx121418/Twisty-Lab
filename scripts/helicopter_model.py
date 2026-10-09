#!/usr/bin/env python3
"""Offline exact authoring of the experimental Helicopter Cube relation package.

Coordinates are confined to this authoring tool. The generated definition contains
only abstract identifiers, exclusion resources, guards, and directed transports.
All constructions use rational or integer arithmetic, including collision tests.
"""
import argparse
from fractions import Fraction as Q
from itertools import combinations, permutations, product
import json
from math import gcd, lcm
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
IDENTITY = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
FACES = {'U': (0, 1, 0), 'D': (0, -1, 0), 'F': (0, 0, 1),
         'B': (0, 0, -1), 'R': (1, 0, 0), 'L': (-1, 0, 0)}
GRIPS = ['UF', 'UR', 'UB', 'UL', 'DF', 'DR', 'DB', 'DL', 'FR', 'FL', 'BR', 'BL']
CORNER = ((1, 1, 1), (0, 1, 1), (1, 0, 1), (1, 1, 0), (Q(1, 2),) * 3)
CENTER = ((0, 1, 0), (1, 1, 0), (0, 1, 1), (Q(1, 2),) * 3)
EDGE = ((1, 1, 0), (1, 0, 0), (0, 1, 0),
        (Q(1, 2), Q(1, 2), Q(1, 2)), (Q(1, 2), Q(1, 2), Q(-1, 2)))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def transpose(a):
    return tuple(zip(*a))


def matmul(a, b):
    return tuple(tuple(dot(row, col) for col in transpose(b)) for row in a)


def apply(a, v):
    return tuple(dot(row, v) for row in a)


def determinant(a):
    return dot(a[0], cross(a[1], a[2]))


def shape(vertices):
    return tuple(sorted(vertices))


def transformed(a, vertices):
    return shape(apply(a, v) for v in vertices)


def classify(vertices, axis):
    values = [dot(v, axis) for v in vertices]
    if min(values) < 1 < max(values):
        return 2  # straddles the cut: rigid-piece blocker
    return 1 if min(values) >= 1 and max(values) > 1 else 0


def primitive(v):
    divisor = gcd(*v)
    if not divisor:
        return None
    result = tuple(x // divisor for x in v)
    return result if next(x for x in result if x) > 0 else tuple(-x for x in result)


class Polyhedron:
    """Integer convex hull features for an exact separating-axis test."""
    def __init__(self, vertices):
        self.scale = lcm(*(Q(x).denominator for v in vertices for x in v))
        self.vertices = tuple(tuple(int(x * self.scale) for x in v) for v in vertices)
        self.normals = set()
        facets = set()
        for a, b, c in combinations(self.vertices, 3):
            normal = primitive(cross(sub(b, a), sub(c, a)))
            if normal is None:
                continue
            distances = [dot(normal, sub(v, a)) for v in self.vertices]
            if min(distances) >= 0 or max(distances) <= 0:
                self.normals.add(normal)
                facets.add(tuple(i for i, d in enumerate(distances) if d == 0))
        self.edges = set()
        for i, j in combinations(range(len(self.vertices)), 2):
            if sum(i in f and j in f for f in facets) >= 2:
                self.edges.add(primitive(sub(self.vertices[i], self.vertices[j])))

    def overlaps(self, other):
        normals = self.normals | other.normals
        normals |= {n for a in self.edges for b in other.edges if (n := primitive(cross(a, b)))}
        for normal in normals:
            a = [dot(normal, v) * other.scale for v in self.vertices]
            b = [dot(normal, v) * self.scale for v in other.vertices]
            if max(a) <= min(b) or max(b) <= min(a):
                return False  # touching boundaries do not occupy shared volume
        return True


class AuthoringModel:
    def __init__(self):
        self.symmetries = []
        for p in permutations(range(3)):
            for signs in product((-1, 1), repeat=3):
                g = tuple(tuple(signs[i] if p[i] == j else 0 for j in range(3)) for i in range(3))
                if determinant(g) == 1:
                    self.symmetries.append(g)
        self.axes = [tuple(x + y for x, y in zip(FACES[n[0]], FACES[n[1]])) for n in GRIPS]
        # acos(1/3) around (1,1,0); outward viewing direction makes this clockwise.
        alpha = ((Q(2, 3), Q(1, 3), Q(2, 3)), (Q(1, 3), Q(2, 3), Q(-2, 3)),
                 (Q(-2, 3), Q(2, 3), Q(1, 3)))
        alpha = transpose(alpha)
        half = ((0, 1, 0), (1, 0, 0), (0, 0, -1))
        self.rotations = []
        self.edge_shapes = []
        for axis in self.axes:
            g = next(g for g in self.symmetries if apply(g, (1, 1, 0)) == axis)
            r = matmul(matmul(g, alpha), transpose(g))
            h = matmul(matmul(g, half), transpose(g))
            stops = [IDENTITY, r, matmul(h, transpose(r)), h, matmul(h, r), transpose(r)]
            self.rotations.append(stops)
            self.edge_shapes.extend(transformed(matmul(stop, g), EDGE) for stop in stops[:3])
        self.relative = [[[matmul(stops[q], transpose(stops[p])) for q in range(6)]
                          for p in range(3)] for stops in self.rotations]
        self.catalogs = [[], []]
        self.by_shape = [{}, {}]
        self.roles = [[], []]
        self.moves = [{}, {}]
        for kind, prototype in enumerate((CORNER, CENTER)):
            for g in self.symmetries:
                self.intern(kind, transformed(g, prototype))
        self.edge_roles = [tuple(classify(vs, a) for a in self.axes) for vs in self.edge_shapes]
        self.origin = bytes(list(range(8)) + [0] * 12 + list(range(24)))
        # Two moves suffice to discover all geometric locations. Exhaustive closure
        # and reachability are checked separately by the C++ headless verifier.
        seen = {self.origin}
        frontier = seen.copy()
        for _ in range(2):
            following = {t for s in sorted(frontier) for t in self.successors(s)} - seen
            seen |= following
            frontier = following
        assert [len(c) for c in self.catalogs] == [80, 144]

    def intern(self, kind, vertices):
        if vertices not in self.by_shape[kind]:
            self.by_shape[kind][vertices] = len(self.catalogs[kind])
            self.catalogs[kind].append(vertices)
            self.roles[kind].append(tuple(classify(vertices, a) for a in self.axes))
        return self.by_shape[kind][vertices]

    def successors(self, state):
        corners, phases, centers = state[:8], state[8:20], state[20:]
        for axis in range(12):
            if (any(self.roles[0][c][axis] == 2 for c in corners)
                    or any(self.roles[1][c][axis] == 2 for c in centers)
                    or any(self.edge_roles[3 * b + phases[b]][axis] == 2 for b in range(12))):
                continue
            p = phases[axis]
            for q in range(6):
                if p == q:
                    continue
                outputs = []
                for kind, locations in enumerate((corners, centers)):
                    moved = []
                    for location in locations:
                        if self.roles[kind][location][axis] != 1:
                            moved.append(location)
                            continue
                        key = location, axis, p, q
                        if key not in self.moves[kind]:
                            target = transformed(self.relative[axis][p][q], self.catalogs[kind][location])
                            self.moves[kind][key] = self.intern(kind, target)
                        moved.append(self.moves[kind][key])
                    outputs.append(sorted(moved))
                changed = list(phases)
                changed[axis] = q % 3
                yield bytes(outputs[0] + changed + outputs[1])

    def definition(self):
        corner_shapes, center_shapes = [sorted(catalog) for catalog in self.catalogs]
        all_shapes = corner_shapes + center_shapes + self.edge_shapes
        self.all_shapes = all_shapes
        resources = [[f'location/{i:03}'] for i in range(len(all_shapes))]
        polyhedra = [Polyhedron(vs) for vs in all_shapes]
        exclusions = 0
        for i, j in combinations(range(len(polyhedra)), 2):
            if polyhedra[i].overlaps(polyhedra[j]):
                token = f'exclusion/{exclusions:04}'
                resources[i].append(token)
                resources[j].append(token)
                exclusions += 1
        assert exclusions == 2424
        corner_frames = []
        for vs in corner_shapes:
            tip = next(v for v in vs if dot(v, v) == 3)
            neighbors = [v for v in vs if dot(v, v) == 2]
            for order in permutations(neighbors):
                frame = transpose(tuple(sub(tip, v) for v in order))
                if determinant(frame) == 1:
                    corner_frames.append(frame)
        center_frames = []
        for vs in center_shapes:
            face = next(v for v in vs if dot(v, v) == 1)
            ends = [v for v in vs if dot(v, v) == 2]
            frame = transpose((sub(ends[0], face), face, sub(ends[1], face)))
            if determinant(frame) != 1:
                frame = transpose((sub(ends[1], face), face, sub(ends[0], face)))
            assert determinant(frame) == 1
            center_frames.append(frame)
        frames = {'corner': sorted(corner_frames), 'center': sorted(center_frames)}
        assert len(frames['corner']) == 240 and len(frames['center']) == 144
        geom_indexes = {vs: i for i, vs in enumerate(all_shapes)}
        definitions = []
        types = []
        pieces = []
        placement_geometry = {}
        frame_keys = {}
        reverse_faces = {v: name for name, v in FACES.items()}
        for kind, prototype in [('corner', CORNER), ('center', CENTER)]:
            entries = []
            home_locations = set()
            frame_keys[kind] = {frame: f'{kind}/q{i:03}' for i, frame in enumerate(frames[kind])}
            for frame, key in frame_keys[kind].items():
                vs = transformed(frame, prototype)
                geometry = geom_indexes[vs]
                placement_geometry[key] = vs
                columns = transpose(frame)
                sockets = sorted(columns) if kind == 'corner' else [columns[1]]
                ports = {str(i): {'cell': f'location/{geometry:03}',
                                 'attachment': f'socket/{sockets.index(normal)}'}
                         for i, normal in enumerate(columns if kind == 'corner' else [columns[1]])}
                entries.append({'key': key, 'footprint': resources[geometry], 'portAttachment': ports})
                if vs in {transformed(g, prototype) for g in self.symmetries} and geometry not in home_locations:
                    home_locations.add(geometry)
                    names = [reverse_faces[v] for v in columns]
                    identity = (''.join(sorted(names)) if kind == 'corner'
                                else names[1] + ''.join(sorted([names[0].lower(), names[2].lower()])))
                    labels = {str(i): reverse_faces[v] for i, v in enumerate(
                        columns if kind == 'corner' else [columns[1]])}
                    pieces.append({'id': f'{kind}/{identity}', 'type': kind,
                                   'homePlacement': key, 'portLabels': labels})
            definitions.append({'id': kind, 'placements': entries})
            types.append({'id': kind, 'placementDomainId': kind,
                          'localPorts': ['0', '1', '2'] if kind == 'corner' else ['0']})
        for axis, name in enumerate(GRIPS):
            domain = f'edge/{name}'
            entries = []
            for phase in range(3):
                key = f'{domain}/{"abc"[phase]}'
                vs = self.edge_shapes[axis * 3 + phase]
                placement_geometry[key] = vs
                entries.append({'key': key, 'footprint': resources[224 + axis * 3 + phase],
                                'portAttachment': {}})
            definitions.append({'id': domain, 'placements': entries})
            types.append({'id': domain, 'placementDomainId': domain, 'localPorts': []})
            pieces.append({'id': f'mechanism/{name}', 'type': domain,
                           'homePlacement': f'{domain}/a', 'portLabels': {}})
        operations = []
        for axis, name in enumerate(GRIPS):
            for p in range(3):
                for q in range(6):
                    if p == q:
                        continue
                    rules = {}
                    rotation = self.relative[axis][p][q]
                    for domain in definitions:
                        blocked, transports = [], {}
                        for entry in domain['placements']:
                            key = entry['key']
                            vs = placement_geometry[key]
                            role = classify(vs, self.axes[axis])
                            target = None
                            if role == 1:
                                if domain['id'] in frames:
                                    frame = frames[domain['id']][int(key.rsplit('q', 1)[1])]
                                    target = frame_keys[domain['id']].get(matmul(rotation, frame))
                                else:
                                    candidates = {placement_geometry[e['key']]: e['key'] for e in domain['placements']}
                                    target = candidates.get(transformed(rotation, vs))
                            if role == 2 or (role == 1 and target is None):
                                blocked.append(key)
                            elif role == 1:
                                transports[key] = target
                        rules[domain['id']] = {'blocked': blocked, 'transports': transports}
                    inverse_target = p + (3 if q >= 3 else 0)
                    operations.append({'id': f'{name}_{"abc"[p]}{"abcdef"[q]}',
                                       'inverse': f'{name}_{"abc"[q % 3]}{"abcdef"[inverse_target]}',
                                       'family': name, 'transport': f'stop/{name}/{p}/{q}',
                                       'pieceGuards': {f'mechanism/{name}': f'edge/{name}/{"abc"[p]}'},
                                       'placementRules': rules})
        return {'schemaVersion': 1, 'semanticsVersion': 'helicopter-stops-v1',
                'kind': 'finite-definition', 'puzzleId': 'helicopter-experimental-v1',
                'ruleModule': 'finite-placement-relations@1',
                'cells': sorted({r for rs in resources for r in rs}),
                'pieceTypes': types, 'pieces': pieces, 'placementDomains': definitions,
                'operations': operations, 'goal': {'kind': 'home'},
                'initialState': {'placementOf': {p['id']: p['homePlacement'] for p in pieces}, 'mechanism': {}},
                'provenance': {'authoringTool': 'scripts/helicopter_model.py',
                               'status': 'experimental-headless', 'geometricLocations': {'corner': 80, 'center': 144, 'edge': 36},
                               'labelledPlacements': {'corner': 240, 'center': 144, 'edge': 36},
                               'references': ['https://twistypuzzles.com/forum/viewtopic.php?f=1&t=28265',
                                              'https://www.jaapsch.net/puzzles/helicopter.htm']}}

    def review(self, digest):
        by_shape = {vs: i for i, vs in enumerate(self.all_shapes)}
        actions = []
        for p in permutations(range(3)):
            for signs in product((-1, 1), repeat=3):
                g = tuple(tuple(signs[i] if p[i] == j else 0 for j in range(3)) for i in range(3))
                action = [by_shape[transformed(g, vs)] for vs in self.all_shapes]
                assert len(set(action)) == 260
                actions.append({'orientation': determinant(g), 'permutation': action})
        return {'schemaVersion': 1, 'definitionDigest': digest,
                'shapeSymmetries': actions,
                'expected': {'orientedShapes': 654117, 'maximumDepth': 28,
                             'rotationClassesByDepth': [1, 2, 19, 122, 490, 1093, 1691, 1760, 2085,
                                2365, 2809, 2377, 2586, 2317, 1725, 1229, 554, 296, 266, 298,
                                280, 304, 434, 771, 1075, 694, 338, 58, 16],
                             'mirrorClassesByDepth': [1, 1, 10, 61, 248, 548, 850, 888, 1046,
                                1190, 1410, 1192, 1298, 1167, 866, 615, 277, 148, 133, 149,
                                140, 153, 220, 387, 544, 350, 169, 29, 8]},
                'reference': 'https://twistypuzzles.com/forum/viewtopic.php?f=1&t=28265'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true', help='Regenerate after reviewing semantic changes.')
    args = parser.parse_args()
    model = AuthoringModel()
    document = model.definition()
    package = ROOT / 'packages/helicopter'
    temporary = ROOT / 'build/helicopter-source.json'
    temporary.write_text(json.dumps(document, separators=(',', ':')) + '\n')
    result = json.loads(subprocess.check_output([str(ROOT / 'build/native/twisty'), 'compile',
                                                '--definition', str(temporary), '--json'], text=True))
    compiled = result['definition']
    documents = {'definition.json': compiled, 'review.json': model.review(compiled['definitionDigest'])}
    for filename, data in documents.items():
        destination = package / filename
        if args.write:
            package.mkdir(parents=True, exist_ok=True)
            destination.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')
        elif json.loads(destination.read_text()) != data:
            raise SystemExit(f'Helicopter {filename} is stale; review it before using --write.')
    print(f'helicopter: {compiled["definitionDigest"]} (240 corner, 144 center, 36 hidden-edge placements)')


if __name__ == '__main__':
    main()
