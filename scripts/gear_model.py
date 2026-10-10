#!/usr/bin/env python3
"""Author separate gear meshes and tracks; the native compiler owns exact rules."""
import argparse
import json
from math import sin, cos, pi, sqrt, isclose
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
FACES = ['U', 'R', 'F', 'D', 'L', 'B']
VECTORS = [(0,1,0),(1,0,0),(0,0,1),(0,-1,0),(-1,0,0),(0,0,-1)]
EDGES = ['UF','UR','UB','UL','DF','DR','DB','DL','FR','FL','BR','BL']
N = (0,1/sqrt(2),1/sqrt(2))
X = (1,0,0)
T = (0,1/sqrt(2),-1/sqrt(2))

def same_geometry(actual, expected):
    """Only geometric floating values tolerate platform libm rounding."""
    if isinstance(actual,float) and isinstance(expected,float):
        return isclose(actual,expected,rel_tol=1e-12,abs_tol=1e-12)
    if isinstance(actual,dict) and isinstance(expected,dict):
        return actual.keys()==expected.keys() and all(same_geometry(actual[k],expected[k]) for k in actual)
    if isinstance(actual,list) and isinstance(expected,list):
        return len(actual)==len(expected) and all(same_geometry(a,b) for a,b in zip(actual,expected))
    return type(actual)==type(expected) and actual==expected

def add(a,b): return tuple(x+y for x,y in zip(a,b))
def mul(a,s): return tuple(x*s for x in a)
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def ident(): return [1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
def compose(a,b): return [sum(a[k*4+r]*b[c*4+k] for k in range(4)) for c in range(4) for r in range(4)]
def rotation(axis,theta):
    out=ident()
    for c in range(3):
        v=tuple(int(i==c) for i in range(3))
        w=add(add(mul(v,cos(theta)),mul(cross(axis,v),sin(theta))),mul(axis,dot(axis,v)*(1-cos(theta))))
        out[c*4:c*4+3]=w
    return out

def frame(group,kind,q):
    prototype={'corner':['U','R','F'],'edge':['U','F'],'gear-edge':['U','F'],'center':['U']}[kind]
    labels=[q['portAttachment'][str(i)]['attachment'].split('/')[0] for i in range(len(prototype))]
    p=next(p for p in group if all(FACES[p[FACES.index(a)]]==b for a,b in zip(prototype,labels)))
    out=ident()
    for c,f in enumerate(['R','U','F']): out[c*4:c*4+3]=VECTORS[p[FACES.index(f)]]
    return out

def mesh(vertices,triangles): return {'vertices':vertices,'triangles':triangles}
def box(center,half):
    vs=[add(center,(half[0]*x,half[1]*y,half[2]*z)) for x,y,z in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
    return mesh(vs,[[0,2,1],[0,3,2],[4,5,6],[4,6,7],[0,1,5],[0,5,4],[3,7,6],[3,6,2],[0,4,7],[0,7,3],[1,2,6],[1,6,5]])
def sticker(center,normal,size=.26):
    right=(0,0,-1) if normal==(1,0,0) else (1,0,0)
    up=cross(normal,right)
    return mesh([add(center,add(mul(right,size*x),mul(up,size*y))) for x,y in [(-1,-1),(1,-1),(1,1),(-1,1)]],[[0,1,2],[0,2,3]])
def cog():
    vs=[];count=48;center=(0,1.10,1.10)
    for level in [-.045,.045]:
        for i in range(count):
            r=.39 if i%4 in (1,2) else .335
            a=2*pi*i/count
            vs.append(add(add(center,mul(N,level)),add(mul(X,r*cos(a)),mul(T,r*sin(a)))))
    vs.extend([add(center,mul(N,-.045)),add(center,mul(N,.045))])
    triangles=[]
    for i in range(count):
        j=(i+1)%count
        triangles.extend([[96,j,i],[97,i+count,j+count],[i,j,j+count],[i,j+count,i+count]])
    return mesh(vs,[[a,c,b] for a,b,c in triangles])
def cog_port(port):
    center=add((0,1.10,1.10),mul(N,.049));start=port*pi+.035;end=(port+1)*pi-.035
    vs=[center]+[add(center,add(mul(X,.30*cos(a)),mul(T,.30*sin(a)))) for a in [start+(end-start)*i/24 for i in range(25)]]
    return mesh(vs,[[0,i+1,i] for i in range(1,25)]) # X cross T points opposite N

def realization(definition,diagram):
    assets={};bindings={};group=definition['symmetry']['members']
    for domain in definition['placementDomains']:
        kind=domain['id'];ports=len(domain['placements'][0]['portAttachment']);parts=[]
        centers={'corner':(1,1,1),'edge':(0,1,1),'center':(0,1,0),'gear-edge':(0,1,1)}
        center=centers[kind]
        if not diagram:
            carrier_center=(0,.89,.89) if kind=='gear-edge' else center
            half=(.23,.23,.23) if kind=='gear-edge' else (.27,.27,.27)
            parts.append(('carrier' if kind=='gear-edge' else 'body','body',None,box(carrier_center,half)))
            if kind=='gear-edge': parts.append(('cog','body',None,cog()))
        for port in range(ports):
            if diagram:
                asset=mesh([(-.16,-.12,0),(.20,0,0),(-.16,.12,0)],[[0,1,2]])
            elif kind=='gear-edge': asset=cog_port(port)
            else:
                f={'corner':['U','R','F'],'edge':['U','F'],'center':['U']}[kind][port]
                normal=VECTORS[FACES.index(f)];asset=sticker(add(center,mul(normal,.275)),normal)
            parts.append((f'port-{port}','port',str(port),asset))
        configs=[]
        for name,role,port,asset in parts:
            aid=kind+'/'+name;assets[aid]=asset
            data={'id':name,'role':role,'meshAssetId':aid,'placementFrames':{},'operationTracks':{}}
            if port is not None: data['portId']=port
            spinning=kind=='gear-edge' and name!='carrier'
            for q in domain['placements']:
                phase=int(q['key'][-1]) if kind=='gear-edge' else 0
                m=frame(group,kind,q)
                if diagram:
                    face=q['portAttachment'][port]['attachment'].split('/')[0]
                    slot=q['footprint'][0];position=(0,0,0)
                    for f in slot: position=add(position,VECTORS[FACES.index(f)])
                    n=VECTORS[FACES.index(face)]
                    right={'R':(0,0,-1),'L':(0,0,1),'B':(-1,0,0)}.get(face,(1,0,0));up=cross(n,right)
                    cx,cy={'U':(0,3),'R':(3,0),'F':(0,0),'D':(0,-3),'L':(-3,0),'B':(6,0)}[face]
                    m=rotation((0,0,1),phase*pi/3 if spinning else 0)
                    m[12:15]=[cx+dot(position,right),cy+dot(position,up),0]
                elif spinning: m=compose(m,rotation(N,phase*pi/3))
                data['placementFrames'][q['key']]=m
            if kind=='center' and not diagram:
                data['symmetries']=[rotation((0,1,0),i*pi/2) for i in range(1,4)]
            for op in definition['operations']:
                paths={};ordinary='q' in op['id'];face=op['family'];reversed=op['id'].endswith("'")
                for q in domain['placements']:
                    target=op['placementRules'][kind]['transports'].get(q['key'])
                    if target is None: continue
                    outer=face in q['footprint'][0];steps=[]
                    if not diagram:
                        steps.append({'axis':VECTORS[FACES.index(face)],'pivot':[0,0,0],
                                      'angle':(1 if reversed else -1)*pi/2*(2 if outer and not ordinary else 1),'space':'world'})
                    if spinning:
                        delta=(int(target[-1])-int(q['key'][-1])+3)%6-3
                        if delta:
                            steps.append({'axis':[0,0,1] if diagram else N,'pivot':[0,0,0],
                                          'angle':delta*pi/3,'space':'local'})
                    paths[q['key']]=steps
                data['operationTracks'][op['id']]=paths
            configs.append(data)
        bindings[kind]=configs
    return {'schemaVersion':1,'id':definition['puzzleId']+('-diagram-v1' if diagram else '-euclidean-v1'),
            'kind':'articulated-port-diagram' if diagram else 'articulated-euclidean',
            'compatibleDefinitionDigest':definition['definitionDigest'],
            'requiredCapabilities':['triangle-meshes','rigid-transforms'],
            'assets':assets,'componentsByDomain':bindings,'fidelity':'schematic-port-diagram' if diagram else 'idealized-geared-kinematics'}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--write',action='store_true');args=parser.parse_args()
    for name,geared in [('gear',EDGES),('gear-extreme',[e for e in EDGES if e not in ['FR','FL','BR','BL']]),('gear-plain',[])]:
        directory=ROOT/'packages'/name
        source={'schemaVersion':1,'semanticsVersion':'1.0.0','kind':'gear-cube-prototypes',
                'puzzleId':name+'-experimental-v1','gearedEdges':geared}
        # CLI takes a file; an explicit write creates source files before compilation.
        if args.write:
            directory.mkdir(parents=True,exist_ok=True)
            (directory/'source.json').write_text(json.dumps(source,indent=2)+'\n')
        result=json.loads(subprocess.check_output([str(ROOT/'build/native/twisty'),'compile','--definition',str(directory/'source.json'),'--json'],text=True))
        if result['status']!='Compiled': raise RuntimeError(result)
        definition=result['definition']
        documents={'source.json':source,'definition.json':definition,
                   'manifest.json':{'puzzleId':source['puzzleId'],'definitionDigest':definition['definitionDigest']},
                   name+'-euclidean.json':realization(definition,False),name+'-port-diagram.json':realization(definition,True)}
        for filename,data in documents.items():
            path=directory/filename
            if args.write: path.write_text(json.dumps(data,separators=(',',':'),sort_keys=True)+'\n')
            else:
                actual=json.loads(path.read_text());expected=json.loads(json.dumps(data))
                same=same_geometry(actual,expected) if 'componentsByDomain' in data else actual==expected
                if not same: raise RuntimeError(f'{path.relative_to(ROOT)} is stale')
        print(name+': '+definition['definitionDigest'])
if __name__=='__main__': main()
