"""Shared spherical-region tracing and mesh containers for both models."""
import math
from .icosahedron_geometry import add, sub, scale, dot, cross, norm, unit

DISPLAY_RADIUS = 2.05
SEGMENTS = 96
INSET = .035


def inside(p, bounds, tolerance=1e-9):
    return all(dot(n,p) >= h-tolerance for n,h in bounds)


def region(bounds):
    """Trace the actual small/great-circle arcs of a connected section."""
    bounds=[(scale(1/norm(n),n),h/norm(n)) for n,h in bounds]
    vertices=[]
    for i,(a,ah) in enumerate(bounds):
        for b,bh in bounds[i+1:]:
            ab=dot(a,b);det=1-ab*ab
            if det < 1e-12:continue
            p=add(scale((ah-bh*ab)/det,a),scale((bh-ah*ab)/det,b))
            t2=1-dot(p,p)
            if t2 < -1e-10:continue
            if abs(t2) < 1e-12:t2=0.
            t=scale(math.sqrt(max(0.,t2)),unit(cross(a,b)))
            for q in (add(p,t),sub(p,t)):
                if inside(q,bounds) and not any(norm(sub(q,v))<1e-8 for v in vertices):vertices.append(q)
    if len(vertices)<2:return []
    arcs=[]
    for n,h in bounds:
        s=math.sqrt(max(0.,1-h*h))
        if s < 1e-10:continue
        x=unit(cross(n,(1.,0.,0.) if abs(n[0])<.9 else (0.,1.,0.)))
        y=cross(n,x)
        points=sorted((math.atan2(dot(p,y),dot(p,x)) % math.tau,i)
                      for i,p in enumerate(vertices) if abs(dot(n,p)-h)<1e-8)
        if len(points)<2:continue
        for j,(t,start) in enumerate(points):
            end_t,end=points[(j+1)%len(points)];dt=(end_t-t)%math.tau
            if dt<1e-10:continue
            m=t+dt/2
            p=add(scale(h,n),scale(s,add(scale(math.cos(m),x),scale(math.sin(m),y))))
            if inside(p,bounds):arcs.append((start,end,n,h,s,x,y,t,dt))
    if not arcs:return []
    outgoing={}
    for arc in arcs:
        assert arc[0] not in outgoing, 'Non-simple spherical color region'
        outgoing[arc[0]]=arc
    loops=[]
    while outgoing:
        start=min(outgoing);v=start;points=[]
        while v in outgoing:
            a,b,n,h,s,x,y,t,dt=outgoing.pop(v)
            count=max(4,math.ceil(dt*SEGMENTS/math.tau))
            points += [add(scale(h,n),scale(s,add(scale(math.cos(t+dt*j/count),x),scale(math.sin(t+dt*j/count),y))))
                       for j in range(count)]
            v=b
        assert v==start
        center=unit(tuple(sum(p[i] for p in points) for i in range(3)))
        assert inside(center,bounds)
        area=sum(2*math.atan2(dot(center,cross(a,b)),1+dot(center,a)+dot(a,b)+dot(b,center))
                 for a,b in zip(points,points[1:]+points[:1]))
        if area>1e-10:loops.append((center,points))
    return loops


def mesh():return dict(vertices=[],triangles=[],radialNormalTriangles=[])
