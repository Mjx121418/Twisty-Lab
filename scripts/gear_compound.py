#!/usr/bin/env python3
"""Build a normal Gear Cube from six big and twelve small bevel-gear solids.

This is an offline construction study. Boolean fields and meshing live outside
the abstract core; the resulting meshes use the existing portable interpreter.
"""
import argparse
from collections import Counter, defaultdict
from copy import deepcopy
from functools import lru_cache
import json
from math import acos, atan, atan2, cos, pi, sin, sqrt
from pathlib import Path
import subprocess

import gear_meshes as shapes
from gear_model import frame, same_geometry

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'packages/gear-compound'
FACES = {'U': (0,1,0), 'R': (1,0,0), 'F': (0,0,1),
         'D': (0,-1,0), 'L': (-1,0,0), 'B': (0,0,-1)}
EDGES = ['UF','UR','UB','UL','DF','DR','DB','DL','FR','FL','BR','BL']
BIG_ANGLE = atan(20/6)
SMALL_ANGLE = atan(6/20)
TOOTH_DEPTH = pi/180
FACE = shapes.FACE
INNER_RADIUS = .92
PINION_INNER_RADIUS = 1.78
CLEARANCE = .008


def normalize(p):
    radius = sqrt(shapes.dot(p,p))
    return tuple(v/radius for v in p), radius


def pulse(phi, teeth, narrow=False):
    u = (phi*teeth/(2*pi)) % 1
    if narrow:
        u = .5 + 2*(u-.5)
    return 2*shapes.tooth_height(u)-1


def big(direction, face):
    n = FACES[face]
    right = (0,1,0) if n[0] else (1,0,0)
    up = shapes.cross(n,right)
    phi = atan2(shapes.dot(direction,up), shapes.dot(direction,right))
    angle = BIG_ANGLE + TOOTH_DEPTH*pulse(phi,20)
    return angle-acos(max(-1,min(1,shapes.dot(direction,n))))


def small(direction, edge):
    n = shapes.scale(shapes.add(FACES[edge[0]],FACES[edge[1]]),1/sqrt(2))
    axis = next(i for i in range(3) if n[i] == 0)
    right = tuple(int(i==axis) for i in range(3))
    up = shapes.cross(n,right)
    phi = atan2(shapes.dot(direction,up),shapes.dot(direction,right))
    angle = SMALL_ANGLE + TOOTH_DEPTH*pulse(phi,6,True)
    return angle-acos(max(-1,min(1,shapes.dot(direction,n))))


def field(kind, p, clearance=CLEARANCE):
    d,r = normalize(p)
    enclosure = min(FACE-max(abs(v) for v in p), r-INNER_RADIUS)
    if kind == 'big':
        value = big(d,'U')*r
    elif kind == 'center':
        value = min(big(d,'U'), *[-big(d,f) for f in ['R','L','F','B']])*r
    elif kind == 'corner':
        value = min(big(d,'U'),big(d,'R'),big(d,'F'))*r
    else:
        pinion = min(small(d,'UF')*r, r-PINION_INNER_RADIUS)
        if kind == 'edge':
            value = pinion
        elif kind == 'extra-edge':
            value = min(big(d,'U')*r,big(d,'F')*r,-big(d,'R')*r,-big(d,'L')*r,-pinion)
        else:
            raise ValueError(kind)
    return min(enclosure,value-clearance)


def owner(d, tolerance=0):
    containing = [f for f in FACES if big(d,f) >= -tolerance]
    if len(containing) == 1:
        return 'center/'+containing[0]
    if len(containing) == 3:
        return 'corner/'+''.join(f for f in ['U','D','F','B','R','L'] if f in containing)
    if len(containing) == 2:
        edge = next(e for e in EDGES if set(e) == set(containing))
        return ('edge/' if small(d,edge)>=0 else 'extra-edge/')+edge
    raise ValueError(f'Unexpected big-gear membership: {containing}')


def audit(samples=12000):
    counts = Counter()
    overlaps = []
    golden = pi*(3-sqrt(5))
    for i in range(samples):
        y = 1-2*(i+.5)/samples
        t = sqrt(1-y*y)
        d = (t*cos(golden*i),y,t*sin(golden*i))
        piece = owner(d)
        counts[piece] += 1
        smalls = [e for e in EDGES if small(d,e)>=0]
        for edge in smalls:
            if piece != 'edge/'+edge:
                overlaps.append({'direction':d,'smallGear':edge,'otherPiece':piece})
    crest_counts = {}
    for face,n in FACES.items():
        right = (0,1,0) if n[0] else (1,0,0)
        up = shapes.cross(n,right)
        teeth = Counter()
        for i in range(20):
            phi = (i+.5)*2*pi/20
            theta = BIG_ANGLE+TOOTH_DEPTH-1e-7
            d = shapes.add(shapes.scale(n,cos(theta)),
                           shapes.scale(shapes.add(shapes.scale(right,cos(phi)),
                                                   shapes.scale(up,sin(phi))),sin(theta)))
            teeth[owner(d).split('/')[0]] += 1
        crest_counts[face] = dict(teeth)
    if overlaps:
        raise RuntimeError(f'Raw pinions overlap other pieces: {overlaps[:3]} ({len(overlaps)} samples)')
    # Probe the contact rim separately: uniform directions rarely hit the
    # narrow tangencies where a pinion meets the remaining pair of big gears.
    n=(0,1/sqrt(2),1/sqrt(2));right=(1,0,0);up=shapes.cross(n,right)
    worst=-pi
    for i in range(14400):
        phi=2*pi*i/14400
        theta=SMALL_ANGLE+TOOTH_DEPTH*pulse(phi,6,True)
        d=shapes.add(shapes.scale(n,cos(theta)),
                     shapes.scale(shapes.add(shapes.scale(right,cos(phi)),shapes.scale(up,sin(phi))),sin(theta)))
        worst=max(worst,big(d,'R'),big(d,'L'))
    if worst>1e-12:
        raise RuntimeError(f'Pinion rim enters the third big-gear pair by {worst} radians')
    if len(counts) != 38:
        raise RuntimeError(f'Expected all 38 angular regions, found {len(counts)}')
    if any(c != {'extra-edge':8,'corner':12} for c in crest_counts.values()):
        raise RuntimeError(f'Unexpected face tooth allocation: {crest_counts}')
    return {'directionSamples':samples,'pieceRegionCounts':dict(sorted(counts.items())),
            'rawPinionOverlapSamples':len(overlaps),'bigGearToothAllocation':crest_counts,
            'pinionRimSamples':14400,'maxThirdGearIntrusionRadians':worst,
            'pitchConeDegrees':{'big':BIG_ANGLE*180/pi,'small':SMALL_ANGLE*180/pi},
            'status':'sampled-solved-construction-only'}


BOUNDS = {'big':((-1.6,1.6),(.1,1.6),(-1.6,1.6)),
          'center':((-.7,.7),(.65,1.6),(-.7,.7)),
          'corner':((.15,1.6),(.15,1.6),(.15,1.6)),
          'edge':((-.7,.7),(.65,1.6),(.65,1.6)),
          'extra-edge':((-.7,.7),(.15,1.6),(.15,1.6))}
TETS = [(0,1,2,6),(0,2,3,6),(0,3,7,6),(0,7,4,6),(0,4,5,6),(0,5,1,6)]
CUBE = [(0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1)]


@lru_cache(maxsize=None)
def solid(kind, step=.065):
    """Bounded marching tetrahedra; at most one prototype is built at a time."""
    axes = []
    for low,high in BOUNDS[kind]:
        count = int((high-low)/step)+1
        axes.append([low+(high-low)*i/count for i in range(count+1)])
    nodes,values = [],[]
    for z in axes[2]:
        for y in axes[1]:
            for x in axes[0]:
                p = (x,y,z)
                nodes.append(p)
                values.append(field(kind,p))
    nx,ny,nz = map(len,axes)
    vertices,triangles,edge_vertices = [],[],{}
    def crossing(a,b):
        key = tuple(sorted((a,b)))
        if key in edge_vertices:
            return edge_vertices[key]
        p,q = nodes[a],nodes[b]
        low,high = 0.,1.
        sign = values[a]>=0
        for _ in range(17):
            t = (low+high)/2
            point = tuple(v+(w-v)*t for v,w in zip(p,q))
            if (field(kind,point)>=0) == sign:
                low=t
            else:
                high=t
        t=(low+high)/2
        point=tuple(v+(w-v)*t for v,w in zip(p,q))
        point=tuple(FACE if abs(v-FACE)<1e-6 else -FACE if abs(v+FACE)<1e-6 else v for v in point)
        index=len(vertices)
        vertices.append(point)
        edge_vertices[key]=index
        return index
    for k in range(nz-1):
        for j in range(ny-1):
            for i in range(nx-1):
                cube = [(i+a)+nx*((j+b)+ny*(k+c)) for a,b,c in CUBE]
                if all(values[v]<0 for v in cube) or all(values[v]>=0 for v in cube):
                    continue
                for tet in TETS:
                    inside=[cube[v] for v in tet if values[cube[v]]>=0]
                    outside=[cube[v] for v in tet if values[cube[v]]<0]
                    if not inside or not outside:
                        continue
                    if len(inside)==2:
                        a,b=inside;c,d=outside
                        polygon=[crossing(a,c),crossing(a,d),crossing(b,d),crossing(b,c)]
                    else:
                        singleton,other=(inside,outside) if len(inside)==1 else (outside,inside)
                        polygon=[crossing(singleton[0],v) for v in other]
                    interior=tuple(sum(nodes[v][axis] for v in inside)/len(inside) for axis in range(3))
                    for t in range(1,len(polygon)-1):
                        tri=[polygon[0],polygon[t],polygon[t+1]]
                        a,b,c=[vertices[v] for v in tri]
                        normal=shapes.cross(shapes.add(b,shapes.scale(a,-1)),shapes.add(c,shapes.scale(a,-1)))
                        if shapes.dot(normal,shapes.add(a,shapes.scale(interior,-1)))<0:
                            tri.reverse()
                        triangles.append(tri)
    return shapes.mesh(vertices,triangles)


def port_mesh(body, face):
    axis=next(i for i,v in enumerate(FACES[face]) if v)
    sign=FACES[face][axis]
    vertices,triangles,indices=[],[],{}
    for tri in body['triangles']:
        if any(abs(body['vertices'][i][axis]-sign*FACE)>1e-8 for i in tri):
            continue
        converted=[]
        for i in tri:
            if i not in indices:
                p=list(body['vertices'][i]);p[axis]+=sign*.009
                indices[i]=len(vertices);vertices.append(p)
            converted.append(indices[i])
        triangles.append(converted)
    if not triangles:
        raise RuntimeError(f'No {face} roof patch')
    return shapes.mesh(vertices,triangles)


def mesh_review(body):
    edges=Counter()
    neighbors=defaultdict(set)
    volume=0
    for a,b,c in body['triangles']:
        volume+=shapes.dot(body['vertices'][a],shapes.cross(body['vertices'][b],body['vertices'][c]))/6
        for x,y in [(a,b),(b,c),(c,a)]:
            edges[tuple(sorted((x,y)))]+=1
            neighbors[x].add(y);neighbors[y].add(x)
    remaining=set(neighbors);components=0
    while remaining:
        components+=1;todo=[remaining.pop()]
        while todo:
            for v in neighbors[todo.pop()]:
                if v in remaining:
                    remaining.remove(v);todo.append(v)
    if any(count!=2 for count in edges.values()) or volume<=0:
        raise RuntimeError('Mesh must be a closed, outward-oriented solid')
    if components!=1:
        raise RuntimeError(f'Expected one connected piece, got {components}')
    return {'vertices':len(body['vertices']),'triangles':len(body['triangles']),
            'connectedComponents':components,'signedVolume':volume,'closedEdgeIncidence':2}


def abstract_source(drivers=False):
    source=deepcopy(json.loads((ROOT/'packages/cube3/definition.json').read_text()))
    source.pop('definitionDigest',None)
    source['puzzleId']='gear-compound-'+('drivers' if drivers else 'construction')+'-v1'
    source['ruleModule']='finite-placement-relations@1'
    source['operations']=[]
    source['provenance']={'authoringKind':'gear-compound-construction','modelStatus':'static-geometry-study'}
    if drivers:
        source['pieces']=[p for p in source['pieces'] if p['type']!='corner']
        source['pieceTypes']=[p for p in source['pieceTypes'] if p['id']!='corner']
        source['placementDomains']=[d for d in source['placementDomains'] if d['id']!='corner']
    else:
        extra=deepcopy(next(d for d in source['placementDomains'] if d['id']=='edge'))
        extra['id']='extra-edge'
        for q in extra['placements']:
            q['key']='extra-'+q['key']
            q['footprint']=['extra/'+c for c in q['footprint']]
            for port in q['portAttachment'].values():
                port['cell']='extra/'+port['cell']
        source['placementDomains'].append(extra)
        source['pieceTypes'].append({'id':'extra-edge','placementDomainId':'extra-edge','localPorts':['0','1']})
        for p in list(source['pieces']):
            if p['type']=='edge':
                q=deepcopy(p);q['id']='extra-'+q['id'];q['type']='extra-edge'
                q['homePlacement']='extra-'+q['homePlacement'];source['pieces'].append(q)
        source['cells']+=sorted({c for q in extra['placements'] for c in q['footprint']})
    source['initialState']['placementOf']={p['id']:p['homePlacement'] for p in source['pieces']}
    return source


def compile_definition(source):
    temporary=ROOT/'build/gear-compound-source.json'
    temporary.write_text(json.dumps(source))
    result=json.loads(subprocess.check_output([str(ROOT/'build/native/twisty'),'compile','--definition',str(temporary),'--json'],text=True))
    if result['status']!='Compiled':
        raise RuntimeError(result)
    return result['definition']


def realization(definition, drivers=False):
    assets,bindings={},{}
    for domain in definition['placementDomains']:
        kind=domain['id']
        physical='big' if drivers and kind=='center' else kind
        body=solid(physical,.035 if physical=='extra-edge' else .065)
        parts=[('body','body',None,body)]
        labels={'center':['U'],'corner':['U','R','F'],'edge':['U','F'],'extra-edge':['U','F']}[kind]
        parts += [(f'port-{i}','port',str(i),port_mesh(body,f)) for i,f in enumerate(labels)]
        components=[]
        for name,role,port,asset in parts:
            aid=physical+'/'+name;assets[aid]=asset
            component={'id':name,'role':role,'meshAssetId':aid,'operationTracks':{},'placementFrames':{}}
            if port is not None:
                component['portId']=port
            frame_kind='edge' if kind=='extra-edge' else kind
            for q in domain['placements']:
                component['placementFrames'][q['key']]=frame(definition['symmetry']['members'],frame_kind,q)
            components.append(component)
        bindings[kind]=components
    return {'schemaVersion':1,'id':definition['puzzleId']+'-euclidean',
            'kind':'articulated-euclidean','compatibleDefinitionDigest':definition['definitionDigest'],
            'requiredCapabilities':['triangle-meshes','rigid-transforms'],
            'assets':assets,'componentsByDomain':bindings,'fidelity':'sampled-boolean-construction-study'}


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--write',action='store_true');parser.add_argument('--audit-only',action='store_true');args=parser.parse_args()
    report=audit()
    print('Solved angular regions:',len(report['pieceRegionCounts']))
    print('Face tooth allocation:',report['bigGearToothAllocation'])
    if args.audit_only:
        return
    meshes={}
    for kind in ['center','corner','edge','extra-edge','big']:
        meshes[kind]=solid(kind,.035 if kind=='extra-edge' else .065)
        report.setdefault('prototypeMeshes',{})[kind]=mesh_review(meshes[kind])
        print(kind,report['prototypeMeshes'][kind],flush=True)
    report['parameters']={'outerCubeHalfSize':FACE,'innerRadius':INNER_RADIUS,
                          'pinionInnerRadius':PINION_INNER_RADIUS,'toothDepthDegrees':TOOTH_DEPTH*180/pi,
                          'displayClearance':CLEARANCE,
                          'gridStepTarget':{'default':.065,'extra-edge':.035}}
    documents={'review.json':report}
    for drivers in [False,True]:
        prefix='drivers' if drivers else 'gear-compound'
        source=abstract_source(drivers);definition=compile_definition(source)
        documents[prefix+'-source.json']=source
        documents[prefix+'-definition.json']=definition
        documents[prefix+'-euclidean.json']=realization(definition,drivers)
    if args.write:
        OUT.mkdir(parents=True,exist_ok=True)
    for filename,document in documents.items():
        path=OUT/filename
        if args.write:
            path.write_text(json.dumps(document,separators=(',',':'),sort_keys=True)+'\n')
        elif not path.exists() or not same_geometry(json.loads(path.read_text()),json.loads(json.dumps(document))):
            raise RuntimeError(f'{path.relative_to(ROOT)} is stale')


if __name__=='__main__':
    main()
