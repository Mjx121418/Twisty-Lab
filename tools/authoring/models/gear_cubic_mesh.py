"""Symmetry-preserving remeshing of the supplied cubic Gear Cube pieces."""
from . import gear_symmetry_mesh as mesh
from . import gear_mesh_reduction
INNER_SURFACE_DEVIATION_LIMIT = .006


def _remesh(assets):
    sectors = mesh.rigid_sectors(assets, cubic_cog=True, optimize=True)
    result = {name: mesh.pack(triangles) for name, triangles in sectors.items()}
    result, bound = gear_mesh_reduction.reduce(result, INNER_SURFACE_DEVIATION_LIMIT)
    if 'core-ball' in assets:
        result['core-ball'] = assets['core-ball']
    return result, bound


def remesh(assets):
    return _remesh(assets)[0]


def author(assets):
    result, bound = _remesh(assets)
    before = sum(len(a['triangles']) for a in assets.values())
    after = sum(len(a['triangles']) for a in result.values())
    assert after <= before*1.03, 'Cubic symmetry meshing exceeds its bounded triangle budget'
    groups = mesh.verify(result, cog_order=2)
    return result, {
        'method': 'Least complex fundamental sector, rotation/reflection copies, then bounded symmetry-orbit reduction',
        'groups': groups, 'sourcePrototypeTriangles': before, 'prototypeTriangles': after,
        'coordinateWeldDigits': 10, 'innerSurfaceDeviationLimit': INNER_SURFACE_DEVIATION_LIMIT,
        'innerSurfaceDeviationBound': bound,
        'centralBallPreserved': True}
