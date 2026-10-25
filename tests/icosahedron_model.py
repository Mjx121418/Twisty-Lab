"""Independent spherical legality, finite closure, and conical-solid checks."""
from collections import defaultdict
import math
from pathlib import Path
import random
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from icosahedron_geometry import AXES, add, sub, scale, dot, cross, norm
from icosahedron_model import A, H, PHASES, DELTAS, TAU, Model, Packing, rotation, compose, apply
from icosahedron_meshes import assets, DISPLAY_RADIUS, INRADIUS


def feasible(packing,domains):
    def reduce(values,arcs):
        while arcs:
            i,j=arcs.pop();rows=packing.neighbors[i][j]
            new=sum(1<<a for a,row in enumerate(rows) if values[i]&(1<<a) and row&values[j])
            if new!=values[i]:
                if not new:return False
                values[i]=new;arcs.extend((k,i) for k in packing.neighbors[i] if k!=j)
        return True
    def solve(values,arcs):
        if not all(values) or not reduce(values,arcs):return False
        options=[i for i,v in enumerate(values) if v.bit_count()>1]
        if not options:return True
        i=min(options,key=lambda j:values[j].bit_count())
        for bit in range(len(packing.groups[i])):
            if values[i]&(1<<bit):
                branch=values.copy();branch[i]=1<<bit
                if solve(branch,[(j,i) for j in packing.neighbors[i]]):return True
        return False
    return solve(domains,[(i,j) for i,row in enumerate(packing.neighbors) for j in row])


class Icosahedron(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.model=Model()

    def test_connected_regions_and_regular_icosahedron(self):
        self.assertEqual(len(A.faces),110)
        self.assertAlmostEqual(sum(f['area'] for f in A.faces),4*math.pi,delta=1e-10)
        self.assertEqual(len(A.edges)-len(A.vertices)+2,110)
        exterior_area=0.
        spherical_area=0.
        for kind,multiplicity in [('center',20),('edge',30),('petal',60)]:
            for spherical in (False,True):
                body,ports=assets(self.model.prototypes[kind],spherical)
                self.assertEqual(len(ports),2 if kind=='edge' else 1)
                for port,asset in ports.items():
                    n=AXES[int(port)]
                    for v in asset['vertices']:
                        p=(v[0],v[2],v[1])
                        if spherical:self.assertAlmostEqual(norm(p),DISPLAY_RADIUS,delta=1e-10)
                        else:
                            self.assertAlmostEqual(dot(n,p),INRADIUS,delta=1e-10)
                            self.assertLessEqual(max(dot(other,p) for other in AXES),INRADIUS+1e-9)
                        self.assertGreaterEqual(dot(n,p),max(dot(other,p) for other in AXES)-1e-9)
                radial=set(body.get('radialNormalTriangles',[]))
                for i,triangle in enumerate(body['triangles']):
                    p,q,r=[body['vertices'][v] for v in triangle]
                    normal=cross(sub(q,p),sub(r,p))
                    self.assertGreater(norm(normal),1e-12)
                    if any(norm(v)<1e-10 for v in (p,q,r)):continue
                    self.assertGreater(dot(normal,p),0.)
                    if spherical:
                        self.assertIn(i,radial)
                        p,q,r=[scale(1/norm(v),v) for v in (p,q,r)]
                        spherical_area+=multiplicity*2*math.atan2(abs(dot(p,cross(q,r))),1+dot(p,q)+dot(q,r)+dot(r,p))
                    else:exterior_area+=multiplicity*norm(normal)/2
        edge=4*DISPLAY_RADIUS/math.sqrt(10+2*math.sqrt(5))
        self.assertAlmostEqual(exterior_area,20*math.sqrt(3)/4*edge*edge*.994**2,delta=1e-8)
        self.assertAlmostEqual(spherical_area,4*math.pi,delta=1e-8)

    def test_catalog_walks_against_cut_circle_coverage(self):
        model=self.model
        intervals={kind:[] for kind in model.kinds}
        for kind in model.kinds:
            for pose in model.poses[kind]:
                result=defaultdict(list)
                for n,start,end,dt,*_ in model.prototypes[kind].arcs:
                    normal=apply(pose,n)
                    axis=min(range(20),key=lambda i:norm(sub(normal,AXES[i])))
                    if norm(sub(normal,AXES[axis]))>1e-7:continue
                    n=AXES[axis];p=apply(pose,start)
                    x=cross(n,(1.,0.,0.) if abs(n[0])<.9 else (0.,1.,0.));x=scale(1/norm(x),x);y=cross(n,x)
                    t=math.atan2(dot(p,y),dot(p,x))%TAU
                    if t+dt<=TAU:result[axis].append((t,t+dt))
                    else:result[axis].extend([(t,TAU),(0.,t+dt-TAU)])
                intervals[kind].append(result)
        def available(state):
            by_axis=defaultdict(list)
            for piece,q in zip(model.pieces,state):
                for axis,arcs in intervals[piece['kind']][q].items():by_axis[axis].extend(arcs)
            result=[]
            for axis,arcs in by_axis.items():
                end=0.
                for a,b in sorted(arcs):
                    if a>end+1e-7:break
                    end=max(end,b)
                if end>=TAU-1e-7:result.append(axis)
            return sorted(result)
        conflicts={frozenset((a,b)) for a,b in model.conflicts}
        state=model.initial();rng=random.Random(20261025);history=[]
        for step in range(1000):
            axes=model.available(state)
            if step%10==0:self.assertEqual(axes,available(state))
            choices=[(axis,d) for axis in axes for d in range(len(DELTAS))];rng.shuffle(choices)
            for axis,d in choices:
                target=model.turn(state,axis,d)
                if target is not None:break
            else:self.fail('No legal continuation')
            inverse=next(i for i,t in enumerate(DELTAS) if abs(t+DELTAS[d])<1e-8)
            self.assertEqual(model.turn(target,axis,inverse),state)
            occupied={(p['kind'],model.location_of[p['kind']][q]) for p,q in zip(model.pieces,target)}
            self.assertEqual(len(occupied),110)
            self.assertFalse(any(pair<=occupied for pair in conflicts))
            history.append((axis,d));state=target
        for axis,d in reversed(history):
            inverse=next(i for i,t in enumerate(DELTAS) if abs(t+DELTAS[d])<1e-8)
            state=model.turn(state,axis,inverse)
            self.assertIsNotNone(state)
        self.assertEqual(state,model.initial())

    def test_handoff_phase_closure(self):
        model=self.model;packing=Packing();u=AXES[0]
        candidates={}
        for kind in model.kinds:
            piece=model.prototypes[kind]
            for q,pose in enumerate(model.poses[kind]):
                if model.roles[kind][q][0]!=1:continue
                for n,*_ in piece.arcs:
                    normal=apply(pose,n);d=dot(normal,u)
                    if abs(d)>1-1e-8:continue
                    for v in AXES:
                        if dot(u,v)<=math.cos(2*A.radius)+1e-9 or abs(dot(u,v)-d)>1e-7:continue
                        theta=math.atan2(dot(u,cross(normal,v)),dot(normal,v)-d*d)%TAU
                        if min(theta,TAU-theta)>1e-7:candidates[round(theta,6)]=theta
        self.assertTrue(all(any(abs(math.remainder(t-d,TAU))<1e-7 for d in DELTAS) for t in candidates.values()))
        free=[[p.piece.role(p.r,u) for p in group] for group in packing.groups]
        cases=0
        for delta in DELTAS:
            transform=rotation(u,delta)
            for phase,value in enumerate(PHASES):
                if any(abs(math.remainder(value+delta-other,TAU/3))<1e-7 for other in PHASES):continue
                for target,v in enumerate(AXES):
                    if target==0 or dot(u,v)<=math.cos(2*A.radius)+1e-9:continue
                    domains=[]
                    for i,group in enumerate(packing.groups):
                        allowed=0
                        for q,p in enumerate(group):
                            role=free[i][q]
                            if role==2 or (i==30 and q!=phase):continue
                            if role==1 and p.piece.role(compose(transform,p.r),v)==2:continue
                            allowed|=1<<q
                        domains.append(allowed)
                    self.assertFalse(feasible(packing,domains),'A new center phase supports a handoff')
                    cases+=1
        self.assertEqual(cases,162)


if __name__=='__main__':unittest.main()
