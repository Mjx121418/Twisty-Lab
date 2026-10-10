#!/usr/bin/env python3
"""Bounded counterexample audit of reusing the existing cube's S2 regions.

This rejects that candidate, not all possible spherical gear mechanisms.
"""
import argparse
import json
from math import cos, sin, sqrt, pi
from pathlib import Path
from gear_model import FACES, VECTORS, add, mul, dot, cross, frame, same_geometry

ROOT=Path(__file__).resolve().parent.parent

def rotate(p,n,a): return add(add(mul(p,cos(a)),mul(cross(n,p),sin(a))),mul(n,dot(n,p)*(1-cos(a))))
def membership(p,c): return frozenset(FACES[i] for i,n in enumerate(VECTORS) if dot(p,n)>c)
def audit(count=4096):
    definition=json.loads((ROOT/'packages/gear/definition.json').read_text())
    domains={d['id']:d for d in definition['placementDomains']}
    c=cos(70*pi/180);shapes=[]
    for piece in definition['pieces']:
        q=next(q for q in domains[piece['type']]['placements'] if q['key']==piece['homePlacement'])
        slot=q['footprint'][0];root=frame(definition['symmetry']['members'],piece['type'],q)
        radial=(0,0,0)
        for face in slot: radial=add(radial,VECTORS[FACES.index(face)])
        radial=mul(radial,1/sqrt(dot(radial,radial)))
        outer='R' in slot;middle='R' not in slot and 'L' not in slot
        orbit=-pi if outer else -pi/2 if middle else 0
        spin=(1 if root[0]>0 else -1)*pi/3 if middle and piece['type']=='gear-edge' else 0
        shapes.append((piece['id'],frozenset(slot),radial,orbit,spin))
    results=[]
    for t in [0,.25,.5,.75,1]:
        gaps=overlaps=0;example=None
        for i in range(count):
            z=1-2*(i+.5)/count;a=i*pi*(3-sqrt(5));r=sqrt(1-z*z)
            p=(r*cos(a),r*sin(a),z);owners=[]
            for id,region,n,orbit,spin in shapes:
                local=rotate(rotate(p,(1,0,0),-orbit*t),n,-spin*t)
                if membership(local,c)==region: owners.append(id)
            gaps+=not owners;overlaps+=len(owners)>1
            if example is None and len(owners)>1: example={'point':p,'owners':owners}
        results.append({'progress':t,'uncoveredSamples':gaps,'overlappingSamples':overlaps,'overlapWitness':example})
    assert results[0]['uncoveredSamples']==results[0]['overlappingSamples']==0
    assert any(r['overlappingSamples'] for r in results[1:])
    return {'candidate':'existing-70-degree-cube-regions-with-geared-edge-spin','sampleCount':count,
            'status':'candidate-rejected','scope':'Does not prove impossibility of a different spherical construction.',
            'frames':results}
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--write',action='store_true');args=parser.parse_args()
    result=audit();path=ROOT/'packages/gear/spherical-audit.json'
    if args.write: path.write_text(json.dumps(result,indent=2)+'\n')
    elif not same_geometry(json.loads(path.read_text()),json.loads(json.dumps(result))): raise SystemExit('Spherical audit is stale')
    for row in result['frames']: print(f"t={row['progress']}: gaps={row['uncoveredSamples']}, overlaps={row['overlappingSamples']}")
