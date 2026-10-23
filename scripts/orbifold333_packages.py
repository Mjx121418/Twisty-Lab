#!/usr/bin/env python3
"""Author three- and six-disk 333 orbifolds on exact triangular lattices."""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import subprocess
import tempfile

import hex_surface_packages as grid

ROOT = Path(__file__).resolve().parent.parent
IDENTITY, ROTATIONS = grid.IDENTITY, grid.ROTATIONS


class Orbifold333:
    def __init__(self, six):
        self.six = six
        self.multiplier = 2 if six else 1
        self.scale = 1 / self.multiplier
        self.radius = .36 if six else .72
        self.names = 'ABCDEF' if six else 'ABC'
        n = 6 * self.multiplier
        self.centers = dict(zip(self.names, [(0, 0), (n, 0), (0, n), (6, 0), (6, 6), (0, 6)]))
        self.periods = ((2*n, -n), (n, n))

    def world(self, t):
        return tuple(v*self.scale for v in grid.cart(t))

    def frame(self, t, q):
        frame = grid.frame((0, 0), q)
        frame[12], frame[13] = self.world(t)
        return frame

    def canonical(self, t, q):
        candidates = []
        n = 6 * self.multiplier
        for rotation in ROTATIONS[::2]:
            p, orientation = grid.mv(rotation, t), grid.mm(rotation, q)
            ix, iy = -(p[0]-p[1])//(3*n), -(p[0]+2*p[1])//(3*n)
            for i in range(ix-1, ix+2):
                for j in range(iy-1, iy+2):
                    anchor = tuple(p[k]+i*self.periods[0][k]+j*self.periods[1][k] for k in (0, 1))
                    if not all(0 <= v <= n for v in anchor):
                        continue
                    frame = self.frame(anchor, orientation)
                    score = tuple(round(frame[k]*1e9) for k in (12, 13, 0, 1, 4, 5))
                    candidates.append((score, anchor, orientation))
        assert candidates, (t, q)
        _, anchor, orientation = min(candidates)
        return anchor, orientation

    def location(self, t, q, family):
        shape = set(grid.PROTOTYPES[family])
        return min(self.canonical(t, grid.mm(q, r)) for r in ROTATIONS
                   if {grid.mv(r, p) for p in shape} == shape)

    def circle_name(self, c):
        anchor = self.canonical(c, IDENTITY)[0]
        return next(name for name, center in self.centers.items()
                    if self.canonical(center, IDENTITY)[0] == anchor)

    def assets(self, family, cone):
        poly = grid.boundary(family)
        _, center, _ = grid.star_mesh(poly)
        inset = [tuple(center[i]+(p[i]-center[i])*.94 for i in (0, 1)) for p in poly]
        def sector(polygon):
            if not cone:
                return polygon
            polygon = grid.half_plane(polygon, (0, -1), 0)
            return grid.half_plane(polygon, (-math.sqrt(3)/2, -.5), 0)
        body = sector(poly)
        assets = {'body': grid.star_mesh(body)[0]}
        neighbors = [(6, 0), (0, 6), (-6, 6), (-6, 0), (0, -6), (6, -6)]
        parents = {}
        for parent, c in enumerate(grid.PROTOTYPES[family]):
            c, patch = grid.cart(c), inset
            for neighbor in neighbors:
                normal = grid.cart(neighbor)
                patch = grid.half_plane(patch, normal, .5+sum(a*b for a, b in zip(normal, c)))
            patch = sector(patch)
            if not self.six:
                parts = [patch]
            elif cone:
                # A cone's 120-degree sector consists of one triangle from
                # each side of the pillow, meeting along the 60-degree ray.
                parts = [grid.half_plane(patch, (sign*math.sqrt(3)/2, -sign*.5), 0)
                         for sign in (-1, 1)]
            elif family == 'face':
                # Every ordinary center lies on a coarse triangle edge. Six
                # local wedges allow its differently oriented seam to use
                # the same rigid mesh templates as the other centers.
                parts = []
                for k in range(6):
                    part = patch
                    for angle, sign in [(k*math.pi/3, 1), ((k+1)*math.pi/3, -1)]:
                        part = grid.half_plane(part, (sign*math.sin(angle), -sign*math.cos(angle)), 0)
                    parts.append(part)
            elif family == 'edge':
                # Edges on the pillow seam straddle the local x axis. An
                # internal edge uses the same shade on both of its halves.
                parts = [grid.half_plane(patch, (0, sign), 0) for sign in (-1, 1)]
            else:
                parts = [patch]
            for part in parts:
                port = str(len(parents))
                parents[port] = parent
                assets[f'port-{port}'] = grid.star_mesh(part)[0]
        for asset in assets.values():
            asset['vertices'] = [[round(x*self.scale, 12), round(y*self.scale, 12), 0]
                                 for x, y, _ in asset['vertices']]
        area = abs(sum(a[0]*b[1]-b[0]*a[1] for a, b in zip(body, body[1:]+body[:1])))/2
        return assets, area*self.scale**2, parents

    @staticmethod
    def shade(point):
        """The two coarse equilateral-triangle types survive every deck map."""
        x, y = point[:2]
        u, v = x-y/math.sqrt(3), 2*y/math.sqrt(3)
        return 'light' if u-math.floor(u)+v-math.floor(v) < 1 else 'dark'

    def port_shade(self, asset, frame):
        def world(p):
            x, y = p[:2]
            return (frame[0]*x+frame[4]*y+frame[12], frame[1]*x+frame[5]*y+frame[13])
        shade = self.shade(world(asset['vertices'][-1]))
        # A rigid patch must never cross the solved front/back shade boundary.
        for triangle in asset['triangles']:
            center = [sum(asset['vertices'][i][axis] for i in triangle)/3 for axis in (0, 1)]
            assert self.shade(world(center)) == shade
        return shade

    def operations(self, name):
        return [('', -1), ("'", 1), ('2', -2)]+([("2'", 2), ('3', -3)] if name in 'DEF' else [])

    def generate(self):
        grid.RADIUS = self.radius/self.scale
        puzzle_id = 'flat-333-6-v1' if self.six else 'flat-333-v1'
        source = dict(kind='relational-puzzle', languageVersion=1, id=puzzle_id,
                      domains=[], predicates={}, requests=[])
        catalog = dict(kind='relational-port-bindings', version=1, puzzleId=puzzle_id,
                       resources=[], domains=[], pieces={}, placements={}, requests={})
        realization = dict(schemaVersion=1, id=puzzle_id, kind='flat-quotient',
                           requiredCapabilities=['triangle-meshes', 'rigid-transforms', 'quotient-surface', 'quotient-orbifold'],
                           flatSurface=dict(topology='orbifold-333', chart='orbifold-333-rhombus', sideLength=1,
                                            cutRadius=self.radius,
                                            diskCenters={name: list(self.world(c)) for name, c in self.centers.items()}),
                           assets={}, componentsByDomain={}, fidelity='flat-333-circle-arrangement')
        invariants, goals, counts, total_area = [], [], {}, 0
        domains = [('cone', 'face')]+([('face', 'face')] if self.six else [])+[('edge', 'edge'), ('vertex', 'vertex')]
        for domain, family in domains:
            assets, area, parents = self.assets(family, domain == 'cone')
            shape, poses = grid.PROTOTYPES[family], set()
            for x in range(18*self.multiplier):
                for y in range(18*self.multiplier):
                    if family == 'face' and (x % 6 or y % 6 or (self.circle_name((x, y)) in 'ABC') != (domain == 'cone')):
                        continue
                    for q in ROTATIONS:
                        if all((x+grid.mv(q, p)[0]) % 6 == 0 and (y+grid.mv(q, p)[1]) % 6 == 0 for p in shape):
                            poses.add(self.canonical((x, y), q))
            poses = sorted(poses)
            by_key = {grid.key(t, q): (t, q) for t, q in poses}
            locations = sorted({self.location(t, q, family) for t, q in poses})
            indices = {p: i for i, p in enumerate(locations)}
            counts[domain] = len(locations)
            total_area += area*len(locations)
            members, groups = [], defaultdict(list)
            ports = list(parents)
            for t, q in poses:
                key = grid.key(t, q)
                loc = f'location/{domain}/{indices[self.location(t, q, family)]:02}'
                groups[loc].append(domain+'/'+key)
                catalog['placements'][domain+'/'+key] = dict(resources=[loc], ports={
                    port: dict(cell=loc, attachment=self.circle_name(
                        (t[0]+grid.mv(q, shape[parent])[0], t[1]+grid.mv(q, shape[parent])[1])))
                    for port, parent in parents.items()})
            for i, loc in enumerate(locations):
                name, key = f'{domain}-{i:02}', grid.key(*loc)
                members.append(dict(id=name, initial=key))
                bindings = catalog['placements'][domain+'/'+key]['ports']
                catalog['pieces'][name] = dict(domain=domain, home=domain+'/'+key,
                                               portLabels={p: bindings[p]['attachment']+('-'+self.port_shade(
                                                   assets['port-'+p], self.frame(*loc)) if self.six else '') for p in ports})
                goals.append({'at': dict(piece=name, placements=[domain+'/'+key])})
            for loc, placements in groups.items():
                catalog['resources'].append(loc)
                invariants.append({'count': dict(min=1, max=1, of=[
                    {'at': dict(piece=p['id'], placements=placements)} for p in members])})
            catalog['domains'].append(dict(id=domain, pieces=[p['id'] for p in members],
                                           placements=[domain+'/'+k for k in by_key], localPorts=ports))
            moves, tracks = [], {}
            for name in self.names:
                for suffix, turns in self.operations(name):
                    operation, transport, paths = name+suffix, {}, {}
                    rotation = ROTATIONS[turns % 6]
                    for key, (t, q) in by_key.items():
                        circles = [(t[0]+grid.mv(q, p)[0], t[1]+grid.mv(q, p)[1]) for p in shape]
                        driving = [c for c in circles if self.circle_name(c) == name]
                        if not driving:
                            continue
                        assert len(driving) == 1
                        c = driving[0]
                        delta = grid.mv(rotation, (t[0]-c[0], t[1]-c[1]))
                        target = self.canonical((c[0]+delta[0], c[1]+delta[1]), grid.mm(rotation, q))
                        dest = grid.key(*target)
                        assert dest in by_key, (domain, operation, key, dest)
                        transport[key] = dest
                        paths[key] = [dict(axis=[0, 0, 1], pivot=[*self.world(c), 0],
                                           angle=turns*math.pi/3, space='world')]
                    assert set(transport) == set(transport.values())
                    moves.append(dict(id=operation, support=list(transport), transport=transport, event=operation+'.motion'))
                    tracks[operation] = paths
            source['domains'].append(dict(id=domain, placements=list(by_key), pieces=members, moves=moves))
            frames = {k: self.frame(t, q) for k, (t, q) in by_key.items()}
            components = []
            for component, asset in assets.items():
                asset_id = domain+'/'+component
                realization['assets'][asset_id] = asset
                binding = dict(id=component, meshAssetId=asset_id, role='port' if component.startswith('port-') else 'body',
                               placementFrames=frames, operationTracks=tracks)
                if component.startswith('port-'):
                    binding['portId'] = component[5:]
                else:
                    binding['materialBindingId'] = 'body'
                components.append(binding)
            realization['componentsByDomain'][domain] = components
        for name in self.names:
            for suffix, _ in self.operations(name):
                operation = name+suffix
                inverse = name+{'': "'", "'": '', '2': "2'" if name in 'DEF' else '2', "2'": '2', '3': '3'}[suffix]
                source['requests'].append(dict(id=operation, relation={'all': [
                    {'chosen': dict(domain=d['id'], moves=[operation])} for d in source['domains']]},
                    profiles=[dict(id='default', choices={d['id']: operation for d in source['domains']})], defaultProfile='default'))
                catalog['requests'][operation] = dict(inverse=inverse, family=name, event=operation+'.motion',
                                                       profile='default', unavailablePlacements=[])
        source['invariant'], source['goal'] = {'all': invariants}, {'all': goals}
        expected = dict(cone=3, edge=3, vertex=2) if not self.six else dict(cone=3, face=3, edge=12, vertex=8)
        assert counts == expected, counts
        assert abs(total_area-math.sqrt(3)/2) < 1e-9, total_area
        for d in source['domains']:
            maps = {m['id']: m['transport'] for m in d['moves']}
            for operation, transport in maps.items():
                inverse = maps[catalog['requests'][operation]['inverse']]
                assert all(inverse[v] == k for k, v in transport.items())
        manifest = dict(pieceCounts=counts, cutRadius=self.radius, topology='orbifold-333', diskCount=len(self.names),
                        uniqueTriangles=sum(len(a['triangles']) for a in realization['assets'].values()))
        return source, catalog, realization, manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--native', default='build/native/twisty')
    args = parser.parse_args()
    for six in (False, True):
        source, catalog, realization, manifest = Orbifold333(six).generate()
        package = dict(kind='relational-package', formatVersion=1, source=source, catalog=catalog)
        with tempfile.TemporaryDirectory(prefix='twisty-333-authoring-') as temp:
            path = Path(temp)/'package.json'
            path.write_text(json.dumps(package))
            result = json.loads(subprocess.check_output(
                [str(ROOT/args.native), 'compile', '--definition', str(path), '--json'], text=True))
        assert result['status'] == 'Compiled', result
        digest = result['definition']['definitionDigest']
        realization['compatibleDefinitionDigest'] = manifest['definitionDigest'] = digest
        folder = ROOT/'packages'/('flat-333-6' if six else 'flat-333')
        if args.write:
            folder.mkdir(parents=True, exist_ok=True)
        for name, document in {'puzzle.pzl.json': source, 'bindings.json': catalog, 'package.json': package,
                               'flat-quotient.json': realization, 'manifest.json': manifest}.items():
            path = folder/name
            if args.write:
                path.write_text(json.dumps(document, indent=2, sort_keys=True)+'\n')
            elif not path.exists() or json.loads(path.read_text()) != document:
                raise SystemExit(f'{path.relative_to(ROOT)} is stale; regenerate with --write')
        print(f'{folder.name}: {sum(manifest["pieceCounts"].values())} pieces, {manifest["uniqueTriangles"]} triangles, {digest}')


if __name__ == '__main__':
    main()
