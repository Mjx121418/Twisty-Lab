#!/usr/bin/env python3
"""Export the exact offline Bagua catalog as a relational language module."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import resource
import subprocess
import tempfile

from bagua_model import AuthoringModel, ROOT


def module(document):
    assert document['goal'] == {'kind': 'home'} and not document['initialState']['mechanism']
    source = dict(kind='relational-puzzle', languageVersion=1, id=document['puzzleId'],
                  domains=[], predicates={}, requests=[])
    catalog = dict(kind='relational-port-bindings', version=1, puzzleId=document['puzzleId'],
                   domains=[], pieces={}, placements={}, requests={}, resources=sorted(document['cells']))
    pieces = {p['id']: p for p in document['pieces']}
    locations = {}
    location_terms = defaultdict(list)
    resource_locations = defaultdict(set)
    for original in sorted(document['placementDomains'], key=lambda d: d['id']):
        name = original['id']
        placements = sorted(original['placements'], key=lambda q: q['key'])
        members = sorted((p for p in pieces.values() if p['type'] == name), key=lambda p: p['id'])
        domain = dict(id=name, placements=[q['key'] for q in placements],
                      pieces=[dict(id=p['id'], initial=p['homePlacement']) for p in members], moves=[])
        source['domains'].append(domain)
        ports = next(t['localPorts'] for t in document['pieceTypes'] if t['id'] == name)
        catalog['domains'].append(dict(id=name, placements=[name + '/' + q['key'] for q in placements],
                                       pieces=[p['id'] for p in members], localPorts=ports))
        local = defaultdict(list)
        for q in placements:
            qualified = name + '/' + q['key']
            location = next(r for r in q['footprint'] if r.startswith('location/'))
            locations[(name, q['key'])] = location
            local[location].append(qualified)
            for r in q['footprint']:
                resource_locations[r].add(location)
            catalog['placements'][qualified] = dict(resources=sorted(q['footprint']), ports=q['portAttachment'])
        for p in members:
            catalog['pieces'][p['id']] = dict(domain=name, home=name + '/' + p['homePlacement'], portLabels=p['portLabels'])
            for location, placements_at in local.items():
                location_terms[location].append({'at': dict(piece=p['id'], placements=placements_at)})
    invariant = []
    for location, terms in sorted(location_terms.items()):
        # Membership leaves serve both occupancy and capacity. Name them once
        # in the source graph as well as interning them in the compiled IR.
        references = []
        for term in terms:
            name = 'p' + format(len(source['predicates']), 'x')
            source['predicates'][name] = term
            references.append({'ref': name})
        source['predicates']['occupied/' + location] = {'any': references}
        invariant.append({'count': dict(min=0, max=1, of=references)})
    for r, occupied in sorted(resource_locations.items()):
        if r.startswith('exclusion/'):
            invariant.append({'count': dict(min=0, max=1,
                                           of=[{'ref': 'occupied/' + q} for q in sorted(occupied)])})
    source['invariant'] = {'all': invariant}
    for op in sorted(document['operations'], key=lambda op: op['id']):
        assert not op.get('pieceGuards') and not op.get('mechanismGuards') and not op.get('placementCountGuards')
        roles = {}
        unavailable = []
        relation = []
        for domain in source['domains']:
            name = domain['id']
            rule = op['placementRules'][name]
            blocked = set(rule['blocked'])
            transport = rule['transports']
            domain['moves'].append(dict(id=op['id'], event=op['transport'], support=sorted(transport), transport=transport))
            relation.append({'chosen': dict(domain=name, moves=[op['id']])})
            for key in domain['placements']:
                location = locations[(name, key)]
                flag = key in blocked
                assert location not in roles or roles[location] == flag, 'Blocking depends on port orientation'
                roles[location] = flag
                if flag:
                    unavailable.append(name + '/' + key)
        relation.append({'not': {'any': [{'ref': 'occupied/' + q} for q, flag in sorted(roles.items()) if flag]}})
        source['requests'].append(dict(id=op['id'], relation={'all': relation}, defaultProfile='turn',
                                       profiles=[dict(id='turn', choices={d['id']: op['id'] for d in source['domains']})]))
        catalog['requests'][op['id']] = dict(inverse=op['inverse'], family=op['family'], event=op['transport'],
                                            profile='turn', unavailablePlacements=sorted(unavailable))
    return source, catalog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--write-source', action='store_true', help='Write only the source and bindings, without compiling geometry pins.')
    parser.add_argument('--probe', action='store_true', help='Write an unreviewed relational package without exclusion constraints to build/.')
    args = parser.parse_args()
    # Keep exact offline geometry authoring within its existing process budget.
    ceiling = 384 * 1024**2
    current, hard = resource.getrlimit(resource.RLIMIT_AS)
    resource.setrlimit(resource.RLIMIT_AS, (min(current, ceiling) if current >= 0 else ceiling, hard))
    model = AuthoringModel()
    document = model.definition(exclusions=not args.probe)
    resource.setrlimit(resource.RLIMIT_AS, (current, hard))
    source, catalog = module(document)
    del document
    assert len(json.dumps(source, separators=(',', ':')).encode()) <= 8 * 1024**2, 'AST exceeds the public source limit'
    package = dict(kind='relational-package', formatVersion=1, source=source, catalog=catalog)
    if args.probe:
        destination = ROOT / 'build/bagua-candidate.json'
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(package, separators=(',', ':'), sort_keys=True) + '\n')
        return
    documents = {'puzzle.pzl.json': source, 'bindings.json': catalog, 'package.json': package}
    directory = ROOT / 'packages/bagua'
    if not args.write_source:
        with tempfile.TemporaryDirectory(prefix='twisty-bagua-') as temporary:
            for name, content in documents.items():
                (Path(temporary) / name).write_text(json.dumps(content, separators=(',', ':'), sort_keys=True))
            result = json.loads(subprocess.check_output([str(ROOT / 'build/native/twisty'), 'validate', '--definition',
                                str(Path(temporary) / 'package.json'), '--json'], text=True))
            assert result['status'] == 'Valid', result
        for filename, realization in model.realizations(result['definitionDigest']).items():
            documents[filename] = realization
    for name, content in documents.items():
        path = directory / name
        if args.write or args.write_source:
            path.write_text(json.dumps(content, separators=(',', ':'), sort_keys=True) + '\n')
        elif not path.exists() or json.loads(path.read_text()) != content:
            raise SystemExit(f'Bagua {name} is stale; review before regenerating.')
    print('Bagua relational authoring: 8 domains, 146 identities, 2232 placements, 30 requests')


if __name__ == '__main__':
    main()
