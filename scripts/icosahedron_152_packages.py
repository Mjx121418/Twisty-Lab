#!/usr/bin/env python3
"""Author the 152-piece spherical/planar icosahedron package."""
import argparse
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess
import tempfile

from icosahedron_geometry import AXES, dot
from icosahedron_152_model import Model, LABELS, STOPS, DELTAS, TAU
from icosahedron_packages import names, color, frame
from icosahedron_152_meshes import assets, DISPLAY_RADIUS, INRADIUS, CUT_OFFSET

ROOT=Path(__file__).resolve().parent.parent
ID='icosahedron-152'


def ref(name):return {'ref':name}
def at(piece,kind,qs):return {'at':dict(piece=piece,placements=[kind+'/p'+str(q) for q in sorted(qs)])}
def location(kind,i):return 'location/'+kind+'/'+str(i)


def module(model,ids,ports):
    source=dict(kind='relational-puzzle',languageVersion=1,id=ID+'-v1',domains=[],predicates={},requests=[])
    catalog=dict(kind='relational-port-bindings',version=1,puzzleId=source['id'],resources=[],domains=[],pieces={},placements={},requests={})
    members={kind:[p for p in model.pieces if p['kind']==kind] for kind in model.kinds}
    predicates=source['predicates'];invariant=[]
    for kind in model.kinds:
        pieces=members[kind]
        source['domains'].append(dict(id=kind,placements=['p'+str(q) for q in range(len(model.poses[kind]))],
            pieces=[dict(id=p['id'],initial='p'+str(p['home'])) for p in pieces],moves=[]))
        catalog['domains'].append(dict(id=kind,pieces=[p['id'] for p in pieces],localPorts=ports[kind],
            placements=[kind+'/p'+str(q) for q in range(len(model.poses[kind]))]))
        for p in pieces:
            labels={}
            for port in ports[kind]:
                n=AXES[int(port)]
                transformed=tuple(sum(p['frame'][i][j]*n[j] for j in range(3)) for i in range(3))
                labels[port]=color(max(range(20),key=lambda i:dot(AXES[i],transformed)))
            catalog['pieces'][p['id']]=dict(domain=kind,home=kind+'/p'+str(p['home']),portLabels=labels)
        for q in range(len(model.poses[kind])):
            path=kind+'/p'+str(q);resource=location(kind,model.location_of[kind][q])
            catalog['placements'][path]=dict(resources=[resource],ports={port:dict(cell=resource,attachment=path+'/'+port) for port in ports[kind]})
        for i,qs in enumerate(model.locations[kind]):
            resource=location(kind,i);catalog['resources'].append(resource)
            terms=[at(p['id'],kind,qs) for p in pieces]
            if len(qs)==1:
                predicates[resource]={'occupiedBy':dict(placement=kind+'/p'+str(qs[0]),pieces=[p['id'] for p in pieces])}
            else:
                for j,term in enumerate(terms):
                    atom='m'+str(len(predicates));predicates[atom]=term;terms[j]=ref(atom)
                predicates[resource]={'any':terms}
            invariant.append({'count':dict(min=0,max=1,of=terms)})
    neighbors={}
    for a,b in model.conflicts: neighbors.setdefault(a,[]).append(b)
    for a,others in neighbors.items():
        invariant.append({'not':{'all':[ref(location(*a)),{'any':[ref(location(*b)) for b in others]}]}})
    source['invariant']={'all':invariant}
    for axis in range(20):
        forbidden=[ref(location(kind,i)) for kind in model.kinds
                   for i,qs in enumerate(model.locations[kind]) if model.roles[kind][qs[0]][axis]==2]
        predicates['free/'+LABELS[axis]]={'not':{'any':forbidden}}
        for d,delta in enumerate(DELTAS):
            name=ids[axis,d]
            missing=[]
            for kind in model.kinds:
                for i,qs in enumerate(model.locations[kind]):
                    statuses={q in model.maps[axis,d][kind] for q in qs}
                    assert len(statuses)==1, 'A geometric location has inconsistent transport coverage'
                    if model.roles[kind][qs[0]][axis]==1 and not next(iter(statuses)):missing.append(ref(location(kind,i)))
            predicates['mapped/'+name]={'not':{'any':missing}}
            choices={kind:name for kind in model.kinds}
            for domain in source['domains']:
                transport={'p'+str(a):'p'+str(b) for a,b in sorted(model.maps[axis,d][domain['id']].items())}
                domain['moves'].append(dict(id=name,event=name,support=list(transport),transport=transport))
            source['requests'].append(dict(id=name,relation={'all':[ref('free/'+LABELS[axis]),ref('mapped/'+name)]+
                [{'chosen':dict(domain=kind,moves=[name])} for kind in model.kinds]},
                profiles=[dict(id='turn',choices=choices)],defaultProfile='turn'))
            inverse=ids[axis,next(i for i,t in enumerate(DELTAS) if abs(t+delta)<1e-8)]
            catalog['requests'][name]=dict(inverse=inverse,family=LABELS[axis],event=name,profile='turn')
    source['goal']={'all':[at(p['id'],p['kind'],model.locations[p['kind']][model.location_of[p['kind']][p['home']]] if p['kind']=='face' else [p['home']]) for p in model.pieces]}
    from compact_transports import compress
    phases={LABELS[axis]:dict(count=9,pairs={ids[axis,d]:model.pairs[d] for d in range(len(DELTAS))}) for axis in range(20)}
    stats=compress(source,catalog,phases)
    return source,catalog,stats


def realizations(model,ids,digest,meshes):
    tracks={}
    for (axis,d),name in ids.items():
        s,t=model.pairs[d][0]
        tracks[name]=dict(family=LABELS[axis],**{'from':str(s),'to':str(t)},
            winding=round((-DELTAS[d]+STOPS[t]-STOPS[s])/TAU))
    common=dict(schemaVersion=1,compatibleDefinitionDigest=digest,
        requiredCapabilities=['triangle-meshes','rigid-transforms','shared-rotation-tracks','family-rotation-generators','transport-path-selectors'],
        rotationTracks=tracks,operationTrackTables={'turns':{name:dict(kind='transport-regions',regions=[dict(request=name,path=name)]) for name in ids.values()}},
        motionFamilies=dict(version=1,families={LABELS[i]:dict(axis=[n[0],n[2],n[1]],pivot=[0,0,0],space='world',
            coordinates={str(s):-t for s,t in enumerate(STOPS)}) for i,n in enumerate(AXES)}))
    result={}
    for spherical in (False,True):
        data=deepcopy(common);data['assets']={};data['componentsByDomain']={}
        for kind in model.kinds:
            body,ports=meshes[spherical][kind]
            frames={'p'+str(q):frame(r) for q,r in enumerate(model.poses[kind])}
            components=[]
            for port,asset in [('body',body),*ports.items()]:
                name=kind+'/'+port;data['assets'][name]=asset
                component=dict(id=port,role='body' if port=='body' else 'port',meshAssetId=name,
                    placementFrames=frames,operationTracks='turns')
                if port!='body':component['portId']=port
                components.append(component)
            data['componentsByDomain'][kind]=components
        suffix='spherical' if spherical else 'euclidean'
        data.update(id=ID+'-'+suffix+'-v1',kind='articulated-'+suffix,
                    fidelity='spherical-152-piece-disk-arrangement' if spherical else 'regular-icosahedron-with-planar-cuts')
        if spherical:data.update(radius=DISPLAY_RADIUS,diskAngleDegrees=math.degrees(math.acos(1/math.sqrt(3))))
        result[ID+'-'+suffix+'.json']=data
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--write',action='store_true');args=parser.parse_args()
    model=Model();ids=names(model)
    meshes={spherical:{kind:assets(model.prototypes[kind],spherical) for kind in model.kinds} for spherical in (False,True)}
    ports={kind:list(meshes[True][kind][1]) for kind in model.kinds}
    source,catalog,stats=module(model,ids,ports)
    package=dict(kind='relational-package',formatVersion=source['languageVersion'],source=source,catalog=catalog)
    documents={'puzzle.pzl.json':source,'bindings.json':catalog,'package.json':package}
    with tempfile.TemporaryDirectory(prefix='twisty-icosahedron-') as temporary:
        path=Path(temporary)/'package.json';path.write_text(json.dumps(package,separators=(',',':')))
        result=json.loads(subprocess.check_output([str(ROOT/'build/native/twisty'),'validate','--definition',str(path),'--json'],text=True))
        assert result['status']=='Valid',result
        digest=result['definitionDigest']
    documents.update(realizations(model,ids,digest,meshes))
    documents['manifest.json']=dict(puzzleId=ID+'-v1',definitionDigest=digest,pieces=152,
        placements={kind:len(qs) for kind,qs in model.poses.items()},requests=len(ids),
        diskAngleDegrees=math.degrees(math.acos(1/math.sqrt(3))),
        rays=AXES,relativeTurnAnglesDegrees=[math.degrees(t) for t in DELTAS],
        poseFamilies={k:len(model.poses[k])//60 for k in model.kinds},
        bodyLocations={k:len(model.locations[k]) for k in model.kinds},conflictPairs=len(model.conflicts),
        turnAnglesDegrees={LABELS[i]:120 for i in range(20)},
        euclideanCut='planar',euclideanCutOffset=CUT_OFFSET,euclideanInradius=INRADIUS,rules='spherical',euclideanShape='regular-icosahedron',transportCompression=stats)
    directory=ROOT/'packages'/ID
    if args.write:directory.mkdir(parents=True,exist_ok=True)
    for filename,document in documents.items():
        path=directory/filename;text=json.dumps(document,separators=(',',':'),sort_keys=True)+'\n'
        if args.write:path.write_text(text)
        elif not path.exists() or path.read_text()!=text:raise SystemExit(f'{path.relative_to(ROOT)} is stale; run npm run build:puzzles.')
    print('Icosahedron 152: 20 faces, 12 corners, 60 wings, 60 petals; 2880 shared poses, 280 requests; spherical and planar Euclidean views',flush=True)


if __name__=='__main__':main()
