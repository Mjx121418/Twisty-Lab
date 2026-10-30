#!/usr/bin/env python3
"""Reachable catalogs and closed, nondegenerate meshes at exact 45° contacts."""
import sys
from pathlib import Path
from collections import Counter, deque
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from cell24_192_model import Model
from cell24_192_meshes import assets, SMALL_PROTOTYPES, DEPTH
from cell24_model import CENTERS, IDENTITY, apply, image, dot

model=Model();meshes=assets()
assert sum(len(d['pieces']) for d in model.domains)==216
assert sum(len(d['pieces']) for d in model.domains if d['prototype']!='center')==192
assert sum(len(d['pieces'])*d['k'] for d in model.domains)==504
tables=[[model.table(d,move) for move in model.moves] for d in model.domains]
for domain,actions in zip(model.domains,tables):
    reached={domain['pieces'][0]['home']};queue=deque(reached)
    while queue:
        q=queue.popleft()
        for action in actions:
            if q in action and action[q] not in reached:reached.add(action[q]);queue.append(action[q])
    assert len(reached)==len(domain['poses'])
for i,move in enumerate(model.moves):
    assert sum(sum(p['home'] in actions[i] for p in d['pieces']) for d,actions in zip(model.domains,tables))==21

key=lambda p:tuple(round(x,8) for x in p)
for name,active,colors,center in SMALL_PROTOTYPES:
    assert tuple(i for i,c in enumerate(CENTERS) if dot(c,center)>DEPTH)==active
    stabilizer=[g for g,p in zip(model.group,model.perms) if image((active,colors),p)==(active,colors)]
    edges=Counter();vertices=set();triangles=0
    for port in range(len(colors)):
        mesh=meshes[name+'/'+str(port)];vs=list(map(key,mesh['vertices']));vertices.update(vs);triangles+=len(mesh['triangles'])
        for t in mesh['triangles']:
            a,b,c=(mesh['vertices'][i] for i in t)
            u=tuple(y-x for x,y in zip(a,b));v=tuple(y-x for x,y in zip(a,c))
            assert dot(u,u)*dot(v,v)-dot(u,v)**2>1e-15,(name,port,'zero-area triangle')
            for i,j in zip(t,t[1:]+t[:1]):edges[tuple(sorted((vs[i],vs[j])))]+=1
        def signatures(g):
            moved=[key(apply(g,p))+key(apply(g,n)) for p,n in zip(mesh['vertices'],mesh['normals'])]
            return Counter(tuple(sorted(moved[i] for i in t)) for t in mesh['triangles'])
        original=signatures(IDENTITY)
        for g in stabilizer:assert signatures(g)==original,(name,port,'unmarked stabilizer')
        for p,n in zip(mesh['vertices'],mesh['normals']):
            assert abs(dot(p,p)-1)+abs(dot(n,n)-1)+abs(dot(p,n))<1e-10
            assert dot(p,CENTERS[colors[port]])>=max(dot(p,c) for c in CENTERS)-1e-10
            assert all((dot(p,c)>=DEPTH-1e-9) if i in active else (dot(p,c)<=DEPTH+1e-9) for i,c in enumerate(CENTERS))
    assert all(count==2 for count in edges.values()),(name,'open or nonmanifold boundary')
    assert len(vertices)-len(edges)+triangles==2,(name,'non-spherical boundary')
    graph={p:set() for p in vertices}
    for a,b in edges:graph[a].add(b);graph[b].add(a)
    reached={next(iter(vertices))};todo=list(reached)
    while todo:
        for p in graph[todo.pop()]:
            if p not in reached:reached.add(p);todo.append(p)
    assert reached==vertices,(name,'disconnected piece')
print('45° 24-cell: 192 movable + 24 centers, 792 reachable placements, closed meshes, and 552 complete 21-piece turns')
