#!/usr/bin/env python3
"""Author the closed 110-piece spherical/conical icosahedron package."""
from ..environment import workspace_root, native_tool
import argparse
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess
import tempfile

from .icosahedron_geometry import AXES, dot
from .icosahedron_model import Model, LABELS, ALPHA, STOPS, DELTAS, TAU, compose, rotation
from .icosahedron_meshes import assets, DISPLAY_RADIUS

ROOT = workspace_root()
ID='icosahedron-110'


def ref(name):return {'ref':name}
def at(piece,kind,qs):return {'at':dict(piece=piece,placements=[kind+'/p'+str(q) for q in sorted(qs)])}
def location(kind,i):return 'location/'+kind+'/'+str(i)


def names(model):
    result={}
    for axis in range(20):
        extra=0
        for d,delta in enumerate(DELTAS):
            if delta<0:continue
            suffix='' if abs(delta-TAU/3)<1e-8 else '_j' if abs(delta-ALPHA)<1e-8 else '_'+chr(ord('a')+extra)
            if suffix not in ('','_j'):extra+=1
            result[axis,d]=LABELS[axis]+suffix
            inverse=next(i for i,t in enumerate(DELTAS) if abs(t+delta)<1e-8)
            result[axis,inverse]=result[axis,d]+'_inv'
    return result


def color(index):
    # Distinct, stable face colors, carried by ordinary opaque port labels.
    palette=['#f3eeee','#e97070','#7bd7ae','#efd16d','#81aef1','#d494da','#eaa675','#7ad1d7',
             '#b8d874','#9b9bea','#e18eac','#8ec7a6','#e0b96f','#90bde1','#c3a3dc','#b3cf8e',
             '#e5a090','#80c6c0','#d0bded','#d9cc8c']
    return palette[index]


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
            predicates['occupied/'+path]={'occupiedBy':dict(placement=path,pieces=[p['id'] for p in pieces])}
        for i,qs in enumerate(model.locations[kind]):
            resource=location(kind,i);catalog['resources'].append(resource)
            predicates[resource]={'any':[ref('occupied/'+kind+'/p'+str(q)) for q in qs]}
            invariant.append({'count':dict(min=0,max=1,of=[at(p['id'],kind,qs) for p in pieces])})
    for a,b in model.conflicts:
        invariant.append({'count':dict(min=0,max=1,of=[ref(location(*a)),ref(location(*b))])})
    for axis,p in enumerate(model.centers):
        invariant.append(at(p['id'],'center',model.driver_placements[axis]))
    for qs in model.edge_slots:
        invariant.append({'count':dict(min=1,max=1,of=[at(p['id'],'edge',qs) for p in members['edge']])})
    source['invariant']={'all':invariant}
    for axis in range(20):
        forbidden=[ref('occupied/'+kind+'/p'+str(q)) for kind in model.kinds
                   for q,roles in enumerate(model.roles[kind]) if roles[axis]==2]
        predicates['free/'+LABELS[axis]]={'not':{'any':forbidden}}
        for d,delta in enumerate(DELTAS):
            name=ids[axis,d]
            phase=at(model.centers[axis]['id'],'center',[model.driver_placements[axis][s] for s,t in model.pairs[d]])
            predicates['phase/'+name]=phase
            missing=[ref('occupied/'+kind+'/p'+str(q)) for kind in model.kinds
                     for q,roles in enumerate(model.roles[kind]) if roles[axis]==1 and q not in model.maps[axis,d][kind]]
            predicates['mapped/'+name]={'not':{'any':missing}}
            choices={kind:name for kind in model.kinds}
            for domain in source['domains']:
                transport={'p'+str(a):'p'+str(b) for a,b in sorted(model.maps[axis,d][domain['id']].items())}
                domain['moves'].append(dict(id=name,event=name,support=list(transport),transport=transport))
            source['requests'].append(dict(id=name,relation={'all':[ref('phase/'+name),ref('free/'+LABELS[axis]),ref('mapped/'+name)]+
                [{'chosen':dict(domain=kind,moves=[name])} for kind in model.kinds]},
                profiles=[dict(id='turn',choices=choices,when=ref('phase/'+name))],defaultProfile='turn'))
            inverse=ids[axis,next(i for i,t in enumerate(DELTAS) if abs(t+delta)<1e-8)]
            catalog['requests'][name]=dict(inverse=inverse,family=LABELS[axis],event=name,profile='turn')
    source['goal']={'all':[at(p['id'],p['kind'],[model.index('center',compose(rotation(AXES[p['caps'][0]],t),p['frame'])) for t in (0.,TAU/3,2*TAU/3)]
                            if p['kind']=='center' else [p['home']]) for p in model.pieces]}
    controls=dict(version=1,drivers=[],families=[])
    for axis,p in enumerate(model.centers):
        family=LABELS[axis]
        controls['drivers'].append(dict(id=family,piece=p['id'],stops=[str(i) for i in range(9)],
            placements={'p'+str(q):str(i) for i,q in enumerate(model.driver_placements[axis])}))
        controls['families'].append(dict(id=family,label=family,drivers=[family],routes=[dict(
            **{'from':[str(s)],'to':[str(t)]},request=ids[axis,d]) for d in range(len(DELTAS)) for s,t in model.pairs[d]]))
    catalog['controls']=controls
    from .compact_transports import compress
    phases={LABELS[axis]:dict(count=9,pairs={ids[axis,d]:model.pairs[d] for d in range(len(DELTAS))}) for axis in range(20)}
    stats=compress(source,catalog,phases)
    return source,catalog,stats


def frame(r):
    # Reflect the author's z-up coordinates to the application's y-up frame.
    order=(0,2,1)
    q=[[r[i][j] for j in order] for i in order]
    return [value for col in range(3) for value in (*[q[row][col] for row in range(3)],0.)]+[0.,0.,0.,1.]


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
            coordinates={str(s):-t for s,t in enumerate(STOPS)}) for i,n in enumerate(AXES)}),
        controlLayout=dict(version=1,drivers={LABELS[i]:dict(angles=list(STOPS)) for i in range(20)},
            paths={name:{LABELS[i]:DELTAS[d]} for (i,d),name in ids.items()}))
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
                    fidelity='spherical-110-piece-disk-arrangement' if spherical else 'regular-icosahedron-with-conical-cuts')
        if spherical:data.update(radius=DISPLAY_RADIUS,diskAngleDegrees=math.degrees(math.acos(math.sqrt((5+2*math.sqrt(5))/15))))
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
        result=json.loads(subprocess.check_output([str(native_tool('twisty')),'validate','--definition',str(path),'--json'],text=True))
        assert result['status']=='Valid',result
        digest=result['definitionDigest']
    documents.update(realizations(model,ids,digest,meshes))
    documents['manifest.json']=dict(puzzleId=ID+'-v1',definitionDigest=digest,pieces=110,
        placements={kind:len(qs) for kind,qs in model.poses.items()},requests=len(ids),
        diskAngleDegrees=math.degrees(math.acos(math.sqrt((5+2*math.sqrt(5))/15))),
        rays=AXES,angularStopsDegrees=[math.degrees(t) for t in STOPS],
        turnAnglesDegrees={LABELS[i]:120 for i in range(20)},
        euclideanCut='conical',euclideanShape='regular-icosahedron',transportCompression=stats)
    directory=ROOT/'packages'/ID
    if args.write:directory.mkdir(parents=True,exist_ok=True)
    for filename,document in documents.items():
        path=directory/filename;text=json.dumps(document,separators=(',',':'),sort_keys=True)+'\n'
        if args.write:path.write_text(text)
        elif not path.exists() or path.read_text()!=text:raise SystemExit(f'{path.relative_to(ROOT)} is stale; run npm run build:puzzles.')
    print('Icosahedron 110: 20 centers, 30 edges, 60 petals; 1080 shared poses, 280 requests; spherical and conical Euclidean views',flush=True)


if __name__=='__main__':main()
