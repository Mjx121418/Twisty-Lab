#!/usr/bin/env python3
"""Build the reviewed, fixed-angle Melty A–D packages and rigid realizations."""
import argparse
from collections import Counter, defaultdict
import json
import math
import multiprocessing
from pathlib import Path
import subprocess
from string import ascii_lowercase
import tempfile

from melty_model import Model, ROOT, LABELS, OPPOSITE, frame, add, sub, scale, dot, unit, cross


def piece_id(piece):
    return ('center/' if len(piece['caps']) == 1 else 'edge/' if len(piece['caps']) == 2 else
            'corner/' if piece['caps'] else '') + piece['id']


def at(model, p, entries):
    return {'at': dict(piece=piece_id(model.pieces[p]),
                      placements=[model.pieces[p]['id'] + '/p' + str(a) for a in sorted(entries)])}


def ref(name):
    return {'ref': name}


def names(model):
    result = {}
    for i, values in enumerate(model.deltas):
        extra = 0
        for d, angle in enumerate(values):
            if angle < 0:
                continue
            if abs(angle - model.phases[i][1]) < 1e-10:
                name = LABELS[i]
            else:
                name = LABELS[i] + '_' + ascii_lowercase[extra]
                extra += 1
            result[i, d] = name
            inverse = next(e for e, a in enumerate(values) if abs(a+angle) < 1e-10)
            result[i, inverse] = name + '_inv'
    return result


def module(model, conflicts):
    ids = names(model)
    macro_id = lambda macro: '+'.join(ids[part] for part in macro)
    macros = model.macros()
    source = dict(kind='relational-puzzle', languageVersion=1, id=model.id+'-v1',
                  domains=[], predicates={}, requests=[])
    catalog = dict(kind='relational-port-bindings', version=1, puzzleId=source['id'],
                   resources=[], domains=[], pieces={}, placements={}, requests={})
    local_moves = [dict() for _ in model.pieces]
    if model.family != 'A':
        catalog.update(kind='relational-articulated-bindings', events={}, moveInverses={})
    predicates, invariant = source['predicates'], []
    for p, (piece, cell) in enumerate(zip(model.pieces, model.cells)):
        domain, identity = piece['id'], piece_id(piece)
        placements = ['p' + str(a) for a in range(len(piece['placements']))]
        ports = [str(k) for k in cell['ports']]
        resource = 'solid/' + domain
        catalog['resources'].append(resource)
        source['domains'].append(dict(id=domain, pieces=[dict(id=identity, initial='p0')],
                                      placements=placements, moves=[]))
        catalog['domains'].append(dict(id=domain, pieces=[identity], localPorts=ports,
                                       placements=[domain + '/' + a for a in placements]))
        # Physical frames carry the continuous sticker attachment. These opaque
        # port labels retain identity/color independently of the rendered mesh.
        labels = dict(zip(map(str, range(6)), ('L', 'R', 'B', 'F', 'D', 'U')))
        catalog['pieces'][identity] = dict(domain=domain, home=domain+'/p0',
                                           portLabels={k: labels[k] for k in ports})
        for a in placements:
            catalog['placements'][domain+'/'+a] = dict(resources=[resource],
                ports={k: dict(cell=resource, attachment=domain+'/'+a+'/'+k) for k in ports})
        if piece['caps']:
            terms = []
            for i, phases in enumerate(model.phases):
                for s in range(len(phases)):
                    allowed = [a for a, q in enumerate(piece['placements']) if [i, s] in q['phaseGuards']]
                    if allowed:
                        terms.append({'all': [at(model, p, allowed), at(model, model.centers[i], [s])]})
            invariant.append({'any': terms})
    # One exclusion clause per forbidden row, rather than a predicate per collision.
    # The native offline SAT tool checks full solids, including cube outer faces.
    rows = defaultdict(list)
    for (p, a, r), entries in sorted(conflicts.items()):
        rows[p, r, tuple(entries)].append(a)
    for (p, r, entries), poses in sorted(rows.items()):
        a, b = at(model, p, poses), at(model, r, entries)
        # Preserve A's canonical source. New members express mutual exclusion
        # directly as a cardinality bound to fit the existing compiler budget.
        invariant.append({'implies': [a, {'not': b}]} if model.family == 'A' else
                         {'count': dict(min=0, max=1, of=[a, b])})
    source['invariant'] = {'all': invariant}

    def forbidden(name, select):
        terms = []
        for p, piece in enumerate(model.pieces):
            entries = [a for a in range(len(piece['placements'])) if select(p, a)]
            if entries:
                terms.append(at(model, p, entries))
        predicates[name] = {'not': {'any': terms}}

    for i in range(6):
        forbidden('free/'+LABELS[i], lambda p, a, i=i: model.roles[p][a][i] == 'blocked')
        for d in range(len(model.deltas[i])):
            name = ids[i, d]
            predicates['phase/'+name] = at(model, model.centers[i], [s for s, _ in model.phase_pairs[i][d]])
            forbidden('mapped/'+name, lambda p, a, i=i, d=d:
                      model.roles[p][a][i] == 'moving' and a not in model.maps[i, d][p])
            for k in range(6):
                if k in (i, OPPOSITE[i]):
                    continue
                forbidden('clear/'+name+'/'+LABELS[k], lambda p, a, i=i, d=d, k=k:
                          a in model.maps[i, d][p] and model.roles[p][model.maps[i, d][p][a]][k] == 'blocked')
    for macro in macros:
        op = macro_id(macro)
        inverse_macro = tuple((i, next(e for e, a in enumerate(model.deltas[i])
                                      if abs(a + model.deltas[i][d]) < 1e-10)) for i, d in macro)
        assert inverse_macro in macros
        cuts = tuple(i for i, _ in macro)
        stationary = 'stationary/' + ''.join(LABELS[i] for i in cuts)
        handoffs = []
        for k in range(6):
            if k in (cuts[0], OPPOSITE[cuts[0]]):
                continue
            key = stationary + '/' + LABELS[k]
            if key not in predicates:
                forbidden(key, lambda p, a, k=k, cuts=cuts:
                          all(model.roles[p][a][i] != 'moving' for i in cuts) and
                          model.roles[p][a][k] == 'blocked')
            handoffs.append({'all': [ref(key)] + [ref('clear/'+ids[i, d]+'/'+LABELS[k]) for i, d in macro]})
        phase = {'all': [ref('phase/'+ids[i, d]) for i, d in macro]}
        clauses = [phase, {'any': handoffs}]
        for i, d in macro:
            clauses += [ref('free/'+LABELS[i]), ref('mapped/'+ids[i, d])]
        choices = {}
        for p, domain in enumerate(source['domains']):
            table = model.table(macro, p)
            transport = {'p'+str(a): 'p'+str(b) for a, b in sorted(table.items())}
            local = op
            if model.family == 'A':
                domain['moves'].append(dict(id=op, event=op, support=list(transport), transport=transport))
            else:
                # Share a local move only when its complete transport AND its
                # signed driving path agree at every participating placement.
                # Equal endpoints alone cannot certify an animation event.
                motion = tuple(sorted((a, b, i, d) for i, d in macro
                                      for a, b in model.maps[i, d][p].items()))
                local = local_moves[p].get(motion)
                if local is None:
                    local = local_moves[p][motion] = op
                    event = domain['id']+'/'+local
                    domain['moves'].append(dict(id=local, event=event, support=list(transport), transport=transport))
                    catalog['events'][event] = dict(domain=domain['id'], request=op)
            choices[domain['id']] = local
            clauses.append({'chosen': dict(domain=domain['id'], moves=[local])})
        source['requests'].append(dict(id=op, relation={'all': clauses},
                                      profiles=[dict(id='turn', choices=choices, when=phase)],
                                      defaultProfile='turn'))
        catalog['requests'][op] = dict(inverse=macro_id(inverse_macro), family='+'.join(LABELS[i] for i in cuts),
                                       profile='turn', event=op)
    if model.family != 'A':
        for p, domain in enumerate(source['domains']):
            inverses = {}
            for motion, local in local_moves[p].items():
                inverse = tuple(sorted((b, a, i, next(e for e, value in enumerate(model.deltas[i])
                                                      if abs(value+model.deltas[i][d]) < 1e-10))
                                       for a, b, i, d in motion))
                inverses[local] = local_moves[p][inverse]
            catalog['moveInverses'][domain['id']] = inverses
        share_atoms(source)
    return source, catalog


def share_atoms(source):
    """Use existing predicate aliases where repeated membership text is larger."""
    counts, atoms = Counter(), {}

    def collect(value):
        if isinstance(value, dict):
            if len(value) == 1 and next(iter(value)) in ('at', 'chosen'):
                key = json.dumps(value, sort_keys=True, separators=(',', ':'))
                counts[key] += 1
                atoms[key] = value
            else:
                for child in value.values():
                    collect(child)
        elif isinstance(value, list):
            for child in value:
                collect(child)

    collect(source)
    shared = {}
    for key in sorted(counts):
        name = 'atom/'+str(len(shared))
        reference = json.dumps(ref(name), separators=(',', ':'))
        if len(key)*counts[key] > len(key)+len(name)+4+len(reference)*counts[key]:
            shared[key] = name

    def rewrite(value):
        if isinstance(value, dict):
            if len(value) == 1 and next(iter(value)) in ('at', 'chosen'):
                key = json.dumps(value, sort_keys=True, separators=(',', ':'))
                return ref(shared[key]) if key in shared else value
            return {key: rewrite(child) for key, child in value.items()}
        if isinstance(value, list):
            return [rewrite(child) for child in value]
        return value

    compact = rewrite(source)
    compact['predicates'].update({name: atoms[key] for key, name in shared.items()})
    source.clear()
    source.update(compact)


def mesh(polygons):
    vertices, triangles = [], []
    for poly in polygons:
        start = len(vertices)
        # Research uses z-up, with U,R,F positively oriented. The app uses
        # y-up and +z at the front: conjugate rotations through this reflection
        # and reverse mesh winding, preserving proper rigid resting frames.
        vertices.extend([v[0], v[2], v[1]] for v in reversed(poly))
        triangles.extend([[start, start+i, start+i+1] for i in range(1, len(poly)-1)])
    return dict(vertices=vertices, triangles=triangles)


def realizations(model, digest):
    ids = names(model)
    assets, components, tables, paths = {}, {}, {}, {}
    for (i, d), name in ids.items():
        axis = model.axes[i]
        paths[name] = [dict(axis=[axis[0], axis[2], axis[1]], pivot=[0., 0., 0.],
                           angle=-model.deltas[i][d], space='world')]
    for p, (piece, cell) in enumerate(zip(model.pieces, model.cells)):
        domain = piece['id']
        resting = {}
        for a, pose in enumerate(piece['placements']):
            w, x, y, z = model.rotations[pose['rotation']]
            resting['p'+str(a)] = frame((w, -x, -z, -y))
        table = {}
        for macro in model.macros():
            op = '+'.join(ids[part] for part in macro)
            entries = {}
            for i, d in macro:
                entries.update({'p'+str(a): ids[i, d] for a in model.maps[i, d][p]})
            table[op] = entries
        tables[domain] = table
        center = scale(1/len(cell['vertices']), tuple(sum(v[j] for v in cell['vertices']) for j in range(3)))
        body = [[add(center, scale(.992, sub(v, center))) for v in f['vertices']] for f in cell['faces']]
        assets[domain+'/body'] = mesh(body)
        components[domain] = [dict(id='body', role='body', meshAssetId=domain+'/body',
                                    placementFrames=resting, operationTracks=domain)]
        for face in cell['faces']:
            if face['plane'] >= 6:
                continue
            poly = face['vertices']
            middle = scale(1/len(poly), tuple(sum(v[j] for v in poly) for j in range(3)))
            normal = unit(cross(sub(poly[1], poly[0]), sub(poly[2], poly[0])))
            sticker = [add(add(middle, scale(.95, sub(v, middle))), scale(.001, normal)) for v in poly]
            port = str(face['plane'])
            asset = domain+'/port-'+port
            assets[asset] = mesh([sticker])
            components[domain].append(dict(id='port-'+port, role='port', portId=port, meshAssetId=asset,
                                           placementFrames=resting, operationTracks=domain))
    common = dict(schemaVersion=1, compatibleDefinitionDigest=digest,
                  requiredCapabilities=['triangle-meshes', 'rigid-transforms', 'shared-rotation-tracks'],
                  assets=assets, componentsByDomain=components, rotationTracks=paths, operationTrackTables=tables,
                  fidelity='literal-six-plane-cut-cube')
    euclidean = dict(common, id=model.id+'-euclidean-v1', kind='articulated-euclidean')
    # A grid of rigid piece views makes every colored surface inspectable. It
    # shares the same rotations, rules, and signed tracks as the physical cube.
    diagram = json.loads(json.dumps(common))
    for p, piece in enumerate(model.pieces):
        for component in diagram['componentsByDomain'][piece['id']]:
            for f in component['placementFrames'].values():
                f[12], f[13] = (p % 7 - 2.4)*1.55, (1.5-p//7)*1.8
    for asset in diagram['assets'].values():
        asset['vertices'] = [scale(.6, v) for v in asset['vertices']]
    diagram.update(id=model.id+'-port-diagram-v1', kind='articulated-port-diagram')
    return {model.id+'-euclidean.json': euclidean, model.id+'-port-diagram.json': diagram}


UNPINNED = '0' * 64


def generate(family, directory):
    """Exit this worker before loading the native compiler's authoring graph."""
    def emit(filename, content):
        (Path(directory)/filename).write_text(json.dumps(content, sort_keys=True, separators=(',', ':'))+'\n')

    model = Model(family)
    witnesses = model.verify_witnesses()
    conflicts = model.conflicts()
    source, catalog = module(model, conflicts)
    del conflicts
    emit('puzzle.pzl.json', source)
    emit('bindings.json', catalog)
    emit('package.json', dict(kind='relational-package', formatVersion=1, source=source, catalog=catalog))
    manifest = dict(puzzleId=source['id'], definitionDigest=UNPINNED,
        angleDegrees=85, cutDepth='1/3', placementCount=sum(len(p['placements']) for p in model.pieces), domainCount=27,
        requestCount=len(source['requests']), witnessCheck=witnesses,
        phaseAnglesDegrees={LABELS[i]: [math.degrees(a) for a in angles] for i, angles in enumerate(model.phases)},
        turnAnglesDegrees={name: math.degrees(model.deltas[i][d]) for (i,d), name in names(model).items()})
    if family != 'A':
        manifest.update(family=family, witnessedPlacementCount=witnesses['placements'],
            unclassifiedPlacements=[dict(piece=piece_id(piece), placement=piece['id']+'/p'+str(a))
                for piece in model.pieces for a, pose in enumerate(piece['placements'])
                if not pose.get('reachabilityWitnessed', True)])
    emit('manifest.json', manifest)
    del source, catalog
    for filename, content in realizations(model, UNPINNED).items():
        emit(filename, content)


def build(family, write):
    # Generating solids and compiling a large relational package have separate
    # memory peaks. A short-lived worker releases all Python graphs first.
    with tempfile.TemporaryDirectory(prefix='twisty-melty-') as temporary:
        worker = multiprocessing.get_context('spawn').Process(target=generate, args=(family, temporary))
        worker.start()
        worker.join()
        assert worker.exitcode == 0, f'Melty {family} generation failed'
        print(f'Melty {family}: rules and geometry generated; validating native package', flush=True)
        result = json.loads(subprocess.check_output([str(ROOT/'build/native/twisty'), 'validate',
                            '--definition', str(Path(temporary)/'package.json'), '--json'], text=True))
        assert result['status'] == 'Valid', result
        digest = result['definitionDigest']
        directory = ROOT/'packages'/('melty-'+family.lower())
        if write:
            directory.mkdir(parents=True, exist_ok=True)
        for generated in sorted(Path(temporary).glob('*.json')):
            encoded = generated.read_text()
            for field in ('definitionDigest', 'compatibleDefinitionDigest'):
                encoded = encoded.replace(f'"{field}":"{UNPINNED}"', f'"{field}":"{digest}"')
            path = directory/generated.name
            if write:
                path.write_text(encoded)
            elif not path.exists() or path.read_text() != encoded:
                raise SystemExit(f'{path.relative_to(ROOT)} is stale; run npm run build:puzzles.')
        manifest = json.loads((Path(temporary)/'manifest.json').read_text())
        print(f'Melty {family}: 27 pieces, {manifest["placementCount"]} placements, '
              f'{manifest["requestCount"]} directed requests; '
              f'{manifest["witnessCheck"]["placements"]} placement witnesses replayed', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--family', choices=list('ABCD'), help='Check/generate just one family member')
    args = parser.parse_args()
    for family in args.family or 'ABCD':
        build(family, args.write)


if __name__ == '__main__':
    main()
