"""Finite one-piece relaxation with the twenty persistent center frames."""
from collections import deque
import math

from .icosahedron_geometry import AXES, TAU, Arrangement, add, sub, scale, dot, cross, norm, unit
ALPHA = math.acos((3*math.sqrt(5)-1)/8)
IDENTITY = ((1.,0.,0.),(0.,1.,0.),(0.,0.,1.))

def apply(r,p): return tuple(dot(row,p) for row in r)
def compose(a,b): return tuple(tuple(sum(a[i][k]*b[k][j] for k in range(3)) for j in range(3)) for i in range(3))
def rotation(axis,theta):
    columns = [add(add(scale(math.cos(theta),p),scale(math.sin(theta),cross(axis,p))),scale((1-math.cos(theta))*dot(axis,p),axis)) for p in IDENTITY]
    return tuple(tuple(p[i] for p in columns) for i in range(3))

H = math.sqrt((5+2*math.sqrt(5))/15)
RADIUS = math.acos(H)
LABELS = tuple(chr(65+i) for i in range(20))
PHASES = (0., ALPHA, TAU/3-ALPHA)
STOPS = sorted((k*TAU/3+t) % TAU for k in range(3) for t in PHASES)
DELTAS = sorted({round(math.remainder(b-a, TAU), 10) for a in STOPS for b in STOPS if abs(a-b)>1e-8})
DELTAS = [next(math.remainder(b-a, TAU) for a in STOPS for b in STOPS if abs(math.remainder(b-a, TAU)-d)<1e-8) for d in DELTAS]
A = Arrangement(RADIUS)


def transpose(r): return tuple(zip(*r))
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


class Piece:
    def __init__(self, face):
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
        return all((dot(n,p)-H)*(1 if self.mask & (1<<i) else -1) > (1e-8 if strict else -1e-9)
                   for i,n in enumerate(AXES))

    def extrema_local(self, d):
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


PIECES = [Piece(f) for f in A.faces]
CENTERS = [next(p for p in PIECES if p.mask == 1<<i) for i in range(20)]
CENTER_POSES = [[p.placed(rotation(n,t)) for t in PHASES] for p,n in zip(CENTERS,AXES)]
CENTER_FREE = [[[CENTERS[v].role(rotation(AXES[v],t),n)!=2 for n in AXES] for t in PHASES] for v in range(20)]
EDGE_POOL = None
PACKING = None


def edge_pool():
    global EDGE_POOL
    if EDGE_POOL is not None:return EDGE_POOL
    edge = next(p for p in PIECES if p.mask.bit_count()==2)
    points = [unit(add(u,v)) for i,u in enumerate(AXES) for v in AXES[i+1:] if abs(dot(u,v)-math.sqrt(5)/3)<1e-9]
    groups=[[] for _ in points]
    entries={key(compose(g,r)):compose(g,r) for r in build(2) for g in GROUP}
    for r in entries.values():
        placed=edge.placed(r)
        occupied=[i for i,p in enumerate(points) if placed.contains(p)]
        assert len(occupied)==1
        groups[occupied[0]].append(placed)
    assert set(map(len,groups))=={10}
    EDGE_POOL=groups
    return groups


def fits_edges(piece,r):
    placed=piece.placed(r)
    return all(any(not placed.overlaps(edge) for edge in group) for group in edge_pool())


class Packing:
    def __init__(self):
        edge=next(p for p in PIECES if p.mask.bit_count()==2)
        symmetry=next(g for g in GROUP if key(g)!=key(IDENTITY) and A.mask(apply(g,edge.center))==edge.mask)
        def body_key(p):return min(key(p.r),key(compose(p.r,symmetry)))
        self.groups=[list({body_key(p):p for p in group}.values()) for group in edge_pool()]+CENTER_POSES
        assert list(map(len,self.groups))==[5]*30+[3]*20
        self.neighbors=[{} for _ in self.groups]
        for i,group in enumerate(self.groups):
            for j in range(i):
                other=self.groups[j]
                ij=[sum(1<<b for b,q in enumerate(other) if not p.overlaps(q)) for p in group]
                ji=[sum(1<<a for a,row in enumerate(ij) if row & (1<<b)) for b in range(len(other))]
                if all(row==(1<<len(other))-1 for row in ij):continue
                self.neighbors[i][j]=ij;self.neighbors[j][i]=ji
        self.calls=0;self.nodes=0

    def possible(self,piece,r):
        placed=piece.placed(r)
        domains=[sum(1<<i for i,p in enumerate(group) if not placed.overlaps(p)) for group in self.groups]
        self.calls+=1
        def reduce(domains,arcs):
            while arcs:
                i,j=arcs.pop()
                allowed=self.neighbors[i][j]
                value=sum(1<<a for a in range(len(allowed)) if domains[i] & (1<<a) and allowed[a]&domains[j])
                if value!=domains[i]:
                    if not value:return False
                    domains[i]=value
                    arcs.extend((k,i) for k in self.neighbors[i] if k!=j)
            return True
        def solve(domains,arcs):
            self.nodes+=1
            if not all(domains) or not reduce(domains,arcs):return False
            options=[i for i,d in enumerate(domains) if d.bit_count()>1]
            if not options:return True
            i=min(options,key=lambda j:(domains[j].bit_count(),-len(self.neighbors[j])))
            for a in range(len(self.groups[i])):
                if not domains[i]&(1<<a):continue
                d=domains.copy();d[i]=1<<a
                if solve(d,[(j,i) for j in self.neighbors[i]]):return True
            return False
        return solve(domains,[(i,j) for i,row in enumerate(self.neighbors) for j in row])


def fits_packing(piece,r):
    global PACKING
    if PACKING is None:PACKING=Packing()
    return PACKING.possible(piece,r)


def compatibility(piece,r):
    placed = piece.placed(r)
    return [sum(1<<i for i,center in enumerate(poses) if not placed.overlaps(center)) for poses in CENTER_POSES]


def canonical(r):
    orbit = [compose(g,r) for g in GROUP]
    return min((key(q),q) for q in orbit)


def build(kind):
    piece = next(p for p in PIECES if p.mask.bit_count()==kind)
    k,r = canonical(IDENTITY)
    seen = {k:(r,compatibility(piece,r))}
    queue = deque([k])
    rejected = set()
    while queue:
        source=queue.popleft();r,compatible=seen[source]
        for axis,n in enumerate(AXES):
            if piece.role(r,n)!=1:continue
            for delta in DELTAS:
                pairs=[(s,t) for s,p in enumerate(PHASES) for t,q in enumerate(PHASES)
                       if abs(math.remainder(q-p-delta,TAU/3))<1e-8 and compatible[axis] & (1<<s)]
                if not pairs:continue
                candidate=compose(rotation(n,delta),r)
                ck,cr=canonical(candidate)
                if ck in seen or ck in rejected:continue
                comp = compatibility(piece,candidate)
                valid=all(comp)
                intrinsic=valid and (kind!=3 or fits_packing(piece,candidate))
                valid=intrinsic
                if valid:
                    valid=any(comp[axis] & (1<<t) for s,t in pairs)
                if valid:
                    valid=all(any((compatible[v]&comp[v]&(1<<phase)) and CENTER_FREE[v][phase][axis] for phase in range(3))
                              for v in range(20) if v!=axis)
                if not valid:
                    # Only intrinsic fit-to-centers rejection is invariant
                    # under I and independent of the source/turn.
                    if not intrinsic:rejected.add(ck)
                    continue
                cc=compatibility(piece,cr)
                seen[ck]=(cr,cc);queue.append(ck)
                assert len(seen)<100,'Unbounded relaxation'
    return [r for r,c in seen.values()]


class Model:
    """Closed connected-piece catalogs, independent of the research workspace."""
    kinds = ('center','edge','petal')

    def __init__(self):
        self.prototypes={kind:next(p for p in PIECES if p.mask.bit_count()==i) for i,kind in enumerate(self.kinds,1)}
        self.stops=list(STOPS)
        self.deltas=list(DELTAS)
        self.pairs=[[(s,t) for s,a in enumerate(STOPS) for t,b in enumerate(STOPS)
                     if abs(math.remainder(b-a-delta,TAU))<1e-8] for delta in DELTAS]
        cosets={'center':[rotation(self.prototypes['center'].center,t) for t in PHASES],
                'edge':build(2),'petal':build(3)}
        assert {k:len(v) for k,v in cosets.items()}=={'center':3,'edge':5,'petal':10}
        self.poses={}
        self.lookups={}
        self.bins={}
        for kind in self.kinds:
            poses={key(compose(g,r)):compose(g,r) for r in cosets[kind] for g in GROUP}
            self.poses[kind]=[poses[k] for k in sorted(poses)]
            self.lookups[kind]={key(r):i for i,r in enumerate(self.poses[kind])}
            self.bins[kind]={}
            for i,r in enumerate(self.poses[kind]):
                b=tuple(math.floor(x/1e-5) for x in r[0])
                self.bins[kind].setdefault(b,[]).append(i)
        assert {k:len(v) for k,v in self.poses.items()}=={'center':180,'edge':300,'petal':600}
        self.pieces=[]
        for face in A.faces:
            kind=self.kinds[face['mask'].bit_count()-1]
            prototype=self.prototypes[kind]
            home=next(g for g in GROUP if A.mask(apply(g,prototype.center))==face['mask'])
            caps=[i for i in range(20) if face['mask']&(1<<i)]
            self.pieces.append(dict(id=kind+'/'+('-'.join(LABELS[i] for i in caps)),kind=kind,
                                    home=self.index(kind,home),frame=home,caps=caps))
        self.centers=[next(p for p in self.pieces if p['kind']=='center' and p['caps']==[i]) for i in range(20)]
        self.driver_placements=[[self.index('center',compose(rotation(n,t),p['frame'])) for t in STOPS]
                                for n,p in zip(AXES,self.centers)]
        self.roles={kind:[[self.prototypes[kind].role(r,n) for n in AXES] for r in self.poses[kind]] for kind in self.kinds}
        self.maps={}
        for axis,n in enumerate(AXES):
            for d,delta in enumerate(DELTAS):
                rot=rotation(n,delta)
                self.maps[axis,d]={}
                for kind in self.kinds:
                    transport={}
                    for q,r in enumerate(self.poses[kind]):
                        if self.roles[kind][q][axis]!=1:continue
                        target=self.index(kind,compose(rot,r))
                        if target is not None:transport[q]=target
                    self.maps[axis,d][kind]=transport
        self.locations={};self.location_of={};self.geometry=[]
        for kind in self.kinds:
            prototype=self.prototypes[kind]
            stabilizer=[g for g in GROUP if A.mask(apply(g,prototype.center))==prototype.mask]
            locations={}
            for q,r in enumerate(self.poses[kind]):
                shape=min(key(compose(r,g)) for g in stabilizer)
                locations.setdefault(shape,[]).append(q)
            self.locations[kind]=list(locations.values())
            self.location_of[kind]={q:i for i,qs in enumerate(self.locations[kind]) for q in qs}
            for i,qs in enumerate(self.locations[kind]):
                self.geometry.append((kind,i,prototype.placed(self.poses[kind][qs[0]])))
        assert {k:len(v) for k,v in self.locations.items()}=={'center':60,'edge':150,'petal':600}
        self.conflicts=[]
        for i,(kind,location,p) in enumerate(self.geometry):
            for other_kind,other_location,q in self.geometry[:i]:
                if p.overlaps(q):self.conflicts.append(((kind,location),(other_kind,other_location)))
        self.edge_anchors=[unit(add(u,v)) for i,u in enumerate(AXES) for v in AXES[i+1:]
                           if abs(dot(u,v)-math.sqrt(5)/3)<1e-9]
        self.edge_slots=[[] for _ in self.edge_anchors]
        for q,r in enumerate(self.poses['edge']):
            occupied=[i for i,p in enumerate(self.edge_anchors) if self.prototypes['edge'].placed(r).contains(p)]
            assert len(occupied)==1
            self.edge_slots[occupied[0]].append(q)
        assert set(map(len,self.edge_slots))=={10}

    def index(self,kind,r):
        found=self.lookups[kind].get(key(r))
        if found is not None:return found
        from itertools import product
        b=tuple(math.floor(x/1e-5) for x in r[0])
        for offsets in product((-1,0,1),repeat=3):
            for i in self.bins[kind].get(tuple(a+d for a,d in zip(b,offsets)),[]):
                q=self.poses[kind][i]
                if max(abs(a-b) for x,y in zip(r,q) for a,b in zip(x,y))<1e-6:return i
        return None

    def initial(self):return [p['home'] for p in self.pieces]

    def available(self,state):
        return [i for i in range(20) if all(self.roles[p['kind']][q][i]!=2 for p,q in zip(self.pieces,state))]

    def turn(self,state,axis,d):
        if axis not in self.available(state):return None
        driver=state[self.pieces.index(self.centers[axis])]
        allowed={self.driver_placements[axis][s] for s,t in self.pairs[d]}
        if driver not in allowed:return None
        result=[]
        for p,q in zip(self.pieces,state):
            if self.roles[p['kind']][q][axis]!=1:result.append(q);continue
            target=self.maps[axis,d][p['kind']].get(q)
            if target is None:return None
            result.append(target)
        return result
