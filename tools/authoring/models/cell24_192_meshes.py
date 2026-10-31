"""Closed cut meshes at the 24-cell's exact 45-degree contact radius."""
import math
from itertools import combinations
from .cell24_model import CENTERS, PROTOTYPES, dot, unit
from .cell24_meshes import basis, INSET
from .melty_model import cross

DEPTH = math.sqrt(.5)
SMALL_PROTOTYPES = PROTOTYPES[:3]
TOL = 1e-9


def patches(bounds, steps=6, rings=4):
    """Trace feasible small-circle arcs, including two-vertex curved faces.

    Contact-only points/arcs have no strict interior and emit no triangles.
    All intersection angles partition each circle before feasibility sampling.
    At 45 degrees the surviving patches are about twice as wide as those of
    the 672-piece model. Double both sampling counts to retain its spacing.
    """
    constraints=[]
    for n,h in bounds:
        length=math.sqrt(dot(n,n));n=tuple(x/length for x in n);h/=length
        if h>1-TOL:return []
        if h<-1+TOL:continue
        if any(math.dist(n,m)<TOL and abs(h-k)<TOL for m,k in constraints):continue
        if any(math.dist(n,tuple(-x for x in m))<TOL and abs(h+k)<TOL for m,k in constraints):return []
        constraints.append((n,h))
    intersections=[[] for _ in constraints]
    for i,j in combinations(range(len(constraints)),2):
        a,ah=constraints[i];b,bh=constraints[j];ab=dot(a,b);det=1-ab*ab
        if det<1e-12:continue
        p=tuple(((ah-bh*ab)*a[k]+(bh-ah*ab)*b[k])/det for k in range(3))
        squared=dot(p,p)
        if squared>1+1e-10:continue
        # Exact contacts otherwise acquire spurious sqrt(machine-epsilon)
        # separations, breaking mesh symmetries and shared boundary vertices.
        axis=cross(a,b);distance=0. if abs(1-squared)<1e-12 else math.sqrt(max(0.,(1-squared)/dot(axis,axis)))
        for sign in (-1,1):
            q=tuple(p[k]+sign*distance*axis[k] for k in range(3))
            intersections[i].append(q);intersections[j].append(q)
    feasible=lambda p:all(dot(n,p)>=h-1e-8 for n,h in constraints)
    key=lambda p:tuple(round(x,7) for x in p)
    arcs=[]
    for (n,h),vertices in zip(constraints,intersections):
        o=tuple(h*x for x in n);r=math.sqrt(1-h*h)
        e=min(((1.,0.,0.),(0.,1.,0.),(0.,0.,1.)),key=lambda v:abs(dot(n,v)))
        u=unit(tuple(e[k]-dot(e,n)*n[k] for k in range(3)));v=cross(n,u)
        angles=sorted(math.atan2(dot(p,v),dot(p,u))%(2*math.pi) for p in vertices)
        unique=[]
        for angle in angles:
            if not unique or angle-unique[-1]>1e-7:unique.append(angle)
        if len(unique)>1 and unique[0]+2*math.pi-unique[-1]<1e-7:unique.pop()
        if not unique:continue
        point=lambda t:tuple(o[k]+r*(math.cos(t)*u[k]+math.sin(t)*v[k]) for k in range(3))
        for a,b in zip(unique,unique[1:]+[unique[0]+2*math.pi]):
            if b-a<1e-7 or not feasible(point((a+b)/2)):continue
            points=[point(a+(b-a)*j/steps) for j in range(steps)]
            arcs.append((key(point(a)),key(point(b)),points))
    remaining=set(range(len(arcs)));result=[]
    while remaining:
        first=min(remaining);start=arcs[first][0];current=first;boundary=[]
        while True:
            remaining.remove(current);a,b,points=arcs[current];boundary.extend(points)
            if b==start:break
            following=[i for i in remaining if arcs[i][0]==b]
            if len(following)!=1:raise ValueError(('Ambiguous cut boundary',b,len(following)))
            current=following[0]
        center=tuple(sum(p[j] for p in boundary) for j in range(3))
        if dot(center,center)<1e-16:continue
        center=unit(center)
        if any(dot(n,center)<=h+1e-10 for n,h in constraints):continue
        points=[center]
        for ring in range(1,rings+1):
            t=ring/rings
            points += [unit(tuple((1-t)*center[k]+t*p[k] for k in range(3))) for p in boundary]
        triangles=[];count=len(boundary)
        for i in range(count):
            j=(i+1)%count;triangles.append([0,1+i,1+j])
            for ring in range(1,rings):
                a,b=1+(ring-1)*count+i,1+(ring-1)*count+j
                c,d=1+ring*count+i,1+ring*count+j
                middle=unit(tuple(points[a][k]+points[b][k]+points[c][k]+points[d][k] for k in range(3)))
                m=len(points);points.append(middle)
                triangles += [[a,c,m],[c,d,m],[d,b,m],[b,a,m]]
        assert all(feasible(p) for p in points),'Cut-face triangulation crosses a boundary'
        result.append((points,triangles))
    return result


def assets():
    result={}
    for name,active,colors,center in SMALL_PROTOTYPES:
        bounds=[(tuple((1 if i in active else -1)*x for x in c),DEPTH if i in active else -DEPTH) for i,c in enumerate(CENTERS)]
        for port,color in enumerate(colors):
            mesh=dict(vertices=[],normals=[],triangles=[])
            sectors=bounds+[(tuple(CENTERS[color][j]-CENTERS[other][j] for j in range(4)),0.) for other in colors if other!=color]
            for normal,height in bounds:
                chart=basis(normal);radius=math.sqrt(1-height*height);local=[];empty=False
                for n,h in sectors:
                    a=tuple(radius*dot(n,b) for b in chart);b=h-height*dot(n,normal)
                    if dot(a,a)<1e-12:
                        if b>TOL:empty=True
                    else:local.append((a,b))
                for points,triangles in ([] if empty else patches(local)):
                    start=len(mesh['vertices'])
                    for p in points:
                        q=tuple(height*normal[i]+radius*sum(p[j]*chart[j][i] for j in range(3)) for i in range(4))
                        outward=tuple(height*q[i]-normal[i] for i in range(4))
                        shrunk=unit(tuple((1-INSET)*q[i]+INSET*center[i] for i in range(4)))
                        n=unit(tuple(outward[i]-dot(outward,shrunk)/dot(q,shrunk)*q[i] for i in range(4)))
                        assert all(dot(shrunk,n)>=h-1e-8 for n,h in sectors)
                        mesh['vertices'].append(shrunk);mesh['normals'].append(n)
                    mesh['triangles'] += [[start+i for i in triangle] for triangle in triangles]
            assert mesh['triangles'],(name,port)
            result[name+'/'+str(port)]=mesh
    return result
