#!/usr/bin/env python3
"""Author the normal Gear Cube realization from the supplied triangle meshes.

Geometry uses the same vertices/triangles JSON as other articulated models.
The original binary asset has been converted and is no longer needed.
"""
import argparse
import copy
import json
from pathlib import Path

import gear_model as gear

ROOT = Path(__file__).resolve().parent.parent
MESHES = ROOT / 'packages/gear/normal-meshes.json'
DESTINATION = ROOT / 'packages/gear/gear-normal-euclidean.json'


def realization(definition, meshes):
    if meshes['schemaVersion'] != 1:
        raise ValueError('Unsupported normal Gear Cube mesh schema')
    # Reuse exact placement/track authoring and replace the procedural assets.
    result = gear.realization(definition, False, 6)
    templates = {domain: {part['id']: part for part in parts}
                 for domain, parts in result['componentsByDomain'].items()}
    result['assets'] = copy.deepcopy(meshes['assets'])
    result['componentsByDomain'] = {}
    specifications = {
        'corner': ['body', 'port-0', 'port-1', 'port-2'],
        'center': ['body', 'port-0'],
        'gear-edge': ['carrier', 'carrier-port-0', 'carrier-port-1', 'cog', 'port-0', 'port-1'],
        # The normal definition also declares an unused plain-edge domain.
        'edge': ['carrier', 'carrier-port-0', 'carrier-port-1', 'cog', 'port-0', 'port-1'],
    }
    for domain, names in specifications.items():
        parts = result['componentsByDomain'].setdefault(domain, [])
        for name in names:
            template_id = name.removeprefix('carrier-') if name.startswith('carrier-port-') else name
            if domain == 'edge' and name in ['carrier', 'cog']:
                template_id = 'body'
            component = copy.deepcopy(templates[domain][template_id])
            component['id'] = name
            component['meshAssetId'] = ('gear-edge' if domain == 'edge' else domain) + '/' + name
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
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    definition = json.loads((ROOT / 'packages/gear/definition.json').read_text())
    meshes = json.loads(MESHES.read_text())
    result = realization(definition, meshes)
    if args.write:
        DESTINATION.write_text(json.dumps(result, sort_keys=True, separators=(',', ':')) + '\n')
    elif not gear.same_geometry(json.loads(DESTINATION.read_text()), json.loads(json.dumps(result))):
        raise RuntimeError('Normal Gear Cube realization is stale; run scripts/gear_normal.py --write')
    print('Normal Gear Cube: 38 exterior parts + fixed ball; 13 shared mesh assets')


if __name__ == '__main__':
    main()
