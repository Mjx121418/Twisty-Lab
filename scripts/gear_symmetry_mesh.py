"""Shared, dependency-free sector meshing and full tessellation symmetry audit."""
from itertools import permutations
from math import atan2, hypot, sqrt

EPS = 1e-10


def add(a, b): return tuple(x+y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))
def scale(a, t): return tuple(x*t for x in a)
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def length(a): return sqrt(dot(a, a))
def key(p): return tuple(round(x, 12) for x in p)


def clip(poly, normal):
    result = []
    for a, b in zip(poly, poly[1:]+poly[:1]):
        da, db = dot(a, normal), dot(b, normal)
        if da >= -EPS:
            result.append(a)
        if da > EPS and db < -EPS or da < -EPS and db > EPS:
            p = add(a, scale(sub(b, a), da/(da-db)))
            # Put intersections on the plane consistently on both sides.
            p = sub(p, scale(normal, dot(p, normal)/dot(normal, normal)))
            result.append(p)
    clean = []
    for p in result:
        if not clean or length(sub(p, clean[-1])) > EPS:
            clean.append(p)
    if len(clean) > 1 and length(sub(clean[0], clean[-1])) <= EPS:
        clean.pop()
    return clean


def sector(assets, names, planes, transform=lambda p: p):
    result = {}
    for name in names:
        triangles = []
        asset = assets[name]
        for triangle in asset['triangles']:
            poly = [tuple(asset['vertices'][i]) for i in triangle]
            for plane in planes:
                poly = clip(poly, plane)
                if len(poly) < 3:
                    break
            if len(poly) >= 3:
                poly = [transform(p) for p in poly]
                for i in range(1, len(poly)-1):
                    tri = [poly[0], poly[i], poly[i+1]]
                    if length(cross(sub(tri[1], tri[0]), sub(tri[2], tri[0]))) > 1e-10:
                        triangles.append(tri)
        result[name] = simplify(triangles)
    return result


def triangulate(poly, points, normal):
    """Ear clipping in the existing plane; returns None for an ambiguous star."""
    poly = poly[:]
    result = []
    while len(poly) > 3:
        for i, b in enumerate(poly):
            a, c = poly[i-1], poly[(i+1) % len(poly)]
            pa, pb, pc = points[a], points[b], points[c]
            if dot(cross(sub(pb, pa), sub(pc, pb)), normal) <= 1e-12:
                continue
            def inside(v):
                p = points[v]
                return all(dot(cross(sub(y, x), sub(p, x)), normal) >= -1e-12
                           for x, y in [(pa, pb), (pb, pc), (pc, pa)])
            if any(inside(v) for v in poly if v not in [a, b, c]):
                continue
            result.append((a, b, c))
            poly.pop(i)
            break
        else:
            return None
    if len(poly) == 3:
        a, b, c = [points[v] for v in poly]
        if dot(cross(sub(b, a), sub(c, a)), normal) <= 1e-12:
            return None
        result.append(tuple(poly))
    return result


def simplify(triangles):
    """Remove redundant vertices in flat patches, never across a curved crease.

Closed planar vertex stars can lose their center. Open stars can lose a vertex
only when it lies on their straight closing edge. Material boundaries are kept.
"""
    points = []
    lookup = {}
    faces = {}
    neighbors = {}
    for tri in triangles:
        ids = []
        for p in tri:
            p = key(p)
            if p not in lookup:
                lookup[p] = len(points)
                points.append(p)
            ids.append(lookup[p])
        if len(set(ids)) < 3:
            continue
        i = len(faces)
        faces[i] = tuple(ids)
        for v in ids:
            neighbors.setdefault(v, set()).add(i)
    next_id = len(faces)
    # A bounded pass is enough for the redundant texture-clipping subdivisions.
    for v in range(len(points)):
        incident = neighbors.get(v, set()).copy()
        if len(incident) < 2 or len(incident) > 48:
            continue
        groups = []
        for i in sorted(incident):
            tri = faces[i]
            a, b, c = [points[w] for w in tri]
            n = cross(sub(b, a), sub(c, a))
            if length(n) <= 1e-10:
                break
            n = scale(n, 1/length(n))
            group = next((g for g in groups if dot(n, g[0]) > 1-1e-12), None)
            if group is None:
                group = [n, []]
                groups.append(group)
            group[1].append(i)
        else:
            if len(groups) > 2:
                continue
            replacement = []
            valid = True
            for n, ids in groups:
                links = {}
                incoming = {}
                for i in ids:
                    tri = faces[i]
                    k = tri.index(v)
                    a, b = tri[(k+1) % 3], tri[(k+2) % 3]
                    if a in links or b in incoming:
                        valid = False
                        break
                    links[a], incoming[b] = b, a
                if not valid:
                    break
                starts = set(links)-set(incoming)
                ends = set(incoming)-set(links)
                if len(starts) == len(ends) == 0:
                    start = min(links)
                elif len(starts) == len(ends) == 1:
                    start, end = min(starts), min(ends)
                    a, b = points[start], points[end]
                    edge = sub(b, a)
                    delta = sub(points[v], a)
                    if length(cross(edge, delta)) > 1e-9*length(edge) or not -EPS <= dot(delta, edge) <= dot(edge, edge)+EPS:
                        valid = False
                        break
                else:
                    valid = False
                    break
                poly = [start]
                while poly[-1] in links:
                    following = links[poly[-1]]
                    if following == start:
                        break
                    if following in poly:
                        valid = False
                        break
                    poly.append(following)
                if not valid or len(poly) != len(set(links)|set(incoming)):
                    valid = False
                    break
                new = triangulate(poly, points, n)
                if new is None:
                    valid = False
                    break
                # The old star and new polygon must have the same oriented area.
                def area(tris):
                    return sum(dot(cross(sub(points[t[1]], points[t[0]]),
                                         sub(points[t[2]], points[t[0]])), n) for t in tris)
                if abs(area(new)-area([faces[i] for i in ids])) > 1e-8:
                    valid = False
                    break
                replacement.extend(new)
            if valid and len(replacement) < len(incident):
                for i in sorted(incident):
                    for w in faces[i]:
                        neighbors[w].remove(i)
                    del faces[i]
                for tri in replacement:
                    faces[next_id] = tri
                    for w in tri:
                        neighbors.setdefault(w, set()).add(next_id)
                    next_id += 1
    return [[points[v] for v in faces[i]] for i in sorted(faces)]


def pack(triangles):
    points = []
    lookup = {}
    faces = []
    seen = set()
    for tri in triangles:
        ids = []
        for p in tri:
            p = tuple(round(x, 10) for x in p)
            if p not in lookup:
                lookup[p] = len(points)
                points.append(p)
            ids.append(lookup[p])
        if len(set(ids)) != 3:
            continue
        signature = tuple(sorted(ids))
        if signature not in seen:
            seen.add(signature)
            faces.append(ids)
    used = sorted({v for tri in faces for v in tri})
    renumber = {v: i for i, v in enumerate(used)}
    return {'vertices': [points[v] for v in used],
            'triangles': [[renumber[v] for v in tri] for tri in faces]}


def parity(order):
    return -1 if sum(order[i] > order[j] for i in range(3) for j in range(i+1, 3)) % 2 else 1


def permute(p, order): return tuple(p[i] for i in order)


def edge_transform(p, flip, swap):
    x, y, z = p
    return (flip*x, z, y) if swap else (flip*x, y, z)


def center_transform(p, sx, sz, swap):
    x, y, z = p
    return (sx*z, y, sz*x) if swap else (sx*x, y, sz*z)


ANGLES = [(1, 0), (.5, sqrt(3)/2), (-.5, sqrt(3)/2),
          (-1, 0), (-.5, -sqrt(3)/2), (.5, -sqrt(3)/2)]


def cog_transform(p, k, reflect):
    x, y, z = p
    a, b = (y+z)/sqrt(2), (y-z)/sqrt(2)
    if reflect:
        b = -b
    c, s = ANGLES[k]
    return (c*x-s*b, (a+s*x+c*b)/sqrt(2), (a-s*x-c*b)/sqrt(2))


def best_sector(assets, names, planes, choices, optimize):
    if not optimize or not names:
        return sector(assets, names, planes)
    candidates = []
    for transform, orientation, material in choices:
        transformed = {}
        for name in names:
            a = assets[name]
            transformed[material(name)] = {
                'vertices': [transform(p) for p in a['vertices']],
                'triangles': [t if orientation > 0 else t[::-1] for t in a['triangles']]}
        candidates.append(sector(transformed, list(transformed), planes))
    return min(candidates, key=lambda f: sum(len(t) for t in f.values()))


def rigid_sectors(assets, *, cubic_cog=False, optimize=False):
    """Repeat fundamental sectors, transporting labels and reflection winding."""
    output = {name: [] for name in assets if name != 'core-ball'}
    axes = [1, 0, 2]
    def corner_material(name, order):
        if '/port-' not in name:
            return name
        return 'corner/port-'+str(axes.index(order.index(axes[int(name[-1])])))

    names = [n for n in assets if n.startswith('corner/')]
    choices = [(lambda p, order=order: permute(p, order), parity(order),
                lambda n, order=order: corner_material(n, order)) for order in permutations(range(3))]
    fundamental = best_sector(assets, names, [(0, -1, 1), (-1, 1, 0)], choices, optimize)
    for order in permutations(range(3)):
        for name, triangles in fundamental.items():
            for tri in triangles:
                points = [permute(p, order) for p in tri]
                output[corner_material(name, order)].append(points if parity(order) > 0 else points[::-1])

    names = [n for n in assets if n.startswith('center/')]
    choices = [(lambda p, sx=sx, sz=sz, swap=swap: center_transform(p, sx, sz, swap),
                sx*sz*(-1 if swap else 1), lambda n: n)
               for sx in [1, -1] for sz in [1, -1] for swap in [False, True]]
    fundamental = best_sector(assets, names, [(1, 0, -1), (0, 0, 1)], choices, optimize)
    for sx in [-1, 1]:
        for sz in [-1, 1]:
            for swap in [False, True]:
                for name, triangles in fundamental.items():
                    for tri in triangles:
                        points = [center_transform(p, sx, sz, swap) for p in tri]
                        output[name].append(points if sx*sz*(-1 if swap else 1) > 0 else points[::-1])

    def edge_material(name, swap):
        return name[:-1]+str(1-int(name[-1])) if swap and 'port-' in name else name
    for prefix in ['edge/', 'gear-edge/carrier']+(['cog'] if cubic_cog else []):
        names = [n for n in assets if
                 (n.startswith('gear-edge/') and not n.startswith('gear-edge/carrier')
                  if prefix == 'cog' else n.startswith(prefix))]
        choices = [(lambda p, flip=flip, swap=swap: edge_transform(p, flip, swap),
                    flip*(-1 if swap else 1), lambda n, swap=swap: edge_material(n, swap))
                   for flip in [1, -1] for swap in [False, True]]
        fundamental = best_sector(assets, names, [(1, 0, 0), (0, 1, -1)], choices, optimize)
        for flip in [-1, 1]:
            for swap in [False, True]:
                for name, triangles in fundamental.items():
                    for tri in triangles:
                        points = [edge_transform(p, flip, swap) for p in tri]
                        output[edge_material(name, swap)].append(points if flip*(-1 if swap else 1) > 0 else points[::-1])
    return output


def verify(assets, *, cog_order=6):
    """Check full triangle connectivity, winding and transported port labels."""
    specifications = []
    axes = [1, 0, 2]
    for order in permutations(range(3)):
        def name_map(name, order=order):
            if '/port-' not in name:
                return name
            return 'corner/port-'+str(axes.index(order.index(axes[int(name[-1])])))
        specifications.append(('corner/', lambda p, order=order: permute(p, order), parity(order), name_map))
    for sx in [-1, 1]:
        for sz in [-1, 1]:
            for swap in [False, True]:
                specifications.append(('center/', lambda p, sx=sx, sz=sz, swap=swap: center_transform(p, sx, sz, swap),
                                       sx*sz*(-1 if swap else 1), lambda name: name))
    for prefix in ['edge/', 'gear-edge/carrier']:
        for flip in [-1, 1]:
            for swap in [False, True]:
                specifications.append((prefix, lambda p, flip=flip, swap=swap: edge_transform(p, flip, swap),
                                       flip*(-1 if swap else 1),
                                       lambda name, swap=swap: name[:-1]+str(1-int(name[-1])) if swap and 'port-' in name else name))
    for k in range(6) if cog_order == 6 else []:
        for reflect in [False, True]:
            # Spherical cog geometry is D6; arbitrary rotations do not preserve its colors.
            specifications.append(('cog', lambda p, k=k, reflect=reflect: cog_transform(p, k, reflect),
                                   -1 if reflect else 1, lambda name: 'cog'))
    for flip in [-1, 1]:
        for swap in [False, True]:
            specifications.append(('cog-colors', lambda p, flip=flip, swap=swap: edge_transform(p, flip, swap),
                                   flip*(-1 if swap else 1),
                                   lambda name, swap=swap: name[:-1]+str(1-int(name[-1])) if swap and 'port-' in name else name))
    cache = {}
    for prefix, transform, orientation, material in specifications:
        if prefix not in cache:
            names = [name for name in assets if
                     (name.startswith('gear-edge/') and not name.startswith('gear-edge/carrier')
                      if prefix in ['cog', 'cog-colors'] else name.startswith(prefix))]
            all_points = sorted({tuple(p) for name in names for p in assets[name]['vertices']})
            point_ids = {p: i for i, p in enumerate(all_points)}
            cells = {}
            for p, i in point_ids.items():
                cell = tuple(int(x/1e-8) for x in p)
                cells.setdefault(cell, []).append(i)
            triangles = set()
            for name in names:
                a = assets[name]
                for tri in a['triangles']:
                    ids = [point_ids[tuple(a['vertices'][i])] for i in tri]
                    cycles = [tuple(ids[i:]+ids[:i]) for i in range(3)]
                    triangles.add((name if prefix != 'cog' else 'cog', min(cycles)))
            cache[prefix] = (names, all_points, cells, triangles)
        names, points, cells, triangles = cache[prefix]
        def locate(p):
            cell = tuple(int(x/1e-8) for x in p)
            best = None
            distance = float('inf')
            for dx in [-1, 0, 1]:
                for dy in [-1, 0, 1]:
                    for dz in [-1, 0, 1]:
                        for i in cells.get((cell[0]+dx, cell[1]+dy, cell[2]+dz), []):
                            d = length(sub(p, points[i]))
                            if d < distance:
                                best, distance = i, d
            assert distance < 1e-9, (prefix, 'missing symmetry vertex', p)
            return best
        mapped = [locate(transform(p)) for p in points]
        point_ids = {p: i for i, p in enumerate(points)}
        for name in names:
            a = assets[name]
            for tri in a['triangles']:
                ids = [mapped[point_ids[tuple(a['vertices'][i])]] for i in tri]
                if orientation < 0:
                    ids.reverse()
                signature = (material(name), min(tuple(ids[i:]+ids[:i]) for i in range(3)))
                assert signature in triangles, (prefix, name, 'missing symmetry triangle', [a['vertices'][i] for i in tri], [points[i] for i in ids], orientation)
    return {'corner': 'S3 (C3 rotations and three mirrors), with port permutation',
            'center': 'D4 (fourfold rotations and mirrors)',
            'plainEdge': 'D2 (end mirror and face exchange), with port permutation',
            'edgeCarrier': 'D2 (end mirror and face exchange), with port permutation',
            'cogGeometry': ('D6 (sixfold rotations and mirrors), ignoring colors' if cog_order == 6
                            else 'D2 (end mirror and face exchange), with port permutation'),
            'cogColors': 'D2 (end mirror and face exchange), with port permutation',
            'centralBall': 'unchanged'}
