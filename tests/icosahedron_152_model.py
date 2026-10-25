"""Planar clipping, independent handoff search, and witnessed pose checks."""
from collections import defaultdict
import math
from pathlib import Path
import random
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from icosahedron_geometry import AXES, add, sub, scale, dot, cross, norm, unit
from icosahedron_152_model import A, H, TAU, DELTAS, Model, rotation, compose, apply, key
from icosahedron_152_meshes import assets, polyhedron, DISPLAY_RADIUS, INRADIUS, CUT_OFFSET


class Icosahedron152(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.model = Model()

    def test_regions_and_literal_planar_cells(self):
        model = self.model
        self.assertEqual(len(A.faces),152)
        self.assertEqual(len(A.edges)-len(A.vertices)+2,152)
        self.assertAlmostEqual(sum(f['area'] for f in A.faces),4*math.pi,delta=1e-9)
        area = 0.; spherical_area = 0.
        for kind,multiplicity in zip(model.kinds,(60,60,12,20)):
            piece = model.prototypes[kind]
            vertices,faces = polyhedron(piece.mask)
            self.assertGreater(len(vertices),3)
            for face,center,points in faces:
                normal = AXES[face%20]
                offset = INRADIUS if face < 20 else CUT_OFFSET
                for p in points: self.assertAlmostEqual(dot(normal,p),offset,delta=1e-8)
                if face < 20:
                    area += multiplicity*sum(norm(cross(sub(a,center),sub(b,center)))/2
                        for a,b in zip(points,points[1:]+points[:1]))
            euclidean = assets(piece,False); spherical = assets(piece,True)
            self.assertEqual(set(euclidean[1]),set(spherical[1]))
            body = spherical[0]
            for i in body['radialNormalTriangles']:
                p,q,r = [unit(body['vertices'][v]) for v in body['triangles'][i]]
                self.assertGreater(dot(p,cross(q,r)),1e-12)
                spherical_area += multiplicity*2*math.atan2(dot(p,cross(q,r)),1+dot(p,q)+dot(q,r)+dot(r,p))
            for is_spherical,(_,ports) in [(False,euclidean),(True,spherical)]:
                for port,asset in ports.items():
                    n = AXES[int(port)]
                    for vertex in asset['vertices']:
                        p = (vertex[0],vertex[2],vertex[1])
                        if is_spherical:
                            self.assertAlmostEqual(norm(p),DISPLAY_RADIUS,delta=1e-9)
                            self.assertTrue(piece.contains_local(unit(p)))
                        else:
                            self.assertAlmostEqual(dot(n,p),INRADIUS,delta=1e-9)
                            for other in AXES: self.assertLessEqual(dot(other,p),INRADIUS+1e-8)
                        self.assertGreaterEqual(dot(n,p),max(dot(other,p) for other in AXES)-1e-8)
        circumradius = INRADIUS/math.sqrt((5+2*math.sqrt(5))/15)
        edge = 4*circumradius/math.sqrt(10+2*math.sqrt(5))
        self.assertAlmostEqual(area,20*math.sqrt(3)*edge*edge/4,delta=1e-8)
        self.assertAlmostEqual(spherical_area,4*math.pi,delta=1e-8)

    def test_witness_and_independent_geometric_handoffs(self):
        model = self.model
        state = model.initial()
        for axis,d in model.witness:
            state = model.turn(state,axis,d)
            self.assertIsNotNone(state)
        intervals = {}
        def arcs(kind,r):
            cache = kind,key(r)
            if cache in intervals: return intervals[cache]
            result = defaultdict(list)
            for n,start,end,dt,*_ in model.prototypes[kind].arcs:
                normal = apply(r,n)
                axis = max(range(20),key=lambda i:dot(normal,AXES[i]))
                if norm(sub(normal,AXES[axis])) > 1e-7: continue
                n = AXES[axis]; point = apply(r,start)
                x = unit(cross(n,(1.,0.,0.) if abs(n[0]) < .9 else (0.,1.,0.)))
                y = cross(n,x)
                t = math.atan2(dot(point,y),dot(point,x))%TAU
                if t+dt <= TAU: result[axis].append((t,t+dt))
                else: result[axis].extend([(t,TAU),(0.,t+dt-TAU)])
            intervals[cache] = result
            return result
        def available(frames):
            by_axis = defaultdict(list)
            for piece,r in zip(model.pieces,frames):
                for axis,parts in arcs(piece['kind'],r).items(): by_axis[axis].extend(parts)
            result = []
            for axis,parts in by_axis.items():
                end = 0.
                for a,b in sorted(parts):
                    if a > end+1e-7: break
                    end = max(end,b)
                if end >= TAU-1e-7: result.append(axis)
            return sorted(result)
        rng = random.Random(1522026); state = model.initial(); history = []; last = None
        conflicts = {frozenset((a,b)) for a,b in model.conflicts}
        for step in range(500):
            frames = [model.poses[p['kind']][q] for p,q in zip(model.pieces,state)]
            axes = available(frames)
            self.assertEqual(axes,model.available(state))
            rng.shuffle(axes); axes.sort(key=lambda a:a == last)
            choices = []
            for axis in axes:
                u = AXES[axis]
                moving = [dot(u,apply(r,model.prototypes[p['kind']].center)) > H
                    for p,r in zip(model.pieces,frames)]
                normals = {tuple(round(x,7) for x in apply(r,n)):apply(r,n)
                    for p,r,inside in zip(model.pieces,frames,moving) if inside
                    for n,*_ in model.prototypes[p['kind']].arcs}
                angles = {}
                targets = [v for i,v in enumerate(AXES) if i != axis and dot(u,v) > math.cos(2*A.radius)+1e-8]
                for n in normals.values():
                    d = dot(u,n)
                    if abs(d) > 1-1e-8: continue
                    for v in targets:
                        if abs(dot(u,v)-d) > 1e-7: continue
                        theta = math.atan2(dot(u,cross(n,v)),dot(n,v)-d*d)%TAU
                        if min(theta,TAU-theta) > 1e-7: angles[round(theta,6)] = theta
                for theta in angles.values():
                    rot = rotation(u,theta)
                    destination = [compose(rot,r) if inside else r for r,inside in zip(frames,moving)]
                    if not any(i != axis and dot(u,AXES[i]) > math.cos(2*A.radius)+1e-8 for i in available(destination)): continue
                    d = min(range(len(DELTAS)),key=lambda d:abs(math.remainder(theta-DELTAS[d],TAU)))
                    self.assertLess(abs(math.remainder(theta-DELTAS[d],TAU)),1e-7,'Uncataloged useful angle')
                    target = [model.index(p['kind'],r) for p,r in zip(model.pieces,destination)]
                    self.assertNotIn(None,target,'A geometric handoff left the pose catalog')
                    self.assertEqual(model.turn(state,axis,d),target)
                    choices.append((axis,d,target))
                if choices: break
            self.assertTrue(choices,'No geometric continuation')
            jumbles = [c for c in choices if abs(math.remainder(DELTAS[c[1]],TAU/3)) > 1e-7]
            axis,d,target = rng.choice(jumbles if jumbles and rng.random() < .7 else choices)
            inverse = next(i for i,t in enumerate(DELTAS) if abs(t+DELTAS[d]) < 1e-8)
            self.assertEqual(model.turn(target,axis,inverse),state)
            if step%10 == 0:
                occupied = {(p['kind'],model.location_of[p['kind']][q]) for p,q in zip(model.pieces,target)}
                self.assertEqual(len(occupied),152)
                self.assertFalse(any(pair <= occupied for pair in conflicts))
            history.append((axis,inverse)); state = target; last = axis
        for axis,d in reversed(history):
            state = model.turn(state,axis,d)
            self.assertIsNotNone(state)
        self.assertEqual(state,model.initial())


if __name__ == '__main__': unittest.main()
