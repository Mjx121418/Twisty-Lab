#!/usr/bin/env python3
"""Generate the 192-movable-piece 24-cell with exact 45-degree cuts."""
import argparse
from cell24_192_model import Model, ID
from cell24_192_meshes import assets, DEPTH
from cell24_packages import generate


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--write',action='store_true');args=parser.parse_args()
    generate(Model(),write=args.write,puzzle_id=ID,depth=DEPTH,mesh_assets=assets(),
             extra_capabilities=['s3-octahedral-tangent-cuts'],
             metadata=dict(movablePieces=192,fixedCenters=24,cutRadiusDegrees=45))


if __name__=='__main__':main()
