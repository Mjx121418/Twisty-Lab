#!/usr/bin/env python3
"""Check exact orbits and the unmarked stabilizers used to canonicalize meshes."""
import sys
from pathlib import Path
from collections import Counter
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from tesseract_model import Model, compose, image, dot
from tesseract_meshes import assets

m=Model()
assert len(m.group)==192
for g in m.group:
    for h in m.group: assert compose(g,h) in m.group
for d in m.domains:
    reached={d['pieces'][0]['home']}
    while True:
        nexts=reached|{q for move in m.moves for p,q in m.table(d,move).items() if p in reached}
        if nexts==reached: break
        reached=nexts
    assert len(reached)==len(d['poses'])

def transform(g,p):
    v=[0.]*4
    for j,c in enumerate(g):v[abs(c)-1]=(1 if c>0 else -1)*p[j]
    return v

def key(p):return tuple(round(x,8) for x in p)
for k in range(1,5):
    stabilizer=[g for g in m.group if g[:k]==tuple(range(1,k+1))]
    meshes=assets(k)
    edges=Counter()
    for a in meshes.values():
        vertices=[key(p) for p in a['vertices']]
        for triangle in a['triangles']:
            for i,j in zip(triangle,triangle[1:]+triangle[:1]):
                edges[tuple(sorted((vertices[i],vertices[j])))]+=1
    assert all(count==2 for count in edges.values()), 'Piece surface is not closed'
    for port,a in meshes.items():
        for p,n in zip(a['vertices'],a['normals']):
            assert abs(dot(p,p)-1)<1e-10 and abs(dot(n,n)-1)<1e-10 and abs(dot(p,n))<1e-10
            x=[p[i]/(1-p[3]) for i in range(3)]; r=dot(x,x)
            inverse=[2*v/(1+r) for v in x]+[(r-1)/(r+1)]
            assert max(abs(u-v) for u,v in zip(p,inverse))<1e-10
        signatures=[key(p)+key(n) for p,n in zip(a['vertices'],a['normals'])]
        original={tuple(sorted(signatures[i] for i in t)) for t in a['triangles']}
        for g in stabilizer:
            changed=[key(transform(g,p))+key(transform(g,n)) for p,n in zip(a['vertices'],a['normals'])]
            assert {tuple(sorted(changed[i] for i in t)) for t in a['triangles']}==original,(k,port,g)
print('S3: exact group, reachable catalogs, and colored mesh stabilizers passed')
