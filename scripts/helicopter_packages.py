#!/usr/bin/env python3
"""Project the exact offline Helicopter authoring catalog into language data.

The AST and geometry are exports of the same rational authoring model. The
current portable compiler validates the package before realization pins change.
"""
import argparse
from collections import defaultdict
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess
import tempfile

from helicopter_model import AuthoringModel, ROOT, GRIPS, matmul


def lift_phases(document, model):
    """Mark the twofold mechanism symmetry without changing occupied solids."""
    for domain in document['placementDomains']:
        if not domain['id'].startswith('edge/'):
            continue
        axis = GRIPS.index(domain['id'].split('/')[1])
        base = model.visual_frames[domain['id']+'/a']
        for phase in range(3, 6):
            entry = deepcopy(domain['placements'][phase-3])
            entry['key'] = domain['id']+'/'+'abcdef'[phase]
            domain['placements'].append(entry)
            model.visual_frames[entry['key']] = matmul(model.rotations[axis][phase], base)
    for op in document['operations']:
        axis = GRIPS.index(op['family'])
        p, q = map('abcdef'.index, op['id'][-2:])
        rotation = model.relative[axis][p][q]
        for domain in document['placementDomains']:
            if not domain['id'].startswith('edge/'):
                continue
            rule = op['placementRules'][domain['id']]
            original = dict(rule['transports'])
            frames = {model.visual_frames[e['key']]: e['key'] for e in domain['placements']}
            rule['transports'] = {}
            for entry in domain['placements']:
                key = entry['key']
                old = key[:-1]+'abc'['abcdef'.index(key[-1]) % 3]
                if old in original:
                    target = frames[matmul(rotation, model.visual_frames[key])]
                    assert target[:-1]+'abc'['abcdef'.index(target[-1]) % 3] == original[old]
                    rule['transports'][key] = target
                if key[-1] in 'def' and old in rule['blocked']:
                    rule['blocked'].append(key)


def module(document):
    family = lambda d: d if d in ('center', 'corner') else 'mechanism'
    old_domains = {d['id']: d for d in document['placementDomains']}
    pieces = {p['id']: p for p in document['pieces']}
    groups = {d: [] for d in ('center', 'corner', 'mechanism')}
    for d in old_domains.values():
        groups[family(d['id'])].extend(d['placements'])
    source = dict(kind='relational-puzzle', languageVersion=1, id=document['puzzleId'],
                  domains=[], predicates={}, requests=[])
    catalog = dict(kind='relational-port-bindings', version=1, puzzleId=document['puzzleId'],
                   domains=[], pieces={}, placements={}, requests={}, resources=document['cells'])
    qualified = {}
    location_of = {}
    allowed = {}
    location_terms = defaultdict(list)
    resource_locations = defaultdict(set)
    invariants = []
    for group, entries in groups.items():
        entries.sort(key=lambda q: q['key'])
        members = sorted((p for p in pieces.values() if family(p['type']) == group), key=lambda p: p['id'])
        domain = dict(id=group, placements=[q['key'] for q in entries],
                      pieces=[dict(id=p['id'], initial=p['homePlacement']) for p in members], moves=[])
        source['domains'].append(domain)
        catalog['domains'].append(dict(id=group, pieces=[p['id'] for p in members],
                                       placements=[group + '/' + q['key'] for q in entries],
                                       localPorts=[] if group == 'mechanism' else
                                       ['0', '1', '2'] if group == 'corner' else ['0']))
        for q in entries:
            key = group + '/' + q['key']
            qualified[q['key']] = key
            location = next(r for r in q['footprint'] if r.startswith('location/'))
            location_of[q['key']] = location
            for r in q['footprint']:
                resource_locations[r].add(location)
            catalog['placements'][key] = dict(resources=sorted(q['footprint']), ports=q['portAttachment'])
        for p in members:
            home = group + '/' + p['homePlacement']
            catalog['pieces'][p['id']] = dict(domain=group, home=home, portLabels=p['portLabels'])
            allowed[p['id']] = {q['key'] for q in old_domains[p['type']]['placements']}
            # A shared mechanism domain does not allow identities to exchange axes.
            if group == 'mechanism':
                invariants.append({'at': dict(piece=p['id'], placements=sorted(group + '/' + q for q in allowed[p['id']]))})
            local = defaultdict(list)
            for q in entries:
                if q['key'] in allowed[p['id']]:
                    local[location_of[q['key']]].append(group + '/' + q['key'])
            for location, placements in local.items():
                location_terms[location].append({'at': dict(piece=p['id'], placements=placements)})
    # Share occupancy expressions once per abstract location. Pair resources then
    # reference these predicates instead of repeating piece/placement products.
    for location, terms in sorted(location_terms.items()):
        source['predicates']['occupied/' + location] = {'any': terms}
        invariants.append({'count': dict(min=0, max=1, of=terms)})
    for resource, locations in sorted(resource_locations.items()):
        if resource.startswith('exclusion/'):
            assert len(locations) == 2
            invariants.append({'count': dict(min=0, max=1,
                                             of=[{'ref': 'occupied/' + loc} for loc in sorted(locations)])})
    source['invariant'] = {'all': invariants}
    for operation in sorted(document['operations'], key=lambda o: o['id']):
        op = operation['id']
        event = operation['transport']
        piece, phase = next(iter(operation['pieceGuards'].items()))
        phase_predicate = 'phase/' + phase
        opposite = phase[:-1]+'def'['abc'.index(phase[-1])]
        source['predicates'][phase_predicate] = {'at': dict(piece=piece, placements=[qualified[phase],qualified[opposite]])}
        blocked_locations = set()
        unavailable = []
        for group, entries in groups.items():
            transport = {}
            classification = {}
            for old_domain in old_domains.values():
                if family(old_domain['id']) != group:
                    continue
                rule = operation['placementRules'][old_domain['id']]
                transport.update(rule['transports'])
                blocked = set(rule['blocked'])
                for q in old_domain['placements']:
                    location = location_of[q['key']]
                    role = 2 if q['key'] in blocked else 1 if q['key'] in rule['transports'] else 0
                    assert location not in classification or classification[location] == role
                    classification[location] = role
                    if role == 2:
                        blocked_locations.add(location)
                        unavailable.append(qualified[q['key']])
            move = dict(id=op, event=event, support=sorted(transport), transport=transport)
            if group == 'mechanism':
                move['requires'] = {'ref': phase_predicate}
            next(d for d in source['domains'] if d['id'] == group)['moves'].append(move)
        # R(s,u,a) couples the three domain choices and forbids any occupied
        # straddling/unavailable location, regardless of the blocking domain.
        coupled = [{'chosen': dict(domain=d, moves=[op])} for d in groups]
        interaction = {'not': {'any': [{'ref': 'occupied/' + loc} for loc in sorted(blocked_locations)]}}
        source['requests'].append(dict(id=op, relation={'all': coupled + [interaction]},
                                       profiles=[dict(id='turn', choices={d: op for d in groups},
                                                      when={'ref': phase_predicate})], defaultProfile='turn'))
        # Unavailable sections are frame-verification metadata, never runtime guards.
        catalog['requests'][op] = dict(inverse=operation['inverse'], family=operation['family'],
                                       event=event, profile='turn', unavailablePlacements=sorted(unavailable))
    source['goal'] = {'all': [dict(at=dict(piece=p['id'], placements=[qualified[p['homePlacement']]] +
        ([qualified[p['homePlacement'][:-1]+'d']] if p['id'].startswith('mechanism/') else [])))
        for p in sorted(pieces.values(), key=lambda p:p['id'])]}
    catalog['controls'] = dict(version=1, drivers=[], families=[])
    for name in GRIPS:
        stops = list('abcdef')
        catalog['controls']['drivers'].append(dict(id=name,piece='mechanism/'+name,stops=stops,
            placements={f'edge/{name}/{s}':s for s in stops}))
        routes = [dict(from_=[stops[a]],to=[stops[b]],request=f'{name}_{stops[a%3]}{stops[(b-(3 if a>=3 else 0))%6]}')
                  for a in range(6) for b in range(6) if a != b]
        for route in routes: route['from'] = route.pop('from_')
        catalog['controls']['families'].append(dict(id=name,label=name,drivers=[name],routes=routes))
    return source, catalog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--write-source', action='store_true', help='Write only the source and bindings, without compiling geometry pins.')
    args = parser.parse_args()
    model = AuthoringModel()
    document = model.definition()
    lift_phases(document, model)
    source, catalog = module(document)
    from compact_transports import helicopter_module
    helicopter_module(source,catalog,model)
    package = dict(kind='relational-package', formatVersion=source['languageVersion'], source=source, catalog=catalog)
    documents = {'puzzle.pzl.json': source, 'bindings.json': catalog,
                 'package.json': package}
    directory = ROOT / 'packages/helicopter'
    if not args.write_source:
        with tempfile.TemporaryDirectory(prefix='twisty-helicopter-') as temporary:
            for name, content in documents.items():
                (Path(temporary) / name).write_text(json.dumps(content, sort_keys=True))
            path = Path(temporary) / 'package.json'
            result = json.loads(subprocess.check_output([str(ROOT / 'build/native/twisty'), 'compile',
                                '--definition', str(path), '--json'], text=True))
            assert result['status'] == 'Compiled', result
        geometry_catalog = {**document, 'definitionDigest': result['definition']['definitionDigest']}
        for filename, realization in model.realizations(geometry_catalog).items():
            realization = deepcopy(realization)
            realization['modelByDomain'] = {'center': 'center', 'corner': 'corner', 'mechanism': 'edge'}
            realization['controlLayout'] = dict(version=1, drivers={name:dict(angles=realization['angularStops']) for name in GRIPS})
            angles = realization['angularStops']
            realization['controlLayout']['paths'] = {}
            realization['requiredCapabilities'].append('family-rotation-generators')
            realization['motionFamilies'] = dict(version=1, families={name:dict(axis=list(model.axes[i]),pivot=[0,0,0],
                space='world',coordinates={s:-a for s,a in zip('abcdef',angles)}) for i,name in enumerate(GRIPS)})
            for op,track in realization['operationTracks'].items():
                a,b=track['fromStop'],track['toStop']
                angle=math.remainder(angles[a]-angles[b],math.tau)
                if abs(abs(angle)-math.pi)<1e-12: angle=-math.pi
                realization['controlLayout']['paths'][op] = {op.split('_')[0]:-angle}
                realization['operationTracks'][op]=dict(family=op.split('_')[0],from_='abcdef'[a],to='abcdef'[b],
                    winding=round((angle-(angles[a]-angles[b]))/math.tau))
                realization['operationTracks'][op]['from']=realization['operationTracks'][op].pop('from_')
            documents[filename] = realization
    for name, content in documents.items():
        path = directory / name
        if args.write or args.write_source:
            # Keep the runtime bundle below the existing 4 MiB import ceiling;
            # keep the separate binding file readable for review.
            options = dict(separators=(',', ':')) if name in ('package.json','puzzle.pzl.json') else dict(indent=2)
            path.write_text(json.dumps(content, sort_keys=True, **options) + '\n')
        elif not path.exists() or json.loads(path.read_text()) != content:
            raise SystemExit(f'Helicopter {name} is stale; review before regenerating.')
    print('Helicopter relational authoring: 3 domains, 44 identities, 456 placements, 180 requests')


if __name__ == '__main__':
    main()
