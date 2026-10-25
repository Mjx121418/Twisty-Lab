#!/usr/bin/env python3
"""Connected spherical regions for offline icosahedron authoring.

Standard library only. Point contacts do not glue positive-area pieces.
"""
from collections import Counter, defaultdict
from itertools import combinations, product
import math

TAU = 2*math.pi
PHI = (1+math.sqrt(5))/2


def dot(a, b): return sum(x*y for x, y in zip(a, b))
def add(a, b): return tuple(x+y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))
def scale(s, a): return tuple(s*x for x in a)
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(a): return math.sqrt(dot(a, a))
def unit(a): return scale(1/norm(a), a)
def angle(a, b): return math.acos(max(-1., min(1., dot(a, b))))
def rotate(a, t, p):
    return add(add(scale(math.cos(t), p), scale(math.sin(t), cross(a, p))), scale((1-math.cos(t))*dot(a, p), a))


def axes():
    result = list(product((-1., 1.), repeat=3))
    for a, b in product((-1., 1.), repeat=2):
        result.extend([(0., a/PHI, b*PHI), (a/PHI, b*PHI, 0.), (b*PHI, 0., a/PHI)])
    return [unit(v) for v in result]


AXES = axes()


class Arrangement:
    def __init__(self, radius):
        assert 0 < radius <= math.pi/2+1e-12
        self.radius = radius
        h, s = math.cos(radius), math.sin(radius)
        self.h, self.s = h, s
        self.axes = AXES if abs(h) > 1e-10 else [v for i, v in enumerate(AXES) if not any(dot(v, w) < -1+1e-10 for w in AXES[:i])]
        normals = self.axes
        frames = []
        for a in normals:
            x = unit(cross(a, (1., 0., 0.) if abs(a[0]) < .9 else (0., 1., 0.)))
            frames.append((x, cross(a, x)))
        self.frames = frames
        vertices = []
        membership = []

        def vertex(p, circles):
            for i, q in enumerate(vertices):
                if norm(sub(p, q)) < 2e-8:
                    membership[i].update(circles)
                    return i
            vertices.append(p)
            membership.append(set(circles))
            return len(vertices)-1

        for i, j in combinations(range(len(normals)), 2):
            a, b = normals[i], normals[j]
            d = dot(a, b)
            if 1+d < 1e-12:
                continue
            p = scale(h/(1+d), add(a, b))
            t2 = 1-dot(p, p)
            if t2 < -1e-10:
                continue
            if t2 < 1e-10:
                vertex(unit(p), [i, j])
            else:
                q = scale(math.sqrt(t2), unit(cross(a, b)))
                vertex(add(p, q), [i, j])
                vertex(sub(p, q), [i, j])
        circles = [[] for _ in normals]
        for v, incident in enumerate(membership):
            for i in incident:
                x, y = frames[i]
                t = math.atan2(dot(vertices[v], y), dot(vertices[v], x)) % TAU
                circles[i].append((t, v))
        isolated = []
        for i, c in enumerate(circles):
            if not c:
                v = vertex(add(scale(h, normals[i]), scale(s, frames[i][0])), [i])
                c.append((0., v))
                isolated.append(i)
            c.sort()
        self.vertices, self.membership = vertices, membership
        self.circles = circles
        edges = []
        outgoing = defaultdict(list)
        for i, c in enumerate(circles):
            for k, (t, start) in enumerate(c):
                t1, end = c[(k+1) % len(c)]
                dt = (t1-t) % TAU if len(c) > 1 else TAU
                assert dt > 1e-10
                e = len(edges)
                edges.append(dict(circle=i, start=start, end=end, t=t, dt=dt))
                outgoing[start].append(2*e)
                outgoing[end].append(2*e+1)
        self.edges = edges

        def tangent(half, at_end=False):
            edge = edges[half//2]
            forward = half % 2 == 0
            v = edge['end' if forward == at_end else 'start']
            return scale(1 if forward else -1, unit(cross(normals[edge['circle']], vertices[v])))

        self.tangent = tangent
        for v, halves in outgoing.items():
            p = vertices[v]
            x = unit(cross(p, (1., 0., 0.) if abs(p[0]) < .9 else (0., 1., 0.)))
            y = cross(p, x)

            def order(half):
                t = tangent(half)
                az = (math.atan2(dot(t, y), dot(t, x))+.123456789) % TAU
                # At tangencies equal tangent directions are ordered by their
                # opposite signed geodesic curvatures, i.e. the local arcs.
                return round(az, 8), 1 if half % 2 == 0 else -1
            halves.sort(key=order)
        next_half = {}
        for halves in outgoing.values():
            for k, half in enumerate(halves):
                next_half[half ^ 1] = halves[(k-1) % len(halves)]

        def middle(half):
            e = edges[half//2]
            i, t = e['circle'], e['t']+e['dt']/2
            x, y = frames[i]
            q = add(scale(h, normals[i]), scale(s, add(scale(math.cos(t), x), scale(math.sin(t), y))))
            margin = min([abs(dot(q, a)-h) for j, a in enumerate(normals) if j != i] or [1.])
            epsilon = min(1e-5, margin*.1)
            assert epsilon > 1e-11, (radius, half, margin)
            left = scale((1 if half % 2 == 0 else -1)/s, sub(normals[i], scale(h, q)))
            return unit(add(q, scale(epsilon, left)))

        self.middle = middle
        faces = []
        used = set()
        half_face = {}
        for first in range(2*len(edges)):
            if first in used:
                continue
            cycle, half = [], first
            while half not in used:
                used.add(half)
                cycle.append(half)
                half_face[half] = len(faces)
                half = next_half[half]
            assert half == first
            masks = {self.mask(middle(q)) for q in cycle}
            assert len(masks) == 1, (radius, len(cycle), masks)
            turns, curvature = 0., 0.
            for half in cycle:
                e = edges[half//2]
                p = vertices[e['end' if half % 2 == 0 else 'start']]
                a, b = tangent(half, True), tangent(next_half[half])
                turn = math.atan2(dot(p, cross(a, b)), dot(a, b))
                # Equal-radius external tangencies make a zero-angle cusp;
                # roundoff must not change its +pi exterior turn to -pi.
                if abs(abs(turn)-math.pi) < 1e-8:
                    turn = math.pi
                turns += turn
                curvature += h*e['dt']*(1 if half % 2 == 0 else -1)
            area = TAU-turns-curvature
            assert area > 1e-10, (radius, area, len(cycle))
            faces.append(dict(boundary=cycle, mask=masks.pop(), area=area, sample=middle(first)))
        if isolated:
            assert len(isolated) == len(normals)
            # Disconnected circles have one common exterior, with many holes.
            faces = [f for f in faces if f['mask']] + [dict(boundary=[], mask=0, area=4*math.pi-len(normals)*TAU*(1-h), sample=None)]
        self.faces, self.half_face = faces, half_face
        self.components = len(normals) if isolated else 1
        expected = len(edges)-len(vertices)+1+self.components
        assert len(faces) == expected, (radius, len(faces), expected)
        assert abs(sum(f['area'] for f in faces)-4*math.pi) < 1e-7

    def mask(self, p):
        return sum(1 << i for i, n in enumerate(AXES) if dot(p, n) > self.h)

    def summary(self):
        depths = Counter(f['mask'].bit_count() for f in self.faces)
        degrees = Counter(len(m) for m in self.membership)
        return dict(radiusDegrees=math.degrees(self.radius), pieces=len(self.faces),
                    vertices=len(self.vertices), edges=len(self.edges), circleComponents=self.components,
                    piecesByDiskMultiplicity=dict(sorted(depths.items())),
                    verticesByCircleMultiplicity=dict(sorted(degrees.items())),
                    distinctSignatures=len({f['mask'] for f in self.faces}),
                    minimumArea=min(f['area'] for f in self.faces),
                    maximumArea=max(f['area'] for f in self.faces),
                    areaSum=sum(f['area'] for f in self.faces))
