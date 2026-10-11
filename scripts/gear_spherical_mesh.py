"""Bounded symmetry-sector remeshing of the supplied spherical gear prototypes.

Works on checked-in meshes, using only the Python standard library. A sector is
clipped and simplified before reflection/rotation copies are made. Material
patches remain separate assets; their union has the complete piece tessellation.
"""
from itertools import permutations
from math import atan2, hypot, sqrt

EPS = 1e-10
COG_RIM_WIDTH = .016
COG_SEAM_HALF_WIDTH = .011


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


def cog_mesh(assets, radius):
    """A D6 shell with an inset rim and a continuous two-color separator.

Geometry is built independently of the imported texture subdivisions. All
three rotated separator bands cut the tessellation; only the horizontal band
is painted black. This preserves D6 connectivity and D2 transported colors.
"""
    names = ['gear-edge/cog', 'gear-edge/port-0', 'gear-edge/port-1']
    fundamental = sector(assets, names, [(0, -1, 1), (-sqrt(2/3), 1, -1)],
                         lambda p: (-p[0], p[2], p[1]))

    def project(p):
        a = (p[1]+p[2])/sqrt(2)
        return (p[0]/a, (p[1]-p[2])/(sqrt(2)*a))

    def on_shell(p, shell=1):
        x, b = p
        factor = shell*radius/sqrt(1+x*x+b*b)
        return (factor*x, factor*(1+b)/sqrt(2), factor*(1-b)/sqrt(2))

    # Wall vertices identify the actual tooth outline, independently of color.
    candidates = set()
    for triangles in fundamental.values():
        for tri in triangles:
            radii = list(map(length, tri))
            if min(radii) < .965*radius and max(radii) > .985*radius:
                candidates.update(tuple(round(v, 12) for v in project(p))
                                  for p, r in zip(tri, radii) if r > .985*radius)
    by_angle = {}
    for p in candidates:
        angle = round(atan2(p[1], p[0]), 9)
        if angle not in by_angle or hypot(*p) > hypot(*by_angle[angle]):
            by_angle[angle] = p
    half = [by_angle[a] for a in sorted(by_angle)]
    assert len(half) >= 3 and abs(half[0][1]) < 1e-8
    assert abs(half[-1][1]-half[-1][0]/sqrt(3)) < 1e-8
    # Texture intersections can leave extra collinear points along a wall.
    i = 1
    while i < len(half)-1:
        a, b, c = half[i-1:i+2]
        if abs((b[0]-a[0])*(c[1]-b[1])-(b[1]-a[1])*(c[0]-b[0])) < 1e-10:
            half.pop(i)
        else:
            i += 1

    # Offset the complete outline, then recover the corresponding half-sector.
    # A geometric inset gives an even rim without copying the central seam.
    def rotate2(p, k, reflect):
        x, y = p[0], -p[1] if reflect else p[1]
        c, s = ANGLES[k]
        return (c*x-s*y, s*x+c*y)

    def key2(p): return tuple(round(v, 10) for v in p)
    outline = {key2(rotate2(p, k, reflect)): rotate2(p, k, reflect)
               for p in half for k in range(6) for reflect in [False, True]}
    ring = sorted(outline.values(), key=lambda p: atan2(p[1], p[0]))
    rim_width, seam_half_width = COG_RIM_WIDTH, COG_SEAM_HALF_WIDTH
    inset = {}
    for i, b in enumerate(ring):
        a, c = ring[i-1], ring[(i+1) % len(ring)]
        u, v = (b[0]-a[0], b[1]-a[1]), (c[0]-b[0], c[1]-b[1])
        u, v = tuple(x/hypot(*u) for x in u), tuple(x/hypot(*v) for x in v)
        p = (b[0]-u[1]*rim_width, b[1]+u[0]*rim_width)
        q = (b[0]-v[1]*rim_width, b[1]+v[0]*rim_width)
        denominator = u[0]*v[1]-u[1]*v[0]
        if abs(denominator) < 1e-10:
            point = p
        else:
            t = ((q[0]-p[0])*v[1]-(q[1]-p[1])*v[0])/denominator
            point = (p[0]+t*u[0], p[1]+t*u[1])
        assert hypot(*point) < hypot(*b), 'Cog rim inset must remain inside the outline'
        inset[key2(b)] = point
    color_half = [inset[key2(p)] for p in half]

    def split(poly, normal, offset):
        """Split exactly along a material boundary in the gnomonic plane."""
        sides = []
        for sign in [-1, 1]:
            result = []
            for a, b in zip(poly, poly[1:]+poly[:1]):
                da = normal[0]*a[0]+normal[1]*a[1]-offset
                db = normal[0]*b[0]+normal[1]*b[1]-offset
                if sign*da >= -EPS:
                    result.append(a)
                if da > EPS and db < -EPS or da < -EPS and db > EPS:
                    t = da/(da-db)
                    result.append((a[0]+t*(b[0]-a[0]), a[1]+t*(b[1]-a[1])))
            clean = []
            for p in result:
                if not clean or hypot(p[0]-clean[-1][0], p[1]-clean[-1][1]) > EPS:
                    clean.append(p)
            if len(clean) > 1 and hypot(clean[0][0]-clean[-1][0], clean[0][1]-clean[-1][1]) <= EPS:
                clean.pop()
            if len(clean) >= 3:
                sides.append(clean)
        return sides

    result = {name: [] for name in names}

    def emit(tri, role, shell=1, reverse=False):
        # Split colored caps by the entire orbit of the straight seam boundaries.
        polygons = [tri]
        if role == 'color':
            for k in range(3):
                c, s = ANGLES[k]
                normal = (-s, c)
                for offset in [-seam_half_width, seam_half_width]:
                    polygons = [part for poly in polygons for part in split(poly, normal, offset)]
        for poly in polygons:
            for i in range(1, len(poly)-1):
                flat = [poly[0], poly[i], poly[i+1]]
                area = ((flat[1][0]-flat[0][0])*(flat[2][1]-flat[0][1])-
                        (flat[1][1]-flat[0][1])*(flat[2][0]-flat[0][0]))
                if abs(area) < 1e-12:
                    continue
                points = [on_shell(p, shell) for p in flat]
                if (area < 0) != reverse:
                    points.reverse()
                for k in range(6):
                    for reflect in [False, True]:
                        transformed = [cog_transform(p, k, reflect) for p in points]
                        target = 'gear-edge/cog'
                        if role == 'color':
                            b = sum(project(p)[1] for p in transformed)/3
                            if abs(b) > seam_half_width+EPS:
                                target = 'gear-edge/port-'+str(0 if b > 0 else 1)
                        result[target].append(transformed[::-1] if reflect else transformed)

    def cap(boundary, role, shell=1, reverse=False):
        rings = [[(p[0]*f, p[1]*f) for p in boundary] for f in [1/3, 2/3, 1]]
        for i in range(len(boundary)-1):
            emit([(0, 0), rings[0][i], rings[0][i+1]], role, shell, reverse)
            for inner, outer in zip(rings, rings[1:]):
                emit([inner[i], outer[i], outer[i+1]], role, shell, reverse)
                emit([inner[i], outer[i+1], inner[i+1]], role, shell, reverse)

    cap(color_half, 'color')
    cap(half, 'body', .945, True)
    for i in range(len(half)-1):
        emit([color_half[i], half[i], half[i+1]], 'body')
        emit([color_half[i], half[i+1], color_half[i+1]], 'body')
        a, b = half[i], half[i+1]
        for tri in [[on_shell(a), on_shell(a, .945), on_shell(b)],
                    [on_shell(b), on_shell(a, .945), on_shell(b, .945)]]:
            for k in range(6):
                for reflect in [False, True]:
                    p = [cog_transform(v, k, reflect) for v in tri]
                    result['gear-edge/cog'].append(p[::-1] if reflect else p)
    return result


def remesh(assets, radius):
    output = {name: [] for name in assets if name != 'core-ball'}
    # Corner S3: x <= y <= z is one sixth of the uncolored prototype.
    corner_names = [n for n in assets if n.startswith('corner/')]
    corner = sector(assets, corner_names, [(0, -1, 1), (-1, 1, 0)])
    axes = [1, 0, 2]  # U, R, F port order.
    for order in permutations(range(3)):
        for name, triangles in corner.items():
            target = name
            if '/port-' in name:
                axis = axes[int(name[-1])]
                target = 'corner/port-'+str(axes.index(order.index(axis)))
            for tri in triangles:
                points = [permute(p, order) for p in tri]
                output[target].append(points if parity(order) > 0 else points[::-1])
    # Center D4: one eighth, including mirrors as well as quarter turns.
    center_names = [n for n in assets if n.startswith('center/')]
    center = sector(assets, center_names, [(1, 0, -1), (0, 0, 1)])
    for sx in [-1, 1]:
        for sz in [-1, 1]:
            for swap in [False, True]:
                for name, triangles in center.items():
                    for tri in triangles:
                        points = [center_transform(p, sx, sz, swap) for p in tri]
                        output[name].append(points if sx*sz*(-1 if swap else 1) > 0 else points[::-1])
    # Plain and carrier D2: end reflection and exchange of adjacent faces.
    for prefix in ['edge/', 'gear-edge/carrier']:
        names = [n for n in assets if n.startswith(prefix)]
        fundamental = sector(assets, names, [(1, 0, 0), (0, 1, -1)])
        for flip in [-1, 1]:
            for swap in [False, True]:
                for name, triangles in fundamental.items():
                    target = name
                    if swap and 'port-' in name:
                        target = name[:-1]+str(1-int(name[-1]))
                    for tri in triangles:
                        points = [edge_transform(p, flip, swap) for p in tri]
                        output[target].append(points if flip*(-1 if swap else 1) > 0 else points[::-1])
    # Rebuild the cog shell independently of the imported color/seam layout.
    output.update(cog_mesh(assets, radius))
    result = {name: pack(triangles) for name, triangles in output.items()}
    result['core-ball'] = assets['core-ball']
    return result


def verify_cog_separator(assets):
    """Check a continuous black band, with the two colors immediately beside it.

    These samples catch the former pinched center even when geometry and material
    transport pass the symmetry audit. Check the complete width across the interior.
    """
    triangles = []
    names = ['gear-edge/cog', 'gear-edge/port-0', 'gear-edge/port-1']
    radius = max(length(p) for name in names for p in assets[name]['vertices'])
    for name in names:
        a = assets[name]
        for t in a['triangles']:
            points = [a['vertices'][v] for v in t]
            n = cross(sub(points[1], points[0]), sub(points[2], points[0]))
            if min(map(length, points)) > .98*radius and dot(n, add(add(points[0], points[1]), points[2])) > 0:
                flat = [(p[0]/((p[1]+p[2])/sqrt(2)), (p[1]-p[2])/(p[1]+p[2])) for p in points]
                triangles.append((name, flat))

    def contains(tri, p):
        return all((b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0]) >= -1e-12
                   for a, b in zip(tri, tri[1:]+tri[:1]))

    for i in range(-22, 23):
        x = i/100
        for y, expected in [(0, 'gear-edge/cog'),
                            (COG_SEAM_HALF_WIDTH/2, 'gear-edge/cog'),
                            (-COG_SEAM_HALF_WIDTH/2, 'gear-edge/cog'),
                            (COG_SEAM_HALF_WIDTH+.003, 'gear-edge/port-0'),
                            (-COG_SEAM_HALF_WIDTH-.003, 'gear-edge/port-1')]:
            materials = {name for name, t in triangles if contains(t, (x, y))}
            assert materials == {expected}, ('cog separator must be continuous', x, y, materials)


def verify(assets):
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
    for k in range(6):
        for reflect in [False, True]:
            # Cog geometry is D6; arbitrary rotations do not preserve its colors.
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
    verify_cog_separator(assets)
    return {'corner': 'S3 (C3 rotations and three mirrors), with port permutation',
            'center': 'D4 (fourfold rotations and mirrors)',
            'plainEdge': 'D2 (end mirror and face exchange), with port permutation',
            'edgeCarrier': 'D2 (end mirror and face exchange), with port permutation',
            'cogGeometry': 'D6 (sixfold rotations and mirrors), ignoring colors',
            'cogColors': 'D2 (end mirror and face exchange), with port permutation',
            'centralBall': 'unchanged'}
