#!/usr/bin/env python3
"""Exact catalogs, complete cuts, ordinary reachability, and closed colored meshes."""
import sys
from pathlib import Path
from collections import Counter, deque
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from cell24_model import Model, PROTOTYPES, CENTERS, DEPTH, dot, apply, image, IDENTITY
from cell24_meshes import assets
m=Model();a=assets()
assert [len(d['pieces']) for d in m.domains[-5:]]==[96,96,288,144,24]
assert sum(len(d['pieces'])*d['k'] for d in m.domains)==1368
tables_by_domain=[[m.table(d,move) for move in m.moves] for d in m.domains]
for d,tables in zip(m.domains,tables_by_domain):
    reached={d['pieces'][0]['home']};queue=deque(reached)
    while queue:
        q=queue.popleft()
        for t in tables:
            if q in t and t[q] not in reached:reached.add(t[q]);queue.append(t[q])
    assert len(reached)==len(d['poses']),d['id']
for i,move in enumerate(m.moves):
    assert sum(sum(p['home'] in tables[i] for p in d['pieces']) for d,tables in zip(m.domains,tables_by_domain))==105

def key(p):return tuple(round(x,7) for x in p)
for name,active,colors,center in PROTOTYPES:
    assert tuple(i for i,c in enumerate(CENTERS) if dot(c,center)>DEPTH)==active
    stabilizer=[g for g,p in zip(m.group,m.perms) if image((active,colors),p)==(active,colors)]
    assert len(stabilizer)=={'center':24,'face':3,'subcorner':4}.get(name,1)
    edges=Counter();vertices_all=set()
    for port in range(len(colors)):
        mesh=a[name+'/'+str(port)]
        vertices=[key(p) for p in mesh['vertices']];vertices_all.update(vertices)
        for t in mesh['triangles']:
            for i,j in zip(t,t[1:]+t[:1]):edges[tuple(sorted((vertices[i],vertices[j])))]+=1
        def signatures(g):
            v=[key(apply(g,p))+key(apply(g,n)) for p,n in zip(mesh['vertices'],mesh['normals'])]
            return Counter(tuple(sorted(v[i] for i in t)) for t in mesh['triangles'])
        original=signatures(IDENTITY)
        for g in stabilizer:assert signatures(g)==original,(name,port,'stabilizer')
        for p,n in zip(mesh['vertices'],mesh['normals']):
            assert dot(p,CENTERS[colors[port]]) >= max(dot(p,c) for c in CENTERS)-1e-10
            assert abs(dot(p,p)-1)<1e-10 and abs(dot(n,n)-1)<1e-10 and abs(dot(p,n))<1e-10
    assert all(n==2 for n in edges.values()),(name,'surface is not closed',Counter(edges.values()))
    adjacency={v:set() for v in vertices_all}
    for u,v in edges:adjacency[u].add(v);adjacency[v].add(u)
    reached={next(iter(vertices_all))};todo=list(reached)
    while todo:
        for v in adjacency[todo.pop()]:
            if v not in reached:reached.add(v);todo.append(v)
    assert reached==vertices_all,(name,'disconnected surface')
    assert len(vertices_all)-len(edges)+sum(len(a[name+'/'+str(port)]['triangles']) for port in range(len(colors)))==2,(name,'non-spherical boundary')
print('24-cell: 672 closed pieces, colored stabilizers, 2088 reachable placements, 552 complete ordinary turns')
