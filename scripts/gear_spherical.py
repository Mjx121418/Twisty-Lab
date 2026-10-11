#!/usr/bin/env python3
"""Author spherical articulated views from the converted, supplied GLB meshes."""
import argparse
import json
import math
from pathlib import Path
import gear_model as gear
import gear_normal

ROOT = Path(__file__).resolve().parent.parent


def spherical_normals(assets, radius):
    # Texture clipping leaves vertices inside spherical chord triangles.
    # Radial bands identify both shell surfaces without smoothing their walls.
    for name, asset in assets.items():
        shells = [.925] if name == 'core-ball' else [1, .945]
        lengths = [math.sqrt(sum(x*x for x in p))/radius for p in asset['vertices']]
        asset['radialNormalTriangles'] = [
            i for i, triangle in enumerate(asset['triangles'])
            if any(all(shell-.012 <= lengths[v] <= shell+1e-6 for v in triangle)
                   for shell in shells)]


def realization(definition, meshes):
    # Both geared and plain templates are present in the converted source.
    result = gear_normal.realization(definition, meshes, meshes)
    spherical_normals(result['assets'], meshes['radius'])
    name = definition['puzzleId'].removesuffix('-experimental-v1')
    result.update(id=name+'-spherical-v1', kind='articulated-spherical',
                  radius=meshes['radius'], fidelity='supplied-spherical-gear-mesh',
                  modelProvenance=meshes['provenance'])
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    meshes = json.loads((ROOT/'packages/gear/spherical-meshes.json').read_text())
    for name in ['gear', 'gear-extreme', 'gear-plain']:
        directory = ROOT/'packages'/name
        result = realization(json.loads((directory/'definition.json').read_text()), meshes)
        path = directory/(name+'-spherical.json')
        if args.write:
            path.write_text(json.dumps(result, sort_keys=True, separators=(',', ':'))+'\n')
        elif not gear.same_geometry(json.loads(path.read_text()), json.loads(json.dumps(result))):
            raise RuntimeError(f'{path.relative_to(ROOT)} is stale; run scripts/gear_spherical.py --write')
        print(f'{path.relative_to(ROOT)}: supplied spherical meshes and 300-degree cog tracks')


if __name__ == '__main__':
    main()
