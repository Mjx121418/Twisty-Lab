"""Small procedural meshes for browser review of the gear mechanism.

All dimensions and surfaces live here, outside the abstract compiler. The
profiles are an editable construction prototype, not certified gear contacts.
"""
from functools import lru_cache
from math import cos, pi, sin, sqrt, tan

FACE = 1.48
EDGE_AXIS = (0, 1 / sqrt(2), 1 / sqrt(2))
RIGHT = (1, 0, 0)
TRANSVERSE = (0, 1 / sqrt(2), -1 / sqrt(2))
TEETH = 12
ROOT_ANGLE = 15 * pi / 180
TIP_ANGLE = 18 * pi / 180
CORNER_AXIS = (1 / sqrt(3),) * 3
# The zero-angle tooth points down the UF cube edge from the UFR vertex.
CORNER_RIGHT = (-2 / sqrt(6), 1 / sqrt(6), 1 / sqrt(6))
CORNER_UP = (0, -1 / sqrt(2), 1 / sqrt(2))
CORNER_ROOT_ANGLE = 14 * pi / 180
CORNER_TIP_ANGLE = 18 * pi / 180
CORNER_THICKNESS = .14


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(a, s):
    return tuple(x * s for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def mesh(vertices, triangles):
    return {'vertices': vertices, 'triangles': triangles}


def join(*meshes):
    vertices, triangles = [], []
    for m in meshes:
        offset = len(vertices)
        vertices.extend(m['vertices'])
        triangles.extend([[offset + v for v in t] for t in m['triangles']])
    return mesh(vertices, triangles)


def tooth_height(u):
    if u < .12 or u > .88:
        return 0
    elif u < .30:
        return (u - .12) / .18
    elif u > .70:
        return (.88 - u) / .18
    return 1


def tooth_angle(phi, teeth=TEETH):
    # Six-tooth edges retain every other tooth of the twelve-tooth profile.
    u = (phi * teeth / (2 * pi)) % 1 * (TEETH / teeth)
    return ROOT_ANGLE + (TIP_ANGLE - ROOT_ANGLE) * tooth_height(u)


def roof_outline(geared=True, count=96, teeth=TEETH):
    out = []
    for i in range(count):
        phi = 2 * pi * i / count
        angle = tooth_angle(phi, teeth) if geared else ROOT_ANGLE - .01
        tangent = add(scale(RIGHT, cos(phi)), scale(TRANSVERSE, sin(phi)))
        direction = add(EDGE_AXIS, scale(tangent, tan(angle)))
        out.append(scale(direction, FACE / max(direction)))
    return out


def loft(rings, bottom_center, top_center):
    """Closed solid with counterclockwise rings viewed from the top."""
    count = len(rings[0])
    vertices = [v for ring in rings for v in ring] + [bottom_center, top_center]
    bottom, top = len(vertices) - 2, len(vertices) - 1
    triangles = []
    for i in range(count):
        j = (i + 1) % count
        triangles.append([bottom, j, i])
        triangles.append([top, (len(rings)-1)*count+i, (len(rings)-1)*count+j])
        for level in range(len(rings) - 1):
            a, b = level*count+i, level*count+j
            c, d = (level+1)*count+i, (level+1)*count+j
            triangles.extend([[a, b, d], [a, d, c]])
    return mesh(vertices, triangles)


def gear_shell(geared=True, teeth=TEETH):
    front = roof_outline(geared, teeth=teeth)
    back = [add(p, scale(EDGE_AXIS, -.20)) for p in front]
    center = (0, FACE, FACE)
    return loft([back, front], add(center, scale(EDGE_AXIS, -.20)), center)


def gear_port(port, geared=True, teeth=TEETH):
    outline = roof_outline(geared, teeth=teeth)
    half = len(outline) // 2
    normal = (0, 1, 0) if port == 0 else (0, 0, 1)
    centroid = (0, FACE, FACE-.42) if port == 0 else (0, FACE-.42, FACE)
    path = [(0, FACE, FACE)] + [outline[i % len(outline)] for i in range(port*half, (port+1)*half+1)]
    vertices = [add(add(scale(p, .90), scale(centroid, .10)), scale(normal, .009)) for p in path]
    return mesh(vertices, [[0, i, i+1] for i in range(1, len(vertices)-1)])


def cylinder(axis, center, radius, half_length, count=32):
    right = RIGHT if abs(axis[0]) < .9 else (0, 1, 0)
    up = cross(axis, right)
    rings = [[add(add(center, scale(axis, level)),
                  add(scale(right, radius*cos(2*pi*i/count)), scale(up, radius*sin(2*pi*i/count))))
              for i in range(count)] for level in [-half_length, half_length]]
    return loft(rings, add(center, scale(axis, -half_length)), add(center, scale(axis, half_length)))


def bearing():
    count = 32
    center = (0, 1.30, 1.30)
    vertices = []
    for level, radius in [(-.07, .19), (.07, .19), (-.07, .10), (.07, .10)]:
        vertices.extend(add(add(center, scale(EDGE_AXIS, level)),
                            add(scale(RIGHT, radius*cos(2*pi*i/count)),
                                scale(TRANSVERSE, radius*sin(2*pi*i/count)))) for i in range(count))
    triangles = []
    for i in range(count):
        j = (i + 1) % count
        a, b, c, d = i, j, count+i, count+j
        e, f, g, h = 2*count+i, 2*count+j, 3*count+i, 3*count+j
        triangles.extend([[a,b,d],[a,d,c], [e,g,h],[e,h,f],
                          [c,d,h],[c,h,g], [a,f,b],[a,e,f]])
    return mesh(vertices, triangles)


def chamfered_square(half, cut, y):
    uv = [(-half+cut,-half),(half-cut,-half),(half,-half+cut),(half,half-cut),
          (half-cut,half),(-half+cut,half),(-half,half-cut),(-half,-half+cut)]
    return [(u, y, -v) for u, v in uv]


def center_body():
    return loft([chamfered_square(.36,.09,.65), chamfered_square(.49,.10,1.40),
                 chamfered_square(.43,.09,FACE)], (0,.65,0), (0,FACE,0))


def center_port():
    vertices = [(0,FACE+.009,0)] + chamfered_square(.37,.075,FACE+.009)
    return mesh(vertices, [[0,i+1,(i+1)%8+1] for i in range(8)])


def corner_outline(count=144):
    """Six tooth centers alternate between cube edges and face interiors.

    Angles 0, 120, and 240 degrees lie on UF, FR, and UR respectively;
    60, 180, and 300 degrees point into F, R, and U. Projection onto the
    cube surface preserves the three edge-aligned tooth centers exactly.
    """
    out = []
    for i in range(count):
        phi = 2 * pi * i / count
        height = tooth_height((phi * 6 / (2 * pi) + .5) % 1)
        angle = CORNER_ROOT_ANGLE + (CORNER_TIP_ANGLE - CORNER_ROOT_ANGLE) * height
        tangent = add(scale(CORNER_RIGHT, cos(phi)), scale(CORNER_UP, sin(phi)))
        direction = add(CORNER_AXIS, scale(tangent, tan(angle)))
        out.append(scale(direction, FACE / max(direction)))
    return out


@lru_cache(maxsize=1)
def corner_body():
    """A thick six-lobed roof folded across the corner's three cube faces."""
    front = corner_outline()
    back = [add(p, scale(CORNER_AXIS, -CORNER_THICKNESS)) for p in front]
    center = (FACE, FACE, FACE)
    return loft([back, front], add(center, scale(CORNER_AXIS, -CORNER_THICKNESS)), center)


def corner_port(port):
    body = corner_body()
    axis = [1,0,2][port]  # U, R, F
    vertices, triangles, indices = [], [], {}
    for tri in body['triangles']:
        if any(abs(body['vertices'][i][axis]-FACE) > 1e-9 for i in tri):
            continue
        converted = []
        for i in tri:
            if i not in indices:
                p = list(body['vertices'][i])
                for k in range(3):
                    p[k] = FACE+.009 if k == axis else 1.16+(p[k]-1.16)*.93
                indices[i] = len(vertices)
                vertices.append(p)
            converted.append(indices[i])
        triangles.append(converted)
    return mesh(vertices,triangles)
