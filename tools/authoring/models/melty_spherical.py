"""Unit-sphere sections of the Melty cuts, in the cube's existing axis frame.

The colored patches use the six directed rays' angular Voronoi sectors. The finite mesh samples
small-circle boundaries; logical legality remains the existing solid model.
"""
from copy import deepcopy
import math

from .melty_model import add, sub, scale, dot, cross, unit

RADIUS = 2.05
DEPTH = 1/3
SEGMENTS = 64
RINGS = 5
PORT_INSET = .04
SURFACE_LIFT = .02
# Cube ports are L, R, B, F, D, U; ray indices are U, R, F, L, B, D.
PORT_AXES = (3, 1, 4, 2, 5, 0)


def inside(point, bounds, tolerance=1e-10):
    return all(dot(point, n) >= h-tolerance for n, h in bounds)


def corners(bounds):
    result = []
    for i, (a, ah) in enumerate(bounds):
        for b, bh in bounds[i+1:]:
            aa, bb, ab = dot(a, a), dot(b, b), dot(a, b)
            determinant = aa*bb-ab*ab
            if determinant < 1e-12:
                continue
            p = add(scale((ah*bb-bh*ab)/determinant, a), scale((bh*aa-ah*ab)/determinant, b))
            if dot(p, p) > 1+1e-12:
                continue
            axis = cross(a, b)
            distance = math.sqrt(max(0., 1-dot(p, p))/dot(axis, axis))
            for sign in (-1, 1):
                q = add(p, scale(sign*distance, axis))
                if inside(q, bounds) and not any(math.dist(q, r) < 1e-10 for r in result):
                    result.append(q)
    assert len(result) >= 3, 'A spherical patch must have a nondegenerate boundary'
    return result


def patch(bounds, radius, inset=0):
    """Sample a star-shaped gnomonic chart, retaining every exact corner."""
    vertices = corners(bounds)
    z = unit(tuple(math.fsum(v[i] for v in vertices) for i in range(3)))
    x = unit(cross((0., 0., 1.) if abs(z[2]) < .9 else (0., 1., 0.), z))
    y = cross(z, x)
    assert inside(z, bounds), 'A spherical patch requires an interior chart center'
    assert all(dot(v, z) > 1e-10 for v in vertices), 'The patch must fit in one hemisphere'

    def direction(u, v):
        return unit(add(z, add(scale(u, x), scale(v, y))))

    angles = [math.tau*i/SEGMENTS for i in range(SEGMENTS)]
    angles += [math.atan2(dot(v, y), dot(v, x)) % math.tau for v in vertices]
    angles.sort()
    unique = []
    for angle in angles:
        if not unique or angle-unique[-1] > 1e-10:
            unique.append(angle)
    if unique[0]+math.tau-unique[-1] < 1e-10:
        unique.pop()
    boundary = []
    for angle in unique:
        u, v = math.cos(angle), math.sin(angle)
        low, high = 0., 4.
        assert not inside(direction(u*high, v*high), bounds), 'Unbounded spherical chart'
        for _ in range(56):
            distance = (low+high)/2
            if inside(direction(u*distance, v*distance), bounds, 0):
                low = distance
            else:
                high = distance
        boundary.append((u*low*(1-inset), v*low*(1-inset)))
    points = [z]
    for ring in range(1, RINGS+1):
        points += [direction(u*ring/RINGS, v*ring/RINGS) for u, v in boundary]
    assert all(inside(p, bounds, 1e-9) for p in points), 'Mesh crosses a spherical cut'
    count = len(boundary)
    triangles = []
    for i in range(count):
        j = (i+1) % count
        triangles.append([0, 1+i, 1+j])
        for ring in range(1, RINGS):
            a, b = 1+(ring-1)*count+i, 1+(ring-1)*count+j
            c, d = 1+ring*count+i, 1+ring*count+j
            triangles.extend([[a, c, d], [a, d, b]])
    # The application's y-up convention exchanges the last two coordinates.
    return dict(vertices=[[radius*p[0], radius*p[2], radius*p[1]] for p in points],
                triangles=[t[::-1] for t in triangles],
                radialNormalTriangles=list(range(len(triangles))))


def piece_bounds(model, piece):
    caps = set(piece['caps'])
    return [(scale(1 if i in caps else -1, n), DEPTH if i in caps else -DEPTH)
            for i, n in enumerate(model.axes)]


def realization(model, euclidean):
    result = deepcopy(euclidean)
    assets = result['assets'] = {}
    for piece, cell in zip(model.pieces, model.cells):
        domain = piece['id']
        if not piece['caps']:
            # The existing fixed internal core has no surface on the sphere.
            core = deepcopy(euclidean['assets'][domain+'/body'])
            core['vertices'] = [scale(RADIUS, v) for v in core['vertices']]
            assets[domain+'/body'] = core
            continue
        bounds = piece_bounds(model, piece)
        assets[domain+'/body'] = patch(bounds, RADIUS-SURFACE_LIFT)
        for port in cell['ports']:
            normal = model.axes[PORT_AXES[port]]
            # Unit rays maximize dot product exactly when angular distance is
            # smallest. Port identities keep each ray's existing face color.
            sector = bounds+[(sub(normal, other), 0.) for i, other in enumerate(model.axes)
                             if i != PORT_AXES[port]]
            assets[domain+'/port-'+str(port)] = patch(sector, RADIUS, PORT_INSET)
    result.update(id=model.id+'-spherical-v1', kind='articulated-spherical', radius=RADIUS,
                  diskAngleDegrees=math.degrees(math.acos(DEPTH)),
                  angularSegments=SEGMENTS, radialSegments=RINGS, portInset=PORT_INSET,
                  surfaceLift=SURFACE_LIFT, fidelity='unit-sphere-sections-of-melty-cuts')
    return result
