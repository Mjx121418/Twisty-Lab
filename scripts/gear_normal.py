#!/usr/bin/env python3
"""Author Gear Cube family realizations from the supplied triangle meshes.

Geometry uses the same vertices/triangles JSON as other articulated models.
The original binary asset has been converted and is no longer needed.
"""
import argparse
import copy
import json
from pathlib import Path

import gear_model as gear
import gear_cubic_mesh

ROOT = Path(__file__).resolve().parent.parent
MESHES = ROOT / 'packages/gear/normal-meshes.json'
DESTINATION = ROOT / 'packages/gear/gear-normal-euclidean.json'
PLAIN_MESHES = ROOT / 'packages/gear-extreme/plain-edge-meshes.json'


def realization(definition, meshes, plain_meshes=None, *, symmetry_mesh=True):
    if meshes['schemaVersion'] != 1:
        raise ValueError('Unsupported normal Gear Cube mesh schema')
    result = gear.realization_templates(definition)
    templates = {domain: {part['id']: part for part in parts}
                 for domain, parts in result['componentsByDomain'].items()}
    result['assets'] = copy.deepcopy(meshes['assets'])
    if plain_meshes is not None:
        if plain_meshes['schemaVersion'] != 1:
            raise ValueError('Unsupported plain-edge mesh schema')
        result['assets'].update(copy.deepcopy(plain_meshes['assets']))
    result['componentsByDomain'] = {}
    specifications = {
        'corner': ['body', 'port-0', 'port-1', 'port-2'],
        'center': ['body', 'port-0'],
        'gear-edge': ['carrier', 'carrier-port-0', 'carrier-port-1', 'cog', 'port-0', 'port-1'],
        # The normal definition also declares an unused plain-edge domain.
        'edge': ['carrier', 'carrier-port-0', 'carrier-port-1', 'cog', 'port-0', 'port-1'],
    }
    if plain_meshes is not None:
        specifications['edge'] = ['body', 'port-0', 'port-1']
    for domain, names in specifications.items():
        parts = result['componentsByDomain'].setdefault(domain, [])
        for name in names:
            template_id = name.removeprefix('carrier-') if name.startswith('carrier-port-') else name
            if domain == 'edge' and name in ['carrier', 'cog']:
                template_id = 'body'
            component = copy.deepcopy(templates[domain][template_id])
            component['id'] = name
            asset_domain = 'gear-edge' if domain == 'edge' and plain_meshes is None else domain
            component['meshAssetId'] = asset_domain + '/' + name
            if name.startswith('carrier-port-') and domain == 'gear-edge':
                carrier = templates[domain]['carrier']
                component['placementFrames'] = copy.deepcopy(carrier['placementFrames'])
                component['operationTracks'] = copy.deepcopy(carrier['operationTracks'])
            parts.append(component)
    # Bind a single frozen decoration to a persistent identity for picking.
    # Its body-only filter prevents six duplicate balls at the center.
    ball = copy.deepcopy(templates['center']['body'])
    ball.update(id='core-ball', meshAssetId='core-ball', pieceIds=['center/U'])
    ball.pop('symmetries', None)
    ball['placementFrames'] = {key: gear.ident() for key in ball['placementFrames']}
    ball['operationTracks'] = {op: {key: [] for key in paths} for op, paths in ball['operationTracks'].items()}
    result['componentsByDomain']['center'].append(ball)
    result.update(id='gear-normal-euclidean-v1', fidelity='supplied-low-poly-gear-cube',
                  modelProvenance=meshes['provenance'])
    if plain_meshes is not None:
        name = definition['puzzleId'].removesuffix('-experimental-v1')
        result.update(id=name+'-supplied-articulated-euclidean-v1',
                      modelProvenance={'shared': meshes['provenance'], 'plainEdge': plain_meshes['provenance']})
    if symmetry_mesh:
        ball = result['assets']['core-ball']
        result['assets'], provenance = gear_cubic_mesh.author(result['assets'])
        assert result['assets']['core-ball'] == ball
        result['modelProvenance'] = {**result['modelProvenance'], 'symmetryMeshing': provenance}
    # The fixed ball is spherical in both ambient spaces. Keep its source
    # geometry and request radial normals through the portable interpreter.
    ball = result['assets']['core-ball']
    ball['radialNormalTriangles'] = list(range(len(ball['triangles'])))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    meshes = json.loads(MESHES.read_text())
    plain_meshes = json.loads(PLAIN_MESHES.read_text())
    documents = [(DESTINATION, realization(gear.load_definition('gear'), meshes))]
    for name in ['gear-extreme', 'gear-plain']:
        directory = ROOT / 'packages' / name
        definition = gear.load_definition(name)
        documents.append((directory / (name+'-supplied-euclidean.json'), realization(definition, meshes, plain_meshes)))
    for path, result in documents:
        gear.store_realization(path, result, args.write)
        print(f'{path.relative_to(ROOT)}: supplied meshes and exact rotation tracks')


if __name__ == '__main__':
    main()
