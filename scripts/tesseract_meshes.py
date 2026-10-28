"""Symmetric S² cut-face patches embedded in S³, with tangent outward normals."""
import math
from itertools import combinations
from tesseract_model import DEPTH, dot, unit
from melty_model import cross, sub, add, scale

EDGE_STEPS = 6
RINGS = 3
# Halve the former 0.925 tangent extent about each intrinsic piece center.
INSET = .5375


def polygon(bounds, edge_steps=EDGE_STEPS, rings=RINGS):
    vertices = []
    for (a, ah), (b, bh) in combinations(bounds, 2):
        aa, bb, ab = dot(a, a), dot(b, b), dot(a, b)
        det = aa*bb-ab*ab
        if det < 1e-12: continue
        p = add(scale((ah*bb-bh*ab)/det, a), scale((bh*aa-ah*ab)/det, b))
        if dot(p, p) > 1+1e-10: continue
        axis = cross(a, b)
        distance = math.sqrt(max(0., (1-dot(p, p))/dot(axis, axis)))
        for s in (-1, 1):
            q = add(p, scale(s*distance, axis))
            if all(dot(q, n) >= h-1e-9 for n, h in bounds) and not any(math.dist(q, v) < 1e-8 for v in vertices):
                vertices.append(q)
    if len(vertices) < 3: return None
    center = unit(tuple(sum(v[i] for v in vertices) for i in range(3)))
    assert all(dot(center, n) > h-1e-9 for n, h in bounds)
    x = unit(sub(vertices[0], scale(dot(vertices[0], center), center))); y = cross(center, x)
    vertices.sort(key=lambda p: math.atan2(dot(p, y), dot(p, x)))
    boundary = []
    for a, b in zip(vertices, vertices[1:]+vertices[:1]):
        n, h = next((n, h) for n, h in bounds if abs(dot(a, n)-h) < 1e-8 and abs(dot(b, n)-h) < 1e-8)
        o = scale(h/dot(n, n), n); u, v = sub(a, o), sub(b, o)
        angle = math.acos(max(-1., min(1., dot(unit(u), unit(v)))))
        for step in range(edge_steps):
            t = step/edge_steps
            q = add(o, add(scale(math.sin((1-t)*angle)/math.sin(angle), u), scale(math.sin(t*angle)/math.sin(angle), v)))
            boundary.append(q)
    points = [center]
    for ring in range(1, rings+1):
        t = ring/rings
        points += [unit(add(scale(1-t, center), scale(t, p))) for p in boundary]
    assert all(dot(p, n) >= h-1e-8 for p in points for n, h in bounds)
    triangles = []; count = len(boundary)
    for i in range(count):
        j = (i+1) % count
        triangles.append([0, 1+i, 1+j])
        for ring in range(1, rings):
            a, b = 1+(ring-1)*count+i, 1+(ring-1)*count+j
            c, d = 1+ring*count+i, 1+ring*count+j
            middle = unit(tuple(points[a][k]+points[b][k]+points[c][k]+points[d][k] for k in range(3)))
            m = len(points); points.append(middle)
            triangles += [[a,c,m],[c,d,m],[d,b,m],[b,a,m]]
    return points, triangles


def assets(k):
    bounds = []
    for j in range(4):
        for s in ((1,) if j < k else (-1, 1)):
            bounds.append((tuple(float(i == j)*s for i in range(4)), DEPTH if j < k else -DEPTH))
    center = unit(tuple(float(j < k) for j in range(4)))
    result = {}
    for port in range(k):
        mesh = dict(vertices=[], normals=[], triangles=[])
        sectors = bounds+[(tuple(float(j == port)-float(j == other) for j in range(4)), 0.)
                           for other in range(k) if other != port]
        for normal, height in bounds:
            j = next(i for i, x in enumerate(normal) if x)
            fixed = height/normal[j]; radius = math.sqrt(1-fixed*fixed)
            remaining = [i for i in range(4) if i != j]
            local = []; empty = False
            for n, h in sectors:
                a = tuple(radius*n[i] for i in remaining); b = h-n[j]*fixed
                if dot(a,a) < 1e-12:
                    if b > 1e-9: empty = True
                else: local.append((a,b))
            patch = None if empty else polygon(local)
            if patch is None: continue
            points, triangles = patch; start = len(mesh['vertices'])
            for p in points:
                q = [0.]*4; q[j] = fixed
                for i, x in zip(remaining, p): q[i] = radius*x
                outward = tuple(height*q[i]-normal[i] for i in range(4))
                shrunk = unit(tuple((1-INSET)*q[i]+INSET*center[i] for i in range(4)))
                n = unit(tuple(outward[i]-dot(outward,shrunk)/dot(q,shrunk)*q[i] for i in range(4)))
                mesh['vertices'].append(shrunk); mesh['normals'].append(n)
            # The portable projection fixes winding using the projected outward
            # normal. Intrinsic triangle orientation need not depend on charts.
            mesh['triangles'] += [[start+i for i in t] for t in triangles]
        assert mesh['triangles']
        result[str(port)] = mesh
    return result
