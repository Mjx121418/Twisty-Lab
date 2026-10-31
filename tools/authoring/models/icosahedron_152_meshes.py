"""Planar regular-icosahedron cells and matching spherical sticker patches."""
from itertools import combinations
import math

from .icosahedron_geometry import AXES, add, sub, scale, dot, cross, norm, unit
from .icosahedron_meshes import DISPLAY_RADIUS, INSET, mesh, region
from .icosahedron_152_model import H

PHI = (1+math.sqrt(5))/2
INRADIUS = DISPLAY_RADIUS*PHI/math.sqrt(3)
CUT_OFFSET = DISPLAY_RADIUS*H


def planes(mask):
    return [(n, INRADIUS) for n in AXES] + [
        (scale(-1,n), -CUT_OFFSET) if mask & (1 << i) else (n,CUT_OFFSET)
        for i,n in enumerate(AXES)]


def polyhedron(mask):
    """Intersect the twenty exterior halfspaces and twenty literal cut planes."""
    bounds = planes(mask)
    vertices = []
    for (a,ah),(b,bh),(c,ch) in combinations(bounds,3):
        det = dot(a,cross(b,c))
        if abs(det) < 1e-10: continue
        p = scale(1/det, add(add(scale(ah,cross(b,c)),scale(bh,cross(c,a))),scale(ch,cross(a,b))))
        if all(dot(n,p) <= h+1e-8 for n,h in bounds) and not any(norm(sub(p,q)) < 1e-7 for q in vertices):
            vertices.append(p)
    faces = []
    for i,(n,h) in enumerate(bounds):
        points = [p for p in vertices if abs(dot(n,p)-h) < 1e-7]
        if len(points) < 3: continue
        center = tuple(sum(p[j] for p in points)/len(points) for j in range(3))
        x = unit(sub(points[0],center)); y = cross(n,x)
        points.sort(key=lambda p: math.atan2(dot(sub(p,center),y),dot(sub(p,center),x)))
        area = sum(dot(n,cross(a,b)) for a,b in zip(points,points[1:]+points[:1]))/2
        if area > 1e-10: faces.append((i,center,points))
    assert vertices and faces
    return vertices,faces


def polygon(asset, center, points):
    offset = len(asset['vertices'])
    asset['vertices'] += [center,*points]
    asset['triangles'] += [[offset,offset+1+i,offset+1+(i+1)%len(points)] for i in range(len(points))]


def spherical_patch(asset,center,boundary,radius,inset=0.):
    """Triangulate concave curved petals in a gnomonic chart, without a fan."""
    x = unit(cross(center,(1.,0.,0.) if abs(center[0]) < .9 else (0.,1.,0.)))
    y = cross(center,x)
    points = [(dot(p,x)/dot(p,center),dot(p,y)/dot(p,center)) for p in boundary]
    determinant = lambda a,b,c:(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    area = sum(a[0]*b[1]-a[1]*b[0] for a,b in zip(points,points[1:]+points[:1]))/2
    assert area > 0
    if inset:
        edges = [(b[0]-a[0],b[1]-a[1]) for a,b in zip(points,points[1:]+points[:1])]
        lengths = [math.hypot(*e) for e in edges]
        distance = min(2*inset*area/sum(lengths),min(lengths)/4)
        normals = [(-e[1]/length,e[0]/length) for e,length in zip(edges,lengths)]
        shifted = []
        for i,p in enumerate(points):
            a,b = normals[i-1],normals[i]
            denominator = 1+a[0]*b[0]+a[1]*b[1]
            assert denominator > 1e-8
            shifted.append((p[0]+distance*(a[0]+b[0])/denominator,p[1]+distance*(a[1]+b[1])/denominator))
        points = shifted
    remaining = list(range(len(points))); triangles = []
    while len(remaining) > 3:
        for j,b in enumerate(remaining):
            a,c = remaining[j-1],remaining[(j+1)%len(remaining)]
            if determinant(points[a],points[b],points[c]) <= 1e-14: continue
            if any(i not in (a,b,c) and all(determinant(points[u],points[v],points[i]) >= -1e-14
                for u,v in ((a,b),(b,c),(c,a))) for i in remaining): continue
            triangles.append((a,b,c));remaining.pop(j);break
        else: raise AssertionError('Spherical sticker polygon cannot be triangulated')
    triangles.append(tuple(remaining))
    positions = [unit(add(center,add(scale(p[0],x),scale(p[1],y)))) for p in points]
    def append(a,b,c,depth):
        if depth:
            ab,bc,ca = unit(add(a,b)),unit(add(b,c)),unit(add(c,a))
            for vertices in ((a,ab,ca),(ab,b,bc),(ca,bc,c),(ab,bc,ca)): append(*vertices,depth-1)
        else:
            offset = len(asset['vertices'])
            asset['vertices'] += [scale(radius,p) for p in (a,b,c)]
            asset['radialNormalTriangles'].append(len(asset['triangles']))
            asset['triangles'].append([offset,offset+1,offset+2])
    for triangle in triangles: append(*[positions[i] for i in triangle],2)


def assets(piece,spherical):
    body = mesh(); ports = {}
    if spherical:
        bounds = [(n,H) if piece.mask & (1 << i) else (scale(-1,n),-H) for i,n in enumerate(AXES)]
        caps = [i for i in range(20) if piece.mask & (1 << i)]
        for color in caps:
            sector = bounds+[(sub(AXES[color],AXES[other]),0.) for other in caps if other != color]
            for center,boundary in region(sector):
                port = ports.setdefault(str(color),mesh())
                spherical_patch(body,center,boundary,DISPLAY_RADIUS-.018)
                spherical_patch(port,center,boundary,DISPLAY_RADIUS,INSET)
        for center,boundary in region(bounds):
            refined = []
            for a,b in zip(boundary,boundary[1:]+boundary[:1]):
                midpoint = unit(add(a,b))
                refined += [a,unit(add(a,midpoint)),midpoint,unit(add(midpoint,b))]
            outer = [scale(DISPLAY_RADIUS-.018,p) for p in refined]
            polygon(body,(0.,0.,0.),outer[::-1])
    else:
        vertices,faces = polyhedron(piece.mask)
        center = tuple(sum(p[i] for p in vertices)/len(vertices) for i in range(3))
        shrink = lambda p: add(center,scale(.994,sub(p,center)))
        for face,c,points in faces:
            polygon(body,shrink(c),[shrink(p) for p in points])
            if face < 20:
                port = ports.setdefault(str(face),mesh())
                polygon(port,c,[add(c,scale(1-INSET,sub(p,c))) for p in points])
    for asset in [body,*ports.values()]:
        asset['vertices'] = [[p[0],p[2],p[1]] for p in asset['vertices']]
        asset['triangles'] = [t[::-1] for t in asset['triangles']]
        if not asset['radialNormalTriangles']: asset.pop('radialNormalTriangles')
    return body,ports
