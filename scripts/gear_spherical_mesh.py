"""Bounded symmetry-sector remeshing of the supplied spherical gear prototypes.

Works on checked-in meshes, using only the Python standard library. A sector is
clipped and simplified before reflection/rotation copies are made. Material
patches remain separate assets; their union has the complete piece tessellation.
"""
from math import atan2, hypot, sqrt
from gear_symmetry_mesh import (EPS, ANGLES, add, sub, scale, dot, cross, length,
                                sector, pack, cog_transform, rigid_sectors)
from gear_symmetry_mesh import verify as verify_symmetry

COG_RIM_WIDTH = .016
COG_SEAM_HALF_WIDTH = .011


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
    groups = verify_symmetry(assets, cog_order=6)
    verify_cog_separator(assets)
    return groups
