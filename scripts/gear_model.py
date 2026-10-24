#!/usr/bin/env python3
"""Author Gear placement/rotation templates and the shared phase diagrams."""
import argparse
import json
import subprocess
from math import sin, cos, pi, sqrt, isclose
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FACES = ['U', 'R', 'F', 'D', 'L', 'B']
VECTORS = [(0,1,0),(1,0,0),(0,0,1),(0,-1,0),(-1,0,0),(0,0,-1)]
N = (0,1/sqrt(2),1/sqrt(2))

def load_definition(name):
    """Use the current authoring factory for frames and exact local transports."""
    result = json.loads(subprocess.check_output([
        str(ROOT/'build/native/twisty'), 'compile', '--definition',
        str(ROOT/'packages'/name/'source.json'), '--json'], text=True))
    if result['status'] != 'Compiled':
        raise RuntimeError(result)
    return result['definition']

def store_realization(path, result, write):
    # Preserve checked-in bytes when platform libm rounding is immaterial.
    expected = json.loads(json.dumps(result))
    if path.exists() and same_geometry(json.loads(path.read_text()), expected):
        return
    if not write:
        raise RuntimeError(f'{path.relative_to(ROOT)} is stale; regenerate with --write')
    path.write_text(json.dumps(result, separators=(',', ':'), sort_keys=True)+'\n')

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

def realization_templates(definition,diagram=False):
    # Six-tooth cog: (20 / 6) × (π / 2) = 5 × (π / 3), or 300 degrees.
    # Solid templates acquire their supplied meshes in gear_normal.py.
    assets={};bindings={};group=definition['symmetry']['members']
    from compact_transports import expanded_source
    moves = {d['id']: {m['id']: m for m in d['moves']} for d in expanded_source(definition['source'])['domains']}
    for domain in definition['placementDomains']:
        kind=domain['id'];ports=len(domain['placements'][0]['portAttachment']);parts=[]
        if not diagram:
            if kind=='gear-edge':
                parts.extend([('carrier','body',None),('cog','body',None)])
            else: parts.append(('body','body',None))
        for port in range(ports):
            parts.append((f'port-{port}','port',str(port)))
        configs=[]
        for name,role,port in parts:
            aid=kind+'/'+name
            if diagram:
                assets[aid]={'vertices':[(-.16,-.12,0),(.20,0,0),(-.16,.12,0)],'triangles':[[0,1,2]]}
            data={'id':name,'role':role,'meshAssetId':aid,'placementFrames':{},'operationTracks':{}}
            if port is not None: data['portId']=port
            spinning=kind=='gear-edge' and (name=='cog' or role=='port')
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
                    m=rotation((0,0,1),phase*pi/3*5 if spinning else 0)
                    m[12:15]=[cx+dot(position,right),cy+dot(position,up),0]
                elif spinning: m=compose(m,rotation(N,phase*pi/3*5))
                data['placementFrames'][q['key']]=m
            if kind=='center' and not diagram:
                data['symmetries']=[rotation((0,1,0),i*pi/2) for i in range(1,4)]
            for op in definition['operations']:
                paths={};face=op['family'];reversed=op['id'].endswith("'")
                for q in domain['placements']:
                    target=moves[kind][op['id']]['transport'].get(q['key'])
                    if target is None: continue
                    outer=face in q['footprint'][0]
                    opposite=FACES[(FACES.index(face)+3)%6] in q['footprint'][0];steps=[]
                    if not diagram and (outer or opposite):
                        steps.append({'axis':VECTORS[FACES.index(face)],'pivot':[0,0,0],
                                      'angle':(1 if reversed else -1)*pi/2*(1 if outer else -1),'space':'world'})
                    if spinning:
                        delta=(int(target[-1])-int(q['key'][-1])+3)%6-3
                        if delta:
                            steps.append({'axis':[0,0,1] if diagram else N,'pivot':[0,0,0],
                                          'angle':delta*pi/3*5,'space':'local'})
                    paths[q['key']]=steps
                data['operationTracks'][op['id']]=paths
            configs.append(data)
        bindings[kind]=configs
    return {'schemaVersion':1,'id':definition['puzzleId']+('-diagram-v1' if diagram else '-euclidean-v1'),
            'kind':'articulated-port-diagram' if diagram else 'articulated-euclidean',
            'compatibleDefinitionDigest':definition['definitionDigest'],
            'requiredCapabilities':['triangle-meshes','rigid-transforms'],
            'assets':assets,'componentsByDomain':bindings,'fidelity':'schematic-port-diagram' if diagram else 'supplied-low-poly-gear-cube'}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--write',action='store_true');args=parser.parse_args()
    for name in ['gear','gear-extreme','gear-plain']:
        directory=ROOT/'packages'/name
        definition=load_definition(name)
        path=directory/(name+'-port-diagram.json')
        store_realization(path, realization_templates(definition,True), args.write)
        print(f'{path.relative_to(ROOT)}: six-phase diagram with 300-degree cog tracks')
if __name__=='__main__': main()
