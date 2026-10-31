#!/usr/bin/env python3
"""Generate the exact 80-piece spherical 8-cell and its S³ realization."""
from ..environment import workspace_root, native_tool
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from .tesseract_model import Model, ID, DEPTH, CELLS, PALETTE, frame, label
from .tesseract_meshes import assets, INSET
from .compact_transports import compress

ROOT = workspace_root()


def slot(pose): return 'slot/'+''.join(label(c) for c in sorted(pose, key=abs))
def at(piece, domain, qs): return {'at':dict(piece=piece,placements=[domain+'/p'+str(q) for q in qs])}


def documents(model):
    source = dict(kind='relational-puzzle', languageVersion=1, id=ID+'-v1', domains=[], predicates={}, requests=[])
    catalog = dict(kind='relational-port-bindings', version=1, puzzleId=source['id'], resources=[], domains=[], pieces={}, placements={}, requests={})
    invariant, goal = [], []
    for d in model.domains:
        name = d['id']; ports = list(map(str, range(d['k'])))
        source['domains'].append(dict(id=name,placements=['p'+str(q) for q in range(len(d['poses']))],
            pieces=[dict(id=p['id'],initial='p'+str(p['home'])) for p in d['pieces']],moves=[]))
        catalog['domains'].append(dict(id=name,pieces=[p['id'] for p in d['pieces']],localPorts=ports,
            placements=[name+'/p'+str(q) for q in range(len(d['poses']))]))
        for p in d['pieces']:
            pose = d['poses'][p['home']]
            catalog['pieces'][p['id']] = dict(domain=name,home=name+'/p'+str(p['home']),
                portLabels={str(j):PALETTE[CELLS.index(c)] for j,c in enumerate(pose)})
            goal.append(at(p['id'],name,[p['home']]))
        slots = {}
        for q, pose in enumerate(d['poses']):
            resource = slot(pose); slots.setdefault(resource,[]).append(q)
            catalog['placements'][name+'/p'+str(q)] = dict(resources=[resource], ports={str(j):dict(
                cell=resource,attachment=label(c)) for j,c in enumerate(pose)})
        for resource, qs in slots.items():
            catalog['resources'].append(resource)
            invariant.append({'count':dict(min=1,max=1,of=[at(p['id'],name,qs) for p in d['pieces']])})
    source['invariant'] = {'all':invariant}; source['goal'] = {'all':goal}
    for move in model.moves:
        name = move['id']; choices = {}
        for d, domain in zip(model.domains, source['domains']):
            table = model.table(d,move); transport={'p'+str(a):'p'+str(b) for a,b in table.items()}
            domain['moves'].append(dict(id=name,event=name,support=list(transport),transport=transport))
            choices[d['id']] = name
        source['requests'].append(dict(id=name, relation={'all':[{'chosen':dict(domain=d,moves=[m])} for d,m in choices.items()]},
            profiles=[dict(id='turn',choices=choices)],defaultProfile='turn'))
        catalog['requests'][name] = dict(inverse=move['inverse'],family=move['group'],event=name,profile='turn')
    stats = compress(source,catalog,{},model.actions())
    realization = dict(schemaVersion=1,id=ID+'-s3-v1',kind='s3-cell-turning',compatibleDefinitionDigest='0'*64,
        requiredCapabilities=['triangle-meshes','so4-isometries','stereographic-projection','dynamic-drawing-meshes'],
        cutDepth=DEPTH,pieceInset=INSET,assets={},domains={},
        operationTracks={m['id']:{key:m[key] for key in ('cell','planeU','planeV','angle')} for m in model.moves},
        controlLayout=dict(version=2,kind='cell-axes',groups=model.groups))
    for k in range(1,5):
        for port,mesh in assets(k).items(): realization['assets'][str(k)+'/'+port] = mesh
    for d in model.domains:
        realization['domains'][d['id']] = dict(activeCoordinates=d['k'],
            placementFrames={'p'+str(q):frame(g) for q,g in enumerate(d['frames'])},
            ports={str(j):str(d['k'])+'/'+str(j) for j in range(d['k'])})
    package = dict(kind='relational-package',formatVersion=3,source=source,catalog=catalog)
    return package,realization,stats


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--write',action='store_true');args=parser.parse_args()
    model=Model(); package,realization,stats=documents(model)
    with tempfile.TemporaryDirectory(prefix='twisty-tesseract-') as tmp:
        path=Path(tmp)/'package.json';path.write_text(json.dumps(package,separators=(',',':')))
        result=json.loads(subprocess.check_output([str(native_tool('twisty')),'validate','--definition',str(path),'--json'],text=True))
        assert result['status']=='Valid',result
    digest=result['definitionDigest'];realization['compatibleDefinitionDigest']=digest
    files={'puzzle.pzl.json':package['source'],'bindings.json':package['catalog'],'package.json':package,
           'tesseract3-s3.json':realization,'manifest.json':dict(puzzleId=ID+'-v1',definitionDigest=digest,pieces=80,
           coloredSectors=216,cells=8,requests=184,placements=440,transportCompression=stats)}
    directory=ROOT/'packages'/ID
    if args.write:directory.mkdir(parents=True,exist_ok=True)
    for name,data in files.items():
        path=directory/name;text=json.dumps(data,separators=(',',':'),sort_keys=True)+'\n'
        if args.write:path.write_text(text)
        elif not path.exists() or path.read_text()!=text:raise SystemExit(f'{path} is stale; run npm run build:puzzles.')
    print('S³ 8-cell: 80 pieces, 216 colored sectors, 440 shared placements, 184 turns',flush=True)


if __name__=='__main__':main()
