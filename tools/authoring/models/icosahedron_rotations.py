"""Finite icosahedral rotations and shared authored turn coordinates.

This module initializes no model-specific arrangement or piece catalog.
"""
import math
from .icosahedron_geometry import AXES, TAU, add, sub, scale, dot, cross, unit


ALPHA = math.acos((3*math.sqrt(5)-1)/8)
IDENTITY = ((1.,0.,0.),(0.,1.,0.),(0.,0.,1.))

def apply(r,p): return tuple(dot(row,p) for row in r)
def transpose(r): return tuple(zip(*r))


def compose(a,b): return tuple(tuple(sum(a[i][k]*b[k][j] for k in range(3)) for j in range(3)) for i in range(3))
def rotation(axis,theta):
    columns = [add(add(scale(math.cos(theta),p),scale(math.sin(theta),cross(axis,p))),scale((1-math.cos(theta))*dot(axis,p),axis)) for p in IDENTITY]
    return tuple(tuple(p[i] for p in columns) for i in range(3))

LABELS = tuple(chr(65+i) for i in range(20))
PHASES = (0., ALPHA, TAU/3-ALPHA)
STOPS = sorted((k*TAU/3+t) % TAU for k in range(3) for t in PHASES)
DELTAS = sorted({round(math.remainder(b-a, TAU), 10) for a in STOPS for b in STOPS if abs(a-b)>1e-8})
DELTAS = [next(math.remainder(b-a, TAU) for a in STOPS for b in STOPS if abs(math.remainder(b-a, TAU)-d)<1e-8) for d in DELTAS]


def key(r): return tuple(round(x, 7) for row in r for x in row)


def symmetries():
    x = AXES[0]
    b = next(v for v in AXES if abs(dot(v, x)-math.sqrt(5)/3)<1e-9)
    y = unit(sub(b, scale(dot(x, b), x)))
    z = cross(x, y)
    result = []
    for u in AXES:
        for v in AXES:
            if abs(dot(u, v)-math.sqrt(5)/3)>1e-9: continue
            Y = unit(sub(v, scale(dot(u,v),u)))
            Z = cross(u, Y)
            result.append(tuple(tuple(u[i]*x[j]+Y[i]*y[j]+Z[i]*z[j] for j in range(3)) for i in range(3)))
    return [IDENTITY]+[g for g in result if key(g)!=key(IDENTITY)]


GROUP = symmetries()


def canonical(r):
    orbit = [compose(g,r) for g in GROUP]
    return min((key(q),q) for q in orbit)
