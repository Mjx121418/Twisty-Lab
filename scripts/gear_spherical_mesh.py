"""Bounded symmetry-sector remeshing of the supplied spherical gear prototypes.

Works on checked-in meshes, using only the Python standard library. A sector is
clipped and simplified before reflection/rotation copies are made. Material
patches remain separate assets; their union has the complete piece tessellation.
"""
from math import atan2, ceil, hypot, sqrt
from gear_symmetry_mesh import (EPS, ANGLES, add, sub, scale, dot, cross, length,
                                sector, pack, cog_transform, edge_transform, rigid_sectors,
                                triangulate)
from gear_symmetry_mesh import verify as verify_symmetry

COG_RIM_WIDTH = .016
COG_SEAM_HALF_WIDTH = .011
PLAIN_EDGE_RIM_WIDTH = .016
PLAIN_EDGE_SEAM_HALF_WIDTH = .011
PLAIN_EDGE_CORNER_CLEARANCE = .018


def edge_project(p):
    return (sqrt(2)*p[0]/(p[1]+p[2]), (p[1]-p[2])/(p[1]+p[2]))


def plain_edge_outline(triangles, radius):
    """Shared center-facing tip and straight sides, in gnomonic coordinates."""
    # Recover the physical outline from the walls, independently of the atlas.
    candidates = set()
    for tri in triangles['edge/body']:
        radii = list(map(length, tri))
        if min(radii) < .965*radius and max(radii) > .985*radius:
            candidates.update(tuple(round(v, 10) for v in edge_project(p))
                              for p, r in zip(tri, radii)
                              if r > .985*radius and p[0] >= -EPS and p[1] >= p[2]-EPS)
    outline = sorted(candidates, key=lambda p: atan2(p[1], p[0]))
    assert abs(outline[0][1]) < EPS and abs(outline[-1][0]) < EPS
    # Leave a narrow cut beside the closest corner surface. Using the original
    # edge's seam width here leaves an unnecessarily broad gap along the side.
    corner_limit = min(edge_project(p)[0] for name, faces in triangles.items()
                       if name.startswith('corner/') for tri in faces for p in tri
                       if length(p) > .985*radius)
    width = corner_limit-PLAIN_EDGE_CORNER_CLEARANCE
    tip = max(range(len(outline)), key=lambda i: outline[i][1])
    a, b = outline[tip-1:tip+1]
    assert a[0] > width > b[0], 'Plain edge side must intersect the source end'
    end = a[1]+(b[1]-a[1])*(width-a[0])/(b[0]-a[0])
    return [(width, 0), (width, end)]+outline[tip:]


def inset_edge_outline(outline, previous, following):
    """Offset physical edges, continuing across the two symmetry boundaries."""
    inset = []
    extended = [previous]+outline+[following]
    for a, b, c in zip(extended, extended[1:], extended[2:]):
        u, v = (b[0]-a[0], b[1]-a[1]), (c[0]-b[0], c[1]-b[1])
        u, v = tuple(x/hypot(*u) for x in u), tuple(x/hypot(*v) for x in v)
        p = (b[0]-u[1]*PLAIN_EDGE_RIM_WIDTH, b[1]+u[0]*PLAIN_EDGE_RIM_WIDTH)
        q = (b[0]-v[1]*PLAIN_EDGE_RIM_WIDTH, b[1]+v[0]*PLAIN_EDGE_RIM_WIDTH)
        denominator = u[0]*v[1]-u[1]*v[0]
        t = 0 if abs(denominator) < EPS else ((q[0]-p[0])*v[1]-(q[1]-p[1])*v[0])/denominator
        inset.append((p[0]+t*u[0], p[1]+t*u[1]))
    return inset


def sample_edge_outline(outline, inset):
    outer, inner = [outline[0]], [inset[0]]
    for a, b, c, d in zip(outline, outline[1:], inset, inset[1:]):
        steps = ceil(hypot(b[0]-a[0], b[1]-a[1])/.07)
        for step in range(1, steps+1):
            t = step/steps
            outer.append(tuple(x+t*(y-x) for x, y in zip(a, b)))
            inner.append(tuple(x+t*(y-x) for x, y in zip(c, d)))
    return outer, inner


def plain_edge_mesh(triangles, radius):
    """Straighten the corner-facing sides in the edge's gnomonic plane.

    A constant transverse coordinate projects to a smooth great-circle arc,
    like the nearly straight sides of the cubical plain edge. Size the sides
    against the adjacent corner, retaining the source notch and shell radii.
    """
    def on_shell(p, shell=1):
        x, y = p
        factor = shell*radius/sqrt(1+x*x+y*y)
        return (factor*x, factor*(1+y)/sqrt(2), factor*(1-y)/sqrt(2))

    outline = plain_edge_outline(triangles, radius)
    width, end = outline[1]
    inset = inset_edge_outline(outline, (width, -end), (-outline[-2][0], outline[-2][1]))
    outer, inner = sample_edge_outline(outline, inset)

    result = {name: [] for name in ['edge/body', 'edge/port-0', 'edge/port-1']}

    def emit(points, name):
        for flip in [-1, 1]:
            for swap in [False, True]:
                target = 'edge/port-1' if swap and name == 'edge/port-0' else name
                tri = [edge_transform(p, flip, swap) for p in points]
                result[target].append(tri if flip*(-1 if swap else 1) > 0 else tri[::-1])

    def cap_triangle(tri, shell=1, color=False):
        polygons = [('edge/body', tri)]
        if color:
            polygons = []
            for sign, name in [(-1, 'edge/body'), (1, 'edge/port-0')]:
                poly = []
                for a, b in zip(tri, tri[1:]+tri[:1]):
                    da, db = a[1]-PLAIN_EDGE_SEAM_HALF_WIDTH, b[1]-PLAIN_EDGE_SEAM_HALF_WIDTH
                    if sign*da >= -EPS:
                        poly.append(a)
                    if da > EPS and db < -EPS or da < -EPS and db > EPS:
                        t = da/(da-db)
                        poly.append((a[0]+t*(b[0]-a[0]), PLAIN_EDGE_SEAM_HALF_WIDTH))
                polygons.append((name, poly))
        for name, poly in polygons:
            for i in range(1, len(poly)-1):
                flat = [poly[0], poly[i], poly[i+1]]
                area = ((flat[1][0]-flat[0][0])*(flat[2][1]-flat[0][1])-
                        (flat[1][1]-flat[0][1])*(flat[2][0]-flat[0][0]))
                assert area >= -EPS, 'Plain edge cap must retain outward winding'
                if area > EPS:
                    points = [on_shell(p, shell) for p in flat]
                    emit(points if shell == 1 else points[::-1], name)

    for boundary, shell, color in [(inner, 1, True), (outer, .945, False)]:
        rings = [[(p[0]*f, p[1]*f) for p in boundary] for f in [1/3, 2/3, 1]]
        for i in range(len(boundary)-1):
            cap_triangle([(0, 0), rings[0][i], rings[0][i+1]], shell, color)
            for a, b in zip(rings, rings[1:]):
                cap_triangle([a[i], b[i], b[i+1]], shell, color)
                cap_triangle([a[i], b[i+1], a[i+1]], shell, color)
    for a, b, c, d in zip(inner, inner[1:], outer, outer[1:]):
        cap_triangle([a, c, d])
        cap_triangle([a, d, b])
        emit([on_shell(c), on_shell(c, .945), on_shell(d)], 'edge/body')
        emit([on_shell(d), on_shell(c, .945), on_shell(d, .945)], 'edge/body')
    return result


def carrier_wall_outline(triangles, radius):
    """Walk the physical quarter-outline from the cog-facing boundary."""
    neighbors = {}
    for tri in triangles:
        radii = list(map(length, tri))
        if min(radii) >= .98*radius:
            continue
        points = [tuple(round(v, 10) for v in edge_project(p))
                  for p, r in zip(tri, radii) if r > .985*radius]
        if len(points) == 2 and all(p[0] >= -EPS and p[1] >= -EPS for p in points):
            a, b = points
            neighbors.setdefault(a, set()).add(b)
            neighbors.setdefault(b, set()).add(a)
    start = min((p for p, adjacent in neighbors.items() if len(adjacent) == 1),
                key=lambda p: p[1])
    source = [start]
    while following := neighbors[source[-1]]-set(source):
        assert len(following) == 1, 'Carrier outline must be a single physical boundary'
        source.append(following.pop())
    assert len(source) == len(neighbors)
    return source


def carrier_mesh(triangles, radius):
    """Use the plain edge's tip and straight sides beside the cog opening."""
    source = carrier_wall_outline(triangles['gear-edge/carrier'], radius)
    bottom = min(range(len(source)), key=lambda i: source[i][1])
    a, b = source[bottom-1:bottom+1]
    tip = plain_edge_outline(triangles, radius)
    width = tip[0][0]
    assert a[0] < b[0] < width
    # Continue the cog-facing segment to the common side plane. Keeping part
    # of the source's sloping side would introduce a kink before the shared tip.
    side_start = (width, a[1]+(b[1]-a[1])*(width-a[0])/(b[0]-a[0]))
    outline = source[:bottom]+[side_start]+tip[1:]
    inset = inset_edge_outline(outline, (-outline[1][0], outline[1][1]),
                               (-outline[-2][0], outline[-2][1]))
    outer, inner = sample_edge_outline(outline, inset)
    result = {name: [] for name in ['gear-edge/carrier', 'gear-edge/carrier-port-0',
                                   'gear-edge/carrier-port-1']}

    def on_shell(p, shell=1):
        x, y = p
        factor = shell*radius/sqrt(1+x*x+y*y)
        return (factor*x, factor*(1+y)/sqrt(2), factor*(1-y)/sqrt(2))

    def emit(points, color=False):
        for flip in [-1, 1]:
            for swap in [False, True]:
                name = 'gear-edge/carrier-port-'+str(int(swap)) if color else 'gear-edge/carrier'
                tri = [edge_transform(p, flip, swap) for p in points]
                result[name].append(tri if flip*(-1 if swap else 1) > 0 else tri[::-1])

    def cap_triangle(tri, shell=1, color=False):
        points = [on_shell(p, shell) for p in tri]
        emit(points if shell == 1 else points[::-1], color)

    for boundary, shell, color in [(inner, 1, True), (outer, .945, False)]:
        points = [(p[0], p[1], 0) for p in boundary]
        faces = triangulate(list(range(len(points))), points, (0, 0, 1))
        assert faces is not None, 'Carrier cap must have a simple outline'
        for face in faces:
            tri = [boundary[i] for i in face]
            center = tuple(sum(p[i] for p in tri)/3 for i in range(2))
            for a, b in zip(tri, tri[1:]+tri[:1]):
                cap_triangle([a, b, center], shell, color)
    for a, b, c, d in zip(inner, inner[1:], outer, outer[1:]):
        cap_triangle([a, c, d])
        cap_triangle([a, d, b])
        emit([on_shell(c), on_shell(c, .945), on_shell(d)])
        emit([on_shell(d), on_shell(c, .945), on_shell(d, .945)])
    return result


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
    output = rigid_sectors(assets)
    plain = plain_edge_mesh(output, radius)
    carrier = carrier_mesh(output, radius)
    output.update(plain)
    output.update(carrier)
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


def verify_carrier_tip(assets, radius):
    """Compare physical and colored center-facing profiles independently.

    The center is shared by both models, so equal tip profiles also give equal
    center gaps. Sample across the complete width, including the central notch.
    """
    def cap(name):
        asset = assets[name]
        return [[edge_project(asset['vertices'][v]) for v in tri]
                for tri in asset['triangles']
                if all(length(asset['vertices'][v]) > .98*radius for v in tri)]

    def profile(triangles, x):
        hits = []
        for tri in triangles:
            for a, b in zip(tri, tri[1:]+tri[:1]):
                if min(a[0], b[0])-EPS <= x <= max(a[0], b[0])+EPS:
                    if abs(b[0]-a[0]) < EPS:
                        hits.extend([a[1], b[1]])
                    else:
                        hits.append(a[1]+(b[1]-a[1])*(x-a[0])/(b[0]-a[0]))
        assert hits, 'Edge tip profile must cover its full width'
        return max(hits)

    for plain, carrier in [('edge/body', 'gear-edge/carrier'),
                           ('edge/port-0', 'gear-edge/carrier-port-0')]:
        a, b = cap(plain), cap(carrier)
        width = min(max(p[0] for tri in a for p in tri),
                    max(p[0] for tri in b for p in tri))
        for i in range(101):
            x = (width-1e-8)*i/100
            assert abs(profile(a, x)-profile(b, x)) < 1e-8, \
                (plain, carrier, 'Center-facing tip and border must match', x)

    body = assets['gear-edge/carrier']
    outline = carrier_wall_outline([[body['vertices'][v] for v in tri]
                                    for tri in body['triangles']], radius)
    bottom = min(range(len(outline)), key=lambda i: outline[i][1])
    width = max(p[0] for p in outline)
    end = next(i for i in range(bottom+1, len(outline)) if outline[i][0] < width-1e-8)
    assert end > bottom+1 and all(abs(p[0]-width) < 1e-8 for p in outline[bottom:end]), \
        'Carrier short sides must each form one uninterrupted great-circle arc'


def verify(assets):
    groups = verify_symmetry(assets, cog_order=6)
    verify_cog_separator(assets)
    # The long corner-facing walls must lie in a single radial plane on each
    # side, including the join between the two colored faces. This catches the
    # former step/bulge independently of the tessellation symmetry audit.
    body = assets['edge/body']
    radius = max(map(length, body['vertices']))
    verify_carrier_tip(assets, radius)
    sides = []
    for triangle in body['triangles']:
        points = [body['vertices'][v] for v in triangle]
        radii = list(map(length, points))
        if min(radii) < .965*radius and max(radii) > .985*radius:
            sides.extend((abs(sqrt(2)*p[0]/(p[1]+p[2])), (p[1]-p[2])/(p[1]+p[2]))
                         for p in points if abs((p[1]-p[2])/(p[1]+p[2])) < .45)
    assert len(sides) >= 24, 'Plain edge must retain both corner-facing walls'
    widths = [p[0] for p in sides]
    assert max(widths)-min(widths) < 1e-9, 'Plain edge sides must be smooth great-circle arcs'
    corner_limit = min(sqrt(2)*p[0]/(p[1]+p[2]) for name, asset in assets.items()
                       if name.startswith('corner/') for p in asset['vertices']
                       if length(p) > .985*radius)
    assert abs(corner_limit-max(widths)-PLAIN_EDGE_CORNER_CLEARANCE) < 1e-9, \
        'Plain edge must leave the intended narrow clearance beside the corners'
    return groups
