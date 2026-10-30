#!/usr/bin/env python3
"""Generate the ordinary 672-piece 24-cell and its stereographic S³ realization."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from cell24_model import Model, ID, DEPTH, CENTERS, PALETTE, frame, label
from cell24_meshes import assets, INSET
from compact_transports import compress

ROOT = Path(__file__).resolve().parent.parent


def slot(pose): return 'slot/'+'.'.join(label(c) for c in pose[0])
def at(piece, domain, qs): return {'at':dict(piece=piece,placements=[domain+'/p'+str(q) for q in qs])}


def documents(model, puzzle_id=ID, depth=DEPTH, mesh_assets=None, extra_capabilities=()):
    source = dict(kind='relational-puzzle', languageVersion=1, id=puzzle_id+'-v1', domains=[], predicates={}, requests=[])
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
                portLabels={str(j):PALETTE[c] for j,c in enumerate(pose[1])})
            goal.append(at(p['id'],name,[p['home']]))
        slots = {}
        for q, pose in enumerate(d['poses']):
            resource = slot(pose); slots.setdefault(resource,[]).append(q)
            catalog['placements'][name+'/p'+str(q)] = dict(resources=[resource], ports={str(j):dict(
                cell=resource,attachment=label(c)) for j,c in enumerate(pose[1])})
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
    stats = compress(source,catalog,{})
    realization = dict(schemaVersion=1,id=puzzle_id+'-s3-v1',kind='s3-cell-turning',compatibleDefinitionDigest='0'*64,
        requiredCapabilities=['triangle-meshes','so4-isometries','stereographic-projection','dynamic-drawing-meshes','s3-octahedral-cells',*extra_capabilities],
        cellShape='octahedron',cutDepth=depth,pieceInset=INSET,assets=assets() if mesh_assets is None else mesh_assets,domains={},
        operationTracks={m['id']:{key:m[key] for key in ('cell','planeU','planeV','angle')} for m in model.moves},
        controlLayout=dict(version=2,kind='cell-axes',groups=model.groups))
    for d in model.domains:
        realization['domains'][d['id']] = dict(
            cutMembership=[i in d['active'] for i in range(24)],
            colorNormals=[CENTERS[i] for i in d['colors']],pieceCenter=d['center'],
            placementFrames={'p'+str(q):frame(g) for q,g in enumerate(d['frames'])},
            ports={str(j):d['prototype']+'/'+str(j) for j in range(d['k'])})
    package = dict(kind='relational-package',formatVersion=3,source=source,catalog=catalog)
    return package,realization,stats


def generate(model, write=False, puzzle_id=ID, depth=DEPTH, mesh_assets=None, extra_capabilities=(), metadata=None):
    package,realization,stats=documents(model,puzzle_id,depth,mesh_assets,extra_capabilities)
    with tempfile.TemporaryDirectory(prefix='twisty-cell24-') as tmp:
        path=Path(tmp)/'package.json';path.write_text(json.dumps(package,separators=(',',':')))
        result=json.loads(subprocess.check_output([str(ROOT/'build/native/twisty'),'validate','--definition',str(path),'--json'],text=True))
        assert result['status']=='Valid',result
    digest=result['definitionDigest'];realization['compatibleDefinitionDigest']=digest
    pieces=sum(len(d['pieces']) for d in model.domains)
    sectors=sum(len(d['pieces'])*d['k'] for d in model.domains)
    placements=sum(len(d['poses']) for d in model.domains)
    files={'puzzle.pzl.json':package['source'],'bindings.json':package['catalog'],'package.json':package,
           puzzle_id+'-s3.json':realization,'manifest.json':dict(puzzleId=puzzle_id+'-v1',definitionDigest=digest,pieces=pieces,
           coloredSectors=sectors,cells=24,requests=552,placements=placements,transportCompression=stats,**(metadata or {}))}
    directory=ROOT/'packages'/puzzle_id
    if write:directory.mkdir(parents=True,exist_ok=True)
    for name,data in files.items():
        path=directory/name;text=json.dumps(data,separators=(',',':'),sort_keys=True)+'\n'
        if write:path.write_text(text)
        elif not path.exists() or path.read_text()!=text:raise SystemExit(f'{path} is stale; run npm run build:puzzles.')
    print(f'S³ {puzzle_id}: {pieces} pieces, {sectors} colored sectors, {placements} shared placements, 552 ordinary turns',flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--write',action='store_true');args=parser.parse_args()
    generate(Model(),write=args.write)


if __name__=='__main__':main()
