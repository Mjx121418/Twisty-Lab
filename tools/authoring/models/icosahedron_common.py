"""Shared spherical-patch geometry for both authored icosahedron models.

Each model supplies its own arrangement and cut threshold. Neither this helper
nor its catalog helpers participate in the abstract executor.
"""
import math
from .icosahedron_geometry import AXES, TAU, add, sub, scale, dot, cross, unit


def apply(r,p): return tuple(dot(row,p) for row in r)
def transpose(r): return tuple(zip(*r))


class SphericalPiece:
    def __init__(self, face, arrangement, threshold):
        self.arrangement, self.threshold = A, H = arrangement, threshold
        self.face = face
        self.mask = face['mask']
        self.arcs = []
        self.vertices = []
        samples = []
        for half in face['boundary']:
            edge = A.edges[half//2]
            n = A.axes[edge['circle']]
            x,y = A.frames[edge['circle']]
            start, end = A.vertices[edge['start']], A.vertices[edge['end']]
            self.arcs.append((n, start, end, edge['dt'], edge['t'], x, y))
            self.vertices.extend([start, end])
            for i in range(8):
                t = edge['t']+edge['dt']*i/8
                samples.append(add(scale(H,n),scale(A.s,add(scale(math.cos(t),x),scale(math.sin(t),y)))))
        self.center = unit(tuple(sum(v[i] for v in samples) for i in range(3)))
        assert self.contains_local(self.center)
        self.cap_cos = self.extrema_local(self.center)[0]

    def contains_local(self, p, strict=False):
        H = self.threshold
        return all((dot(n,p)-H)*(1 if self.mask & (1<<i) else -1) > (1e-8 if strict else -1e-9)
                   for i,n in enumerate(AXES))

    def extrema_local(self, d):
        A, H = self.arrangement, self.threshold
        values = [dot(d,p) for p in self.vertices]
        for n,start,end,dt,t,x,y in self.arcs:
            phi = math.atan2(dot(d,y),dot(d,x))
            for q in (phi,phi+math.pi):
                if (q-t) % TAU <= dt+1e-9:
                    values.append(H*dot(n,d)+A.s*(math.cos(q)*dot(d,x)+math.sin(q)*dot(d,y)))
        if self.contains_local(d):values.append(1.)
        if self.contains_local(scale(-1,d)):values.append(-1.)
        return min(values),max(values)

    def role(self, r, axis):
        H = self.threshold
        low,high = self.extrema_local(apply(transpose(r),axis))
        return 1 if low >= H-1e-8 else 0 if high <= H+1e-8 else 2

    def placed(self, r):
        return Placed(self,r)


class Placed:
    def __init__(self,piece,r):
        self.piece,self.r,self.inv = piece,r,transpose(r)
        self.center = apply(r,piece.center)
        self.vertices = [apply(r,p) for p in piece.vertices]
        self.arcs = [(apply(r,n),apply(r,a),apply(r,b),dt) for n,a,b,dt,*_ in piece.arcs]

    def contains(self,p):return self.piece.contains_local(apply(self.inv,p),True)

    def overlaps(self,other):
        if self.piece.threshold != other.piece.threshold:
            raise ValueError("Overlap requires a shared spherical cut threshold.")
        H = self.piece.threshold
        radius = math.acos(self.piece.cap_cos)+math.acos(other.piece.cap_cos)
        if dot(self.center,other.center) < math.cos(radius)-1e-9:return False
        if self.contains(other.center) or other.contains(self.center):return True
        if any(other.contains(v) for v in self.vertices) or any(self.contains(v) for v in other.vertices):return True
        for a,astart,aend,adt in self.arcs:
            for b,bstart,bend,bdt in other.arcs:
                cosine = dot(a,b)
                if abs(cosine) > 1-1e-9:continue
                p = scale(H/(1+cosine),add(a,b))
                distance = 1-dot(p,p)
                if distance <= 1e-10:continue
                offset = scale(math.sqrt(distance),unit(cross(a,b)))
                for q in (add(p,offset),sub(p,offset)):
                    aa = math.atan2(dot(a,cross(astart,q)),dot(astart,q)-H*H) % TAU
                    bb = math.atan2(dot(b,cross(bstart,q)),dot(bstart,q)-H*H) % TAU
                    if 1e-7<aa<adt-1e-7 and 1e-7<bb<bdt-1e-7:return True
        return False


def add_domain(source, catalog, model, kind, pieces, ports, color):
    source['domains'].append(dict(id=kind,placements=['p'+str(q) for q in range(len(model.poses[kind]))],
        pieces=[dict(id=p['id'],initial='p'+str(p['home'])) for p in pieces],moves=[]))
    catalog['domains'].append(dict(id=kind,pieces=[p['id'] for p in pieces],localPorts=ports,
        placements=[kind+'/p'+str(q) for q in range(len(model.poses[kind]))]))
    for p in pieces:
        labels={}
        for port in ports:
            n=AXES[int(port)]
            transformed=tuple(sum(p['frame'][i][j]*n[j] for j in range(3)) for i in range(3))
            labels[port]=color(max(range(20),key=lambda i:dot(AXES[i],transformed)))
        catalog['pieces'][p['id']]=dict(domain=kind,home=kind+'/p'+str(p['home']),portLabels=labels)
    for q in range(len(model.poses[kind])):
        path=kind+'/p'+str(q);resource='location/'+kind+'/'+str(model.location_of[kind][q])
        catalog['placements'][path]=dict(resources=[resource],ports={port:dict(cell=resource,attachment=path+'/'+port) for port in ports})


def components(model, meshes, frame):
    assets = {}; domains = {}
    for kind in model.kinds:
        body,ports=meshes[kind]
        frames={'p'+str(q):frame(r) for q,r in enumerate(model.poses[kind])}
        entries=[]
        for port,asset in [('body',body),*ports.items()]:
            name=kind+'/'+port;assets[name]=asset
            component=dict(id=port,role='body' if port=='body' else 'port',meshAssetId=name,
                placementFrames=frames,operationTracks='turns')
            if port!='body':component['portId']=port
            entries.append(component)
        domains[kind]=entries
    return assets, domains
