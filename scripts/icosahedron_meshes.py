"""Matched spherical and conically cut regular-icosahedron meshes."""
import math

from icosahedron_geometry import AXES, add, sub, scale, dot, cross, norm, unit
from icosahedron_model import H, RADIUS

DISPLAY_RADIUS = 2.05
INRADIUS = DISPLAY_RADIUS*H
SEGMENTS = 96
RINGS = 5
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


def bounds(piece):
    return [(n,H) if piece.mask & (1<<i) else (scale(-1,n),-H) for i,n in enumerate(AXES)]


def mesh():return dict(vertices=[],triangles=[],radialNormalTriangles=[])


def append_patch(asset, center, boundary, point, inset=0., smooth=False):
    points=[center]
    boundary=[unit(add(scale(inset,center),scale(1-inset,p))) for p in boundary]
    for ring in range(1,RINGS+1):
        points += [unit(add(scale(1-ring/RINGS,center),scale(ring/RINGS,p))) for p in boundary]
    offset=len(asset['vertices']);asset['vertices'] += [point(p) for p in points]
    count=len(boundary)
    triangles=[]
    for i in range(count):
        j=(i+1)%count
        triangles.append([0,1+i,1+j])
        for ring in range(1,RINGS):
            a,b=1+(ring-1)*count+i,1+(ring-1)*count+j
            c,d=1+ring*count+i,1+ring*count+j
            triangles.extend([[a,c,d],[a,d,b]])
    if smooth:asset['radialNormalTriangles'] += list(range(len(asset['triangles']),len(asset['triangles'])+len(triangles)))
    asset['triangles'] += [[offset+i for i in t] for t in triangles]


def assets(piece, spherical):
    body=mesh();ports={}
    caps=[i for i in range(20) if piece.mask & (1<<i)]
    for color in caps:
        sector=bounds(piece)+[(sub(AXES[color],AXES[other]),0.) for other in caps if other!=color]
        loops=region(sector)
        if not loops:continue
        port=ports[str(color)]=mesh()
        for center,boundary in loops:
            if spherical:
                backing=lambda p:scale(DISPLAY_RADIUS-.018,p)
                exterior=lambda p:scale(DISPLAY_RADIUS,p)
            else:
                backing=lambda p:scale(INRADIUS*.994/dot(AXES[color],p),p)
                exterior=lambda p:scale(INRADIUS/dot(AXES[color],p),p)
            append_patch(body,center,boundary,backing,smooth=spherical)
            append_patch(port,center,boundary,exterior,INSET,spherical)
    for center,boundary in region(bounds(piece)):
        outer=[scale(DISPLAY_RADIUS-.018,p) if spherical else scale(INRADIUS*.994/max(dot(n,p) for n in AXES),p) for p in boundary]
        origin=len(body['vertices']);body['vertices'].append((0.,0.,0.))
        start=len(body['vertices']);body['vertices']+=outer
        body['triangles'] += [[origin,start+(i+1)%len(outer),start+i] for i in range(len(outer))]
    # Authoring is z-up. The application's reflection to y-up reverses winding.
    for asset in [body,*ports.values()]:
        asset['vertices']=[[v[0],v[2],v[1]] for v in asset['vertices']]
        asset['triangles']=[t[::-1] for t in asset['triangles']]
        if not asset['radialNormalTriangles']:asset.pop('radialNormalTriangles')
    return body,ports


if __name__=='__main__':
    from icosahedron_model import PIECES
    for kind in (1,2,3):
        p=next(p for p in PIECES if p.mask.bit_count()==kind)
        for spherical in (False,True):
            body,ports=assets(p,spherical)
            print(kind,spherical,len(body['vertices']),{k:len(v['vertices']) for k,v in ports.items()},flush=True)
