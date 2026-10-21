#!/usr/bin/env python3
"""Author the radius-4/5 square-grid puzzles, without a geometric runtime rule.

Placements and turns use integer half-unit coordinates and signed permutation
matrices. Floating point is used only for circle arcs, meshes and animation.
"""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
RADIUS = 0.8
CENTERS = dict(zip("ABCD", [(1, 1), (3, 1), (1, 3), (3, 3)]))
IDENTITY = (1, 0, 0, 1)
REFLECT = (-1, 0, 0, 1)
ROTATIONS = [IDENTITY, (0, -1, 1, 0), (-1, 0, 0, -1), (0, 1, -1, 0)]
PROTOTYPES = {
    "face": [(0, 0)],
    "edge": [(-1, 0), (1, 0)],
    "vertex": [(-1, -1), (1, -1), (-1, 1)],
    "vertex-core": [(-1, -1), (1, -1), (-1, 1), (1, 1)],
}
PORT_CENTERS = {**PROTOTYPES, 'vertex': [(-1, -1)]}


def mv(q, p):
    return (q[0] * p[0] + q[1] * p[1], q[2] * p[0] + q[3] * p[1])


def mm(a, b):
    return (a[0]*b[0]+a[1]*b[2], a[0]*b[1]+a[1]*b[3],
            a[2]*b[0]+a[3]*b[2], a[2]*b[1]+a[3]*b[3])


def canonical(t, q, klein):
    x, y = t
    if klein and (y // 4) % 2:
        x = 4 - x
        q = mm(REFLECT, q)
    return (x % 4, y % 4), q


def circle_name(c, klein):
    t, _ = canonical(c, IDENTITY, klein)
    return next(name for name, center in CENTERS.items() if center == t)


def key(t, q):
    return f'{t[0]},{t[1]}/' + ','.join(map(str, q))


def signature(t, q, family):
    return (t, tuple(sorted(mv(q, p) for p in PROTOTYPES[family])))


def frame(t, q):
    det = q[0]*q[3]-q[1]*q[2]
    return [q[0], q[2], 0, 0, q[1], q[3], 0, 0,
            0, 0, det, 0, t[0]/2, t[1]/2, 0, 1]


def boundary(family):
    """Oriented analytic arcs separating this region from its neighbors.

    All circles use the same 64-angle sampling lattice, supplemented by exact
    intersection endpoints. Quarter turns/reflections therefore preserve it.
    """
    positive = frozenset((x/2, y/2) for x, y in PROTOTYPES[family])
    circles = [(x/2, y/2) for x in range(-5, 6, 2) for y in range(-5, 6, 2)]
    if family == 'face':
        circles = [(x, y) for x in range(-2, 3) for y in range(-2, 3)]
    elif family == 'edge':
        circles = [(x/2, y) for x in range(-5, 6, 2) for y in range(-2, 3)]
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
            m = (c[0]+RADIUS*math.cos((a+b)/2), c[1]+RADIUS*math.sin((a+b)/2))
            inside = frozenset(d for d in circles if d != c and
                               (m[0]-d[0])**2+(m[1]-d[1])**2 < RADIUS**2-1e-10)
            if inside | {c} != positive and inside != positive:
                continue
            samples = [a] + [k*math.tau/64 for k in range(math.floor(a*64/math.tau)+1,
                                                          math.ceil(b*64/math.tau))
                             if a+1e-10 < k*math.tau/64 < b-1e-10] + [b]
            points = [(c[0]+RADIUS*math.cos(v), c[1]+RADIUS*math.sin(v)) for v in samples]
            if inside == positive:
                points.reverse()
            arcs.append(points)
    rounded = lambda p: tuple(round(v, 9) for v in p)
    by_start = {rounded(arc[0]): arc for arc in arcs}
    assert len(by_start) == len(arcs), (family, 'ambiguous boundary')
    result = []
    arc = arcs[0]
    for _ in arcs:
        result.extend(arc[:-1])
        arc = by_start[rounded(arc[-1])]
    assert arc is arcs[0] and len(result) > 3
    return result


def cross(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def triangulate(poly):
    remaining = list(range(len(poly)))
    triangles = []
    while len(remaining) > 3:
        for k, b in enumerate(remaining):
            a, c = remaining[k-1], remaining[(k+1) % len(remaining)]
            if cross(poly[a], poly[b], poly[c]) < 1e-12:
                continue
            if any(all(cross(poly[u], poly[v], poly[p]) >= -1e-12
                       for u, v in [(a,b), (b,c), (c,a)])
                   for p in remaining if p not in (a,b,c)):
                continue
            triangles.append((a,b,c))
            remaining.pop(k)
            break
        else:
            raise ValueError('Cannot triangulate simple circular region')
    triangles.append(tuple(remaining))
    return triangles


def clip(poly, axis, bound, sign):
    output = []
    for a, b in zip(poly, poly[1:]+poly[:1]):
        da, db = sign*(a[axis]-bound), sign*(b[axis]-bound)
        if da >= -1e-12:
            output.append(a)
        if (da < -1e-12) != (db < -1e-12):
            t = da/(da-db)
            output.append((a[0]+t*(b[0]-a[0]), a[1]+t*(b[1]-a[1])))
    return output


def mesh(poly, triangles):
    return dict(vertices=[[round(x, 12), round(y, 12), 0] for x,y in poly],
                triangles=[list(t) for t in triangles])


def star_mesh(poly):
    clean = []
    for point in poly:
        if not clean or math.dist(point,clean[-1]) > 1e-10:
            clean.append(point)
    if math.dist(clean[0],clean[-1]) < 1e-10:
        clean.pop()
    area2 = sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(clean,clean[1:]+clean[:1]))
    center = tuple(sum((a[i]+b[i])*(a[0]*b[1]-b[0]*a[1])
                       for a,b in zip(clean,clean[1:]+clean[:1]))/(3*area2) for i in (0,1))
    center = tuple(0 if abs(v)<1e-12 else v for v in center)
    # A centroid fan preserves every symmetry of a star-shaped region.
    assert all(cross(center,a,b)>1e-12 for a,b in zip(clean,clean[1:]+clean[:1]))
    count = len(clean)
    return mesh(clean+[center],[(count,i,(i+1)%count) for i in range(count)]), center, area2/2


def assets_for(family):
    poly = boundary(family)
    body, centroid, area = star_mesh(poly)
    inset = [tuple(centroid[i]+(p[i]-centroid[i])*.94 for i in range(2)) for p in poly]
    assets = {family+'/body': body}
    for port, (cx,cy) in enumerate(PORT_CENTERS[family]):
        patch = inset
        for axis, center in [(0,cx/2), (1,cy/2)]:
            patch = clip(patch,axis,center-.5,1)
            patch = clip(patch,axis,center+.5,-1)
        assets[family+f'/port-{port}'] = star_mesh(patch)[0]
    # A small asymmetric L distinguishes orientation and handedness.
    clearance = min(math.hypot(centroid[0]-p[0], centroid[1]-p[1]) for p in poly)
    s = min(.06, clearance*.4)
    marker = [(0,0), (s,0), (s,s*.28), (s*.28,s*.28), (s*.28,s*.8), (0,s*.8)]
    marker = [(x+centroid[0]-s*.4, y+centroid[1]-s*.35) for x,y in marker]
    assets[family+'/mark'] = mesh(marker, triangulate(marker))
    # Verify connectivity as well as boundary symmetry, excluding the
    # deliberately asymmetric orientation marker.
    def triangle_set(asset, q=IDENTITY):
        points = [tuple(round(v,9) for v in mv(q,p[:2])) for p in asset['vertices']]
        return {tuple(sorted(points[i] for i in tri)) for tri in asset['triangles']}
    circle_set = set(PROTOTYPES[family])
    for q in ROTATIONS + [mm(r,REFLECT) for r in ROTATIONS]:
        if {mv(q,p) for p in circle_set} != circle_set:
            continue
        assert triangle_set(body,q) == triangle_set(body), (family,'body symmetry')
        patches = [a for name,a in assets.items() if '/port-' in name]
        assert set().union(*(triangle_set(a,q) for a in patches)) == set().union(*(triangle_set(a) for a in patches)), (family,'patch symmetry')
    return assets, area


def generate(topology):
    klein = topology == 'klein-bottle'
    orientations = ROTATIONS + ([mm(r,REFLECT) for r in ROTATIONS] if klein else [])
    source = dict(kind='relational-puzzle', languageVersion=1, id='flat-'+topology+'-v1',
                  domains=[], predicates={}, requests=[])
    catalog = dict(kind='relational-port-bindings', version=1, puzzleId=source['id'],
                   resources=[], domains=[], pieces={}, placements={}, requests={})
    realization = dict(schemaVersion=1, id='flat-'+topology+'-v1', kind='flat-quotient',
                       requiredCapabilities=['triangle-meshes','rigid-transforms','quotient-surface'],
                       flatSurface=dict(topology=topology, sideLength=2, cutRadius=RADIUS,
                                        diskCenters={name:[x/2,y/2] for name,(x,y) in CENTERS.items()}),
                       assets={}, componentsByDomain={}, fidelity='flat-circle-arrangement')
    invariants, goal, counts = [], [], {}
    total_area = 0
    for family, shape in PROTOTYPES.items():
        ports = PORT_CENTERS[family]
        if family == 'face':
            anchors = [(x,y) for y in (1,3) for x in (1,3)]
        elif family == 'edge':
            anchors = [(x,y) for y in (1,3) for x in (0,2)] + [(x,y) for y in (0,2) for x in (1,3)]
        else:
            anchors = [(x,y) for y in (0,2) for x in (0,2)]
        poses = [(t,q) for t in anchors for q in orientations
                 if family != 'edge' or (q[0] != 0) == (t[0] % 2 == 0)]
        pose_by_key = {key(t,q):(t,q) for t,q in poses}
        locations = {}
        for t,q in poses:
            locations.setdefault(signature(t,q,family), len(locations))
        counts[family] = len(locations)
        members, frames = [], {key(t,q):frame(t,q) for t,q in poses}
        source_domain = dict(id=family, placements=list(pose_by_key), pieces=members, moves=[])
        source['domains'].append(source_domain)
        groups = defaultdict(list)
        local_ports = [str(i) for i in range(len(ports))]
        for t,q in poses:
            k = key(t,q)
            loc = f'location/{family}/{locations[signature(t,q,family)]:02}'
            groups[loc].append(family+'/'+k)
            transformed = [(t[0]+mv(q,p)[0],t[1]+mv(q,p)[1]) for p in ports]
            catalog['placements'][family+'/'+k] = dict(resources=[loc], ports={
                str(i):dict(cell=loc,attachment=circle_name(c,klein)) for i,c in enumerate(transformed)})
        seen = set()
        for t,q in poses:
            sig = signature(t,q,family)
            if sig in seen:
                continue
            seen.add(sig)
            name = f'{family}-{locations[sig]:02}'
            k = key(t,q)
            members.append(dict(id=name, initial=k))
            labels = {p:catalog['placements'][family+'/'+k]['ports'][p]['attachment'] for p in local_ports}
            catalog['pieces'][name] = dict(domain=family,home=family+'/'+k,portLabels=labels)
            goal.append({'at':dict(piece=name,placements=[family+'/'+k])})
        catalog['domains'].append(dict(id=family,pieces=[p['id'] for p in members],
                                       placements=[family+'/'+k for k in pose_by_key],localPorts=local_ports))
        for location, qs in groups.items():
            catalog['resources'].append(location)
            invariants.append({'count':dict(min=1,max=1,of=[
                {'at':dict(piece=p['id'],placements=qs)} for p in members])})
        operation_tracks = {}
        for name in CENTERS:
            for suffix, turns in [('',-1),("'",1),('2',-2)]:
                op = name+suffix
                transport, tracks = {}, {}
                for k,(t,q) in pose_by_key.items():
                    circles = [(t[0]+mv(q,p)[0],t[1]+mv(q,p)[1]) for p in shape]
                    driving = [c for c in circles if circle_name(c,klein) == name]
                    if not driving:
                        continue
                    assert len(driving) == 1
                    c = driving[0]
                    parity = -1 if klein and (c[1]//4)%2 else 1
                    signed_turns = turns*parity
                    rotation = ROTATIONS[signed_turns%4]
                    delta = mv(rotation,(t[0]-c[0],t[1]-c[1]))
                    target, orientation = canonical((c[0]+delta[0],c[1]+delta[1]),mm(rotation,q),klein)
                    destination = key(target,orientation)
                    assert destination in pose_by_key
                    transport[k] = destination
                    tracks[k] = [dict(axis=[0,0,1],pivot=[c[0]/2,c[1]/2,0],
                                      angle=signed_turns*math.pi/2,space='world')]
                assert set(transport) == set(transport.values()), (family,op,'nonbijective')
                source_domain['moves'].append(dict(id=op,support=list(transport),transport=transport,event=op+'.motion'))
                operation_tracks[op] = tracks
        assets, area = assets_for(family)
        total_area += area * len(locations)
        realization['assets'].update(assets)
        components = []
        for asset in assets:
            port = asset.rsplit('port-',1)[1] if '/port-' in asset else None
            component = dict(id=asset.split('/')[1],meshAssetId=asset,role='port' if port is not None else 'body',
                             placementFrames=frames,operationTracks=operation_tracks)
            if port is not None:
                component['portId'] = port
            else:
                component['materialBindingId'] = 'ink' if asset.endswith('/mark') else 'body'
            components.append(component)
        realization['componentsByDomain'][family] = components
    for name in CENTERS:
        for suffix in ['',"'",'2']:
            op = name+suffix
            inverse = name+("'" if suffix == '' else '' if suffix == "'" else '2')
            source['requests'].append(dict(id=op,relation={'all':[
                {'chosen':dict(domain=family,moves=[op])} for family in PROTOTYPES]},
                profiles=[dict(id='default',choices={family:op for family in PROTOTYPES})],defaultProfile='default'))
            catalog['requests'][op] = dict(inverse=inverse,family=name,event=op+'.motion',profile='default',unavailablePlacements=[])
    source['invariant'], source['goal'] = {'all':invariants}, {'all':goal}
    assert counts == dict(face=4,edge=8,vertex=16,**{'vertex-core':4}), counts
    assert abs(total_area-4) < 1e-9, ('arrangement area',total_area)
    for domain in source['domains']:
        moves = {m['id']:m['transport'] for m in domain['moves']}
        for op, transport in moves.items():
            inverse = moves[catalog['requests'][op]['inverse']]
            assert all(inverse[v] == k for k,v in transport.items())
    total = sum(len(a['triangles']) for a in realization['assets'].values())
    assert total <= 5000
    return source, catalog, realization, dict(pieceCounts=counts,uniqueTriangles=total,cutRadius=RADIUS)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--native', default='build/native/twisty')
    args = parser.parse_args()
    for topology in ['torus','klein-bottle']:
        source,catalog,realization,manifest = generate(topology)
        package = dict(kind='relational-package',formatVersion=1,source=source,catalog=catalog)
        with tempfile.TemporaryDirectory(prefix='twisty-flat-') as directory:
            path = Path(directory)/'package.json'
            path.write_text(json.dumps(package))
            result = json.loads(subprocess.check_output([str(ROOT/args.native),'compile','--definition',str(path),'--json'],text=True))
            assert result['status'] == 'Compiled', result
        digest = result['definition']['definitionDigest']
        realization['compatibleDefinitionDigest'] = digest
        manifest.update(definitionDigest=digest,topology=topology)
        documents = {'puzzle.pzl.json':source,'bindings.json':catalog,
                     'package.json':package,'flat-quotient.json':realization,'manifest.json':manifest}
        folder = ROOT/'packages'/('flat-'+topology)
        if args.write:
            folder.mkdir(parents=True,exist_ok=True)
        for name, document in documents.items():
            path = folder/name
            if args.write:
                path.write_text(json.dumps(document,indent=2,sort_keys=True)+'\n')
            elif not path.exists() or json.loads(path.read_text()) != document:
                raise SystemExit(f'{path.relative_to(ROOT)} is stale; regenerate with --write')
        print(f'{topology}: 32 pieces, {manifest["uniqueTriangles"]} triangles, {digest}')


if __name__ == '__main__':
    main()
