"""Exact ordinary 24-cell turns and reachable colored placements (no jumbling)."""
from itertools import permutations, product
import math
from tesseract_model import dot, unit, cross4

ID = 'cell24'
DEPTH = 2/3
CENTERS = tuple([tuple(float(s if i == j else 0) for i in range(4)) for j in range(4) for s in (1,-1)] +
                [tuple(s/2 for s in ss) for ss in product((1,-1), repeat=4)])
CINDEX = {c:i for i,c in enumerate(CENTERS)}
IDENTITY = tuple(tuple(float(i == j) for j in range(4)) for i in range(4))
# Opposite pairs share a hue; the two lightnesses keep opposite cells distinct.
PALETTE = ('#ed665e','#ac393b','#efba52','#a77826','#67c79a','#277d68','#86a8f2','#485ba8',
           '#c782e0','#864b9e','#e99ac0','#a4507c','#71d4de','#318791','#a3d879','#63963f',
           '#f29668','#b96435','#b0b8ed','#737cab','#e8dc94','#a89d55','#d2bca5','#8b7765')
PROTOTYPES = (
    ('center', (0,), (0,), (1,0,0,0)),
    ('face', (0,8), (0,8), unit((3,1,1,1))),
    ('edge', (0,8,9), (0,8,9), unit((2,1,1,0))),
    ('face-side', (0,2,8,9), (8,9), unit((1,1,math.sqrt(.08),0))),
    ('subcorner', (2,8,9,10,11), (2,), unit((2,3,0,0))),
    ('corner', (0,2,8,9,10,11), (0,2,8,9,10,11), unit((1,1,0,0))),
)

def label(i):
    if i < 8: return 'XYZW'[i//2]+('p' if i%2 == 0 else 'm')
    return 'H'+''.join('p' if x > 0 else 'm' for x in CENTERS[i])
def apply(a,p): return tuple(dot(row,p) for row in a)
def transpose(a): return tuple(zip(*a))
def compose(a,b): return tuple(tuple(dot(row,col) for col in zip(*b)) for row in a)
def frame(a): return [x for col in zip(*a) for x in col]
def left(q):
    w,x,y,z=q
    return ((w,-x,-y,-z),(x,w,-z,y),(y,z,w,-x),(z,-y,x,w))
def permutation(g): return tuple(CINDEX[apply(g,c)] for c in CENTERS)
def image(pose,perm): return tuple(sorted(perm[i] for i in pose[0])),tuple(perm[i] for i in pose[1])
def octahedral():
    result=[]
    for pp in permutations(range(3)):
        parity=(-1)**sum(pp[i]>pp[j] for i in range(3) for j in range(i+1,3))
        for ss in product((1,-1),repeat=3):
            if parity*math.prod(ss)!=1: continue
            m=[list(row) for row in IDENTITY]
            for i in range(3):
                for j in range(3): m[i+1][j+1]=float(ss[j] if i==pp[j] else 0)
            result.append(tuple(map(tuple,m)))
    return sorted(result,key=lambda m:(m!=IDENTITY,m))

class Model:
    def __init__(self):
        self.group=sorted((compose(left(c),s) for c in CENTERS for s in octahedral()),key=lambda g:(g!=IDENTITY,g))
        assert len(set(self.group))==576
        self.perms=[permutation(g) for g in self.group]
        self.moves=[];self.groups=[]
        for cell,c in enumerate(CENTERS):
            categories={k:[] for k in ('face','edge','vertex')}
            local=left(c)
            for a in product((-1,0,1),repeat=3):
                if not any(a) or next(x for x in a if x)<0: continue
                k=sum(x!=0 for x in a);kind={1:'vertex',2:'edge',3:'face'}[k]
                axis_id=kind[0]+''.join('XYZ'[i]+('m' if x<0 else '') for i,x in enumerate(a) if x)
                axis=apply(local,(0,)+unit(a))
                e=min(IDENTITY,key=lambda e:abs(dot(e,c))+abs(dot(e,axis)))
                u=unit(tuple(e[i]-dot(e,c)*c[i]-dot(e,axis)*axis[i] for i in range(4)))
                v=unit(cross4(c,axis,u));turns=[]
                for degrees in {1:(90,-90,180),2:(180,),3:(120,-120)}[k]:
                    theta=math.radians(degrees);co,si=math.cos(theta)-1,math.sin(theta)
                    g=tuple(tuple(round(2*(float(i==j)+co*(u[i]*u[j]+v[i]*v[j])+si*(v[i]*u[j]-u[i]*v[j])))/2
                                  for j in range(4)) for i in range(4))
                    assert g in self.group and apply(g,c)==c
                    name=label(cell)+'_'+axis_id+('_inv' if degrees<0 else '_2' if degrees==180 and k==1 else '')
                    self.moves.append(dict(id=name,cell=label(cell),cellIndex=cell,group=label(cell),kind=kind,
                                           planeU=u,planeV=v,angle=theta,matrix=g,perm=permutation(g)))
                    turns.append(dict(operation=name,angleDegrees=degrees))
                categories[kind].append(dict(id=axis_id,label=' / '.join(('+' if x>0 else '−')+'XYZ'[i] for i,x in enumerate(a) if x),direction=axis,turns=turns))
            self.groups.append(dict(id=label(cell),label=(('+' if cell%2==0 else '−')+'XYZW'[cell//2]) if cell<8 else label(cell),
                                    center=c,categories=[dict(kind=k,axes=v) for k,v in categories.items()]))
        for m in self.moves:
            m['inverse']=next(n['id'] for n in self.moves if n['cell']==m['cell'] and compose(n['matrix'],m['matrix'])==IDENTITY)
        self.domains=[]
        for name,active,colors,center in PROTOTYPES:
            reps={}
            for g,p in zip(self.group,self.perms):reps.setdefault(image((active,colors),p),g)
            poses=sorted(reps)
            if name=='center':
                for pose in poses:
                    self.domains.append(dict(id='center-'+label(pose[0][0]),prototype=name,active=active,colors=colors,center=center,
                        poses=[pose],frames=[reps[pose]]))
            else:self.domains.append(dict(id={'face':'f','edge':'e','face-side':'s','subcorner':'u','corner':'v'}[name],prototype=name,active=active,colors=colors,center=center,poses=poses,frames=[reps[p] for p in poses]))
        for d in self.domains:
            d['k']=len(d['colors']);d['index']={p:q for q,p in enumerate(d['poses'])}
            slots={}
            for q,p in enumerate(d['poses']):slots.setdefault(p[0],q)
            d['pieces']=[dict(id=d['id']+'/'+format(j,'x'),home=q) for j,(active,q) in enumerate(slots.items())]
        assert len(self.moves)==552
        assert sum(len(d['pieces']) for d in self.domains)==672
        assert sum(len(d['poses']) for d in self.domains)==2088
    def table(self,d,m):
        return {q:d['index'][image(p,m['perm'])] for q,p in enumerate(d['poses']) if m['cellIndex'] in p[0]}
    def actions(self):
        result=[]
        for perm in self.perms:
            action=[];offset=0
            for d in self.domains:
                action.extend(offset+(q if d['prototype']=='center' else d['index'][image(p,perm)]) for q,p in enumerate(d['poses']))
                offset+=len(d['poses'])
            result.append(action)
        return result
