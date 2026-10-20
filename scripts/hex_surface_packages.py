#!/usr/bin/env python3
"""Four face-turning disks on a 60-degree rhombus, using exact lattice transports.

Integer coordinates are sixths of a triangular-lattice basis. The fundamental
rhombus has side two. Floating point is confined to rendering assets/frames.
"""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import subprocess
import tempfile

from flat_surface_packages import ROOT, IDENTITY, mv, mm, key, star_mesh, mesh, triangulate

RADIUS = .8
SWAP = (0, 1, 1, 0)
ROTATIONS = [IDENTITY]
for _ in range(5):
    ROTATIONS.append(mm((0, -1, 1, 1), ROTATIONS[-1]))
CENTERS = dict(zip('ABCD', [(0, 0), (6, 0), (0, 6), (6, 6)]))
PROTOTYPES = {'face': [(0, 0)], 'edge': [(-3, 0), (3, 0)],
              'vertex': [(-2, -2), (4, -2), (-2, 4)]}
ANCHOR = {'face': (0, 0), 'edge': (3, 0), 'vertex': (2, 2)}


def cart(p):
    return ((p[0] + p[1]/2)/6, p[1]*math.sqrt(3)/12)


def canonical(t, q, klein):
    i, j = t[0]//12, t[1]//12
    t = (t[0] % 12, t[1] % 12)
    if klein and (i+j) % 2:
        return (t[1], t[0]), mm(SWAP, q)
    return t, q


def circle_name(c, klein):
    t, _ = canonical(c, IDENTITY, klein)
    return next(name for name, center in CENTERS.items() if center == t)


def signature(t, q, family):
    return t, tuple(sorted(mv(q, p) for p in PROTOTYPES[family]))


def frame(t, q):
    # Conjugate the integer lattice map by its oblique Cartesian basis.
    u, v = cart(mv(q, (6, 0))), cart(mv(q, (0, 6)))
    a, c = u
    b, d = (2*v[0]-a)/math.sqrt(3), (2*v[1]-c)/math.sqrt(3)
    x, y = cart(t)
    det = q[0]*q[3]-q[1]*q[2]
    return [a, c, 0, 0, b, d, 0, 0, 0, 0, det, 0, x, y, 0, 1]


def boundary(family):
    positive = frozenset(cart(p) for p in PROTOTYPES[family])
    origin = ANCHOR[family]
    circles = [cart((6*i-origin[0], 6*j-origin[1])) for i in range(-3, 4) for j in range(-3, 4)]
    arcs = []
    for c in circles:
        angles = []
        for d in circles:
            dx, dy = d[0]-c[0], d[1]-c[1]
            length = math.hypot(dx, dy)
            if 1e-9 < length < 2*RADIUS:
                base, delta = math.atan2(dy, dx), math.acos(length/(2*RADIUS))
                angles += [(base-delta) % math.tau, (base+delta) % math.tau]
        angles = sorted(set(round(a, 13) for a in angles))
        for i, a in enumerate(angles):
            b = angles[(i+1) % len(angles)]
            if b <= a:
                b += math.tau
            mid = (c[0]+RADIUS*math.cos((a+b)/2), c[1]+RADIUS*math.sin((a+b)/2))
            inside = frozenset(d for d in circles if d != c and math.dist(mid, d) < RADIUS-1e-10)
            if inside | {c} != positive and inside != positive:
                continue
            samples = [a] + [k*math.tau/96 for k in range(math.floor(a*96/math.tau)+1, math.ceil(b*96/math.tau))
                             if a+1e-10 < k*math.tau/96 < b-1e-10] + [b]
            points = [(c[0]+RADIUS*math.cos(v), c[1]+RADIUS*math.sin(v)) for v in samples]
            if inside == positive:
                points.reverse()
            arcs.append(points)
    rounded = lambda p: tuple(round(v, 9) for v in p)
    by_start = {rounded(arc[0]): arc for arc in arcs}
    assert arcs and len(by_start) == len(arcs), (family, 'ambiguous boundary')
    result, arc = [], arcs[0]
    for _ in arcs:
        result.extend(arc[:-1])
        arc = by_start[rounded(arc[-1])]
    assert arc is arcs[0]
    return result


def half_plane(poly, normal, bound):
    result = []
    for a, b in zip(poly, poly[1:]+poly[:1]):
        da = bound-sum(x*y for x, y in zip(normal, a))
        db = bound-sum(x*y for x, y in zip(normal, b))
        if da >= -1e-12:
            result.append(a)
        if (da < -1e-12) != (db < -1e-12):
            t = da/(da-db)
            result.append(tuple(a[i]+t*(b[i]-a[i]) for i in (0, 1)))
    return result


def assets_for(family):
    poly = boundary(family)
    body, center, area = star_mesh(poly)
    assets = {family+'/body': body}
    inset = [tuple(center[i]+(p[i]-center[i])*.94 for i in (0, 1)) for p in poly]
    # The initial colors are the four regular hexagonal Voronoi cells.
    neighbors = [(6, 0), (0, 6), (-6, 6), (-6, 0), (0, -6), (6, -6)]
    for port, c in enumerate(PROTOTYPES[family]):
        patch, c = inset, cart(c)
        for n in neighbors:
            n = cart(n)
            patch = half_plane(patch, n, .5 + sum(a*b for a, b in zip(n, c)))
        assets[family+f'/port-{port}'] = star_mesh(patch)[0]
    s = min(.05, min(math.dist(center, p) for p in poly)*.35)
    marker = [(0, 0), (s, 0), (s, s*.28), (s*.28, s*.28), (s*.28, s*.8), (0, s*.8)]
    marker = [(x+center[0]-s*.4, y+center[1]-s*.35) for x, y in marker]
    assets[family+'/mark'] = mesh(marker, triangulate(marker))
    def triangle_set(asset, q=IDENTITY):
        f = frame((0, 0), q)
        points = [tuple(round(v, 9) for v in (f[0]*x+f[4]*y, f[1]*x+f[5]*y)) for x, y, _ in asset['vertices']]
        return {tuple(sorted(points[i] for i in tri)) for tri in asset['triangles']}
    for q in ROTATIONS+[mm(r, SWAP) for r in ROTATIONS]:
        if {mv(q, p) for p in PROTOTYPES[family]} != set(PROTOTYPES[family]):
            continue
        assert triangle_set(body, q) == triangle_set(body), (family, 'body symmetry')
        patches = [a for name, a in assets.items() if '/port-' in name]
        assert set().union(*(triangle_set(a, q) for a in patches)) == set().union(*(triangle_set(a) for a in patches)), (family, 'patch symmetry')
    return assets, area


def generate(topology):
    klein = topology == 'klein-bottle'
    orientations = ROTATIONS+([mm(r, SWAP) for r in ROTATIONS] if klein else [])
    source = dict(kind='relational-puzzle', languageVersion=1, id='flat-hex-'+topology+'-v1', domains=[], predicates={}, requests=[])
    catalog = dict(kind='relational-port-bindings', version=1, puzzleId=source['id'], resources=[], domains=[], pieces={}, placements={}, requests={})
    realization = dict(schemaVersion=1, id=source['id'], kind='flat-quotient',
                       requiredCapabilities=['triangle-meshes', 'rigid-transforms', 'quotient-surface'],
                       flatSurface=dict(topology=topology, sideLength=2, chart='rhombus-60', cutRadius=RADIUS,
                                        diskCenters={n:list(cart(c)) for n, c in CENTERS.items()}),
                       assets={}, componentsByDomain={}, fidelity='hexagonal-circle-arrangement')
    invariants, goal, counts, total_area = [], [], {}, 0
    for family, shape in PROTOTYPES.items():
        if family == 'face':
            anchors = list(CENTERS.values())
        elif family == 'edge':
            anchors = [(x, y) for y in range(0, 12, 3) for x in range(0, 12, 3) if x % 6 or y % 6]
        else:
            anchors = [(x+i, y+i) for i in (2, 4) for y in (0, 6) for x in (0, 6)]
        poses = [(t, q) for t in anchors for q in orientations if all(
            (t[0]+mv(q, p)[0]) % 6 == 0 and (t[1]+mv(q, p)[1]) % 6 == 0 for p in shape)]
        pose_by_key = {key(t, q):(t, q) for t, q in poses}
        locations = {}
        for t, q in poses:
            locations.setdefault(signature(t, q, family), len(locations))
        counts[family] = len(locations)
        members, frames, groups = [], {key(t, q):frame(t, q) for t, q in poses}, defaultdict(list)
        domain = dict(id=family, placements=list(pose_by_key), pieces=members, moves=[])
        source['domains'].append(domain)
        local_ports = list(map(str, range(len(shape))))
        for t, q in poses:
            k, loc = key(t, q), f'location/{family}/{locations[signature(t, q, family)]:02}'
            groups[loc].append(family+'/'+k)
            circles = [(t[0]+mv(q, p)[0], t[1]+mv(q, p)[1]) for p in shape]
            catalog['placements'][family+'/'+k] = dict(resources=[loc], ports={str(i):dict(cell=loc, attachment=circle_name(c, klein)) for i, c in enumerate(circles)})
        seen = set()
        for t, q in poses:
            sig, k = signature(t, q, family), key(t, q)
            if sig in seen:
                continue
            seen.add(sig)
            name = f'{family}-{locations[sig]:02}'
            members.append(dict(id=name, initial=k))
            catalog['pieces'][name] = dict(domain=family, home=family+'/'+k, portLabels={p:catalog['placements'][family+'/'+k]['ports'][p]['attachment'] for p in local_ports})
            goal.append({'at':dict(piece=name, placements=[family+'/'+k])})
        catalog['domains'].append(dict(id=family, pieces=[p['id'] for p in members], placements=[family+'/'+k for k in pose_by_key], localPorts=local_ports))
        for loc, qs in groups.items():
            catalog['resources'].append(loc)
            invariants.append({'count':dict(min=1, max=1, of=[{'at':dict(piece=p['id'], placements=qs)} for p in members])})
        operation_tracks = {}
        for name in CENTERS:
            for suffix, turns in [('', -1), ("'", 1), ('2', -2), ("2'", 2), ('3', -3)]:
                op, transport, tracks = name+suffix, {}, {}
                for k, (t, q) in pose_by_key.items():
                    circles = [(t[0]+mv(q, p)[0], t[1]+mv(q, p)[1]) for p in shape]
                    driving = [c for c in circles if circle_name(c, klein) == name]
                    if not driving:
                        continue
                    assert len(driving) == 1, (name, circles)
                    c = driving[0]
                    sign = -1 if klein and (c[0]//12+c[1]//12) % 2 else 1
                    rotation = ROTATIONS[turns*sign % 6]
                    delta = mv(rotation, (t[0]-c[0], t[1]-c[1]))
                    target, target_q = canonical((c[0]+delta[0], c[1]+delta[1]), mm(rotation, q), klein)
                    dest = key(target, target_q)
                    assert dest in pose_by_key, (family, op, k, dest)
                    transport[k] = dest
                    tracks[k] = [dict(axis=[0, 0, 1], pivot=[*cart(c), 0], angle=turns*sign*math.pi/3, space='world')]
                assert set(transport) == set(transport.values())
                domain['moves'].append(dict(id=op, support=list(transport), transport=transport, event=op+'.motion'))
                operation_tracks[op] = tracks
        assets, area = assets_for(family)
        total_area += area*len(locations)
        realization['assets'].update(assets)
        components = []
        for asset in assets:
            port = asset.rsplit('port-', 1)[1] if '/port-' in asset else None
            component = dict(id=asset.split('/')[1], meshAssetId=asset, role='port' if port is not None else 'body', placementFrames=frames, operationTracks=operation_tracks)
            if port is not None:
                component['portId'] = port
            else:
                component['materialBindingId'] = 'ink' if asset.endswith('/mark') else 'body'
            components.append(component)
        realization['componentsByDomain'][family] = components
    for name in CENTERS:
        for suffix, inverse in [('', "'"), ("'", ''), ('2', "2'"), ("2'", '2'), ('3', '3')]:
            op = name+suffix
            source['requests'].append(dict(id=op, relation={'all':[{'chosen':dict(domain=f, moves=[op])} for f in PROTOTYPES]},
                profiles=[dict(id='default', choices={f:op for f in PROTOTYPES})], defaultProfile='default'))
            catalog['requests'][op] = dict(inverse=name+inverse, family=name, event=op+'.motion', profile='default', unavailablePlacements=[])
    source['invariant'], source['goal'] = {'all':invariants}, {'all':goal}
    assert counts == dict(face=4, edge=12, vertex=8), counts
    assert abs(total_area-2*math.sqrt(3)) < 1e-9, ('area', total_area)
    for domain in source['domains']:
        moves = {m['id']:m['transport'] for m in domain['moves']}
        for op, transport in moves.items():
            inverse = moves[catalog['requests'][op]['inverse']]
            assert all(inverse[v] == k for k, v in transport.items())
    total = sum(len(a['triangles']) for a in realization['assets'].values())
    assert total < 1000
    return source, catalog, realization, dict(pieceCounts=counts, uniqueTriangles=total, cutRadius=RADIUS)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--native', default='build/native/twisty')
    args = parser.parse_args()
    for topology in ['torus', 'klein-bottle']:
        source, catalog, realization, manifest = generate(topology)
        package = dict(kind='relational-package', formatVersion=1, source=source, catalog=catalog)
        with tempfile.TemporaryDirectory(prefix='twisty-hex-') as directory:
            path = Path(directory)/'package.json'
            path.write_text(json.dumps(package))
            result = json.loads(subprocess.check_output([str(ROOT/args.native), 'compile', '--definition', str(path), '--json'], text=True))
            assert result['status'] == 'Compiled', result
        digest = result['definition']['definitionDigest']
        realization['compatibleDefinitionDigest'] = digest
        manifest.update(definitionDigest=digest, topology=topology)
        folder = ROOT/'packages'/('flat-hex-'+topology)
        if args.write:
            folder.mkdir(parents=True, exist_ok=True)
        for name, document in {'relational.pzl.json':source, 'relational-bindings.json':catalog,
                               'relational-package.json':package, 'flat-quotient.json':realization, 'manifest.json':manifest}.items():
            path = folder/name
            if args.write:
                path.write_text(json.dumps(document, indent=2, sort_keys=True)+'\n')
            elif not path.exists() or json.loads(path.read_text()) != document:
                raise SystemExit(f'{path.relative_to(ROOT)} is stale; regenerate with --write')
        print(f'hexagonal {topology}: 24 pieces, {manifest["uniqueTriangles"]} triangles, {digest}')


if __name__ == '__main__':
    main()
