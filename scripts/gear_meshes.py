"""Small procedural meshes for browser review of the gear mechanism.

All dimensions and surfaces live here, outside the abstract compiler. The
profiles are an editable construction prototype, not certified gear contacts.
"""
from functools import lru_cache
from math import atan2, cos, pi, sin, sqrt, tan

FACE = 1.48
EDGE_AXIS = (0, 1 / sqrt(2), 1 / sqrt(2))
RIGHT = (1, 0, 0)
TRANSVERSE = (0, 1 / sqrt(2), -1 / sqrt(2))
TEETH = 12
ROOT_ANGLE = 23 * pi / 180
TIP_ANGLE = 27 * pi / 180


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


def tooth_angle(phi, teeth=TEETH):
    # Six-tooth edges retain every other tooth of the original profile. Keeping
    # the tooth width and seats avoids changing the existing corner mesh.
    u = (phi * teeth / (2 * pi)) % 1 * (TEETH / teeth)
    if u < .12 or u > .88:
        height = 0
    elif u < .30:
        height = (u - .12) / .18
    elif u > .70:
        height = (.88 - u) / .18
    else:
        height = 1
    return ROOT_ANGLE + (TIP_ANGLE - ROOT_ANGLE) * height


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


def corner_distance(p):
    distance = max(max(p)-FACE, .52-min(p))
    # Cut complementary seats around each of this corner's three edge axes.
    for missing in range(3):
        axis = tuple(0 if i == missing else 1/sqrt(2) for i in range(3))
        right = tuple(int(i == missing) for i in range(3))
        transverse = cross(axis, right)
        axial = dot(p, axis)
        radial = add(p, scale(axis, -axial))
        phi = atan2(dot(radial, transverse), dot(radial, right))
        clearance = (axial + .20) * tan(tooth_angle(phi)) + .018
        distance = max(distance, clearance - sqrt(dot(radial, radial)))
    return distance


@lru_cache(maxsize=1)
def corner_body():
    """Marching tetrahedra on one small prototype, with shared edge vertices."""
    cells, lo, hi = 26, .47, FACE+.05
    width = cells + 1
    points = [(lo+(hi-lo)*x/cells, lo+(hi-lo)*y/cells, lo+(hi-lo)*z/cells)
              for x in range(width) for y in range(width) for z in range(width)]
    values = [corner_distance(p) for p in points]
    vertices, triangles, crossings = [], [], {}

    def intersection(a, b):
        key = (min(a,b), max(a,b))
        if key not in crossings:
            t = values[a] / (values[a] - values[b])
            crossings[key] = len(vertices)
            vertices.append(add(scale(points[a], 1-t), scale(points[b], t)))
        return crossings[key]

    def triangle(indices, outward):
        a,b,c = (vertices[i] for i in indices)
        n = cross(add(b, scale(a,-1)), add(c, scale(a,-1)))
        if dot(n,n) < 1e-18:
            return
        if dot(n,outward) < 0:
            indices[1],indices[2] = indices[2],indices[1]
        triangles.append(indices)

    tetrahedra = [(0,1,3,7),(0,3,2,7),(0,2,6,7),(0,6,4,7),(0,4,5,7),(0,5,1,7)]
    for x in range(cells):
        for y in range(cells):
            for z in range(cells):
                ids = [((x+dx)*width+y+dy)*width+z+dz
                       for dx,dy,dz in [(0,0,0),(0,0,1),(0,1,0),(0,1,1),
                                        (1,0,0),(1,0,1),(1,1,0),(1,1,1)]]
                for tetra in tetrahedra:
                    inside = [ids[i] for i in tetra if values[ids[i]] < 0]
                    outside = [ids[i] for i in tetra if values[ids[i]] >= 0]
                    if not inside or not outside:
                        continue
                    outward = add(scale(tuple(sum(points[i][k] for i in outside) for k in range(3)),1/len(outside)),
                                  scale(tuple(sum(points[i][k] for i in inside) for k in range(3)),-1/len(inside)))
                    if len(inside) == 2:
                        a,b = inside
                        c,d = outside
                        ac,ad,bc,bd = [intersection(i,j) for i,j in [(a,c),(a,d),(b,c),(b,d)]]
                        triangle([ac,ad,bd],outward)
                        triangle([ac,bd,bc],outward)
                    else:
                        single,others = (inside[0],outside) if len(inside) == 1 else (outside[0],inside)
                        triangle([intersection(single,i) for i in others],outward)
    return mesh(vertices,triangles)


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
