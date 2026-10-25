"""Check the authored sphere sections independently of the rendering kernel."""
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from melty_model import LABELS, Model, dot, cross, sub, unit
from melty_packages import realizations
from melty_spherical import RADIUS, DEPTH, SURFACE_LIFT


class SphericalMelty(unittest.TestCase):
    def test_sections_axes_and_color_coverage(self):
        for family in 'ABCD':
            with self.subTest(family=family):
                model = Model(family)
                models = realizations(model, '0'*64)
                cube = models[model.id+'-euclidean.json']
                sphere = models[model.id+'-spherical.json']
                self.assertEqual(sphere['motionFamilies'], cube['motionFamilies'])
                self.assertEqual(sphere['rotationTracks'], cube['rotationTracks'])
                self.assertEqual(sphere['operationTrackTables'], cube['operationTrackTables'])
                self.assertEqual(sphere['componentsByDomain'], cube['componentsByDomain'])
                port_labels = ('L', 'R', 'B', 'F', 'D', 'U')
                area = 0.
                for piece, cell in zip(model.pieces, model.cells):
                    for component in sphere['componentsByDomain'][piece['id']]:
                        asset = sphere['assets'][component['meshAssetId']]
                        if not piece['caps']:
                            self.assertTrue(all(math.dist(v, (0, 0, 0)) < RADIUS for v in asset['vertices']))
                            continue
                        radius = RADIUS if component['role'] == 'port' else RADIUS-SURFACE_LIFT
                        directions = []
                        for v in asset['vertices']:
                            self.assertAlmostEqual(math.sqrt(dot(v, v)), radius, delta=1e-10)
                            p = unit((v[0], v[2], v[1]))
                            directions.append(p)
                            for i, axis in enumerate(model.axes):
                                value = dot(p, axis)-DEPTH
                                self.assertGreaterEqual(value if i in piece['caps'] else -value, -1e-9)
                            if component['role'] == 'port':
                                axis = model.axes[LABELS.index(port_labels[int(component['portId'])])]
                                self.assertGreaterEqual(dot(axis, p), max(dot(n, p) for n in model.axes)-1e-9)
                        self.assertEqual(asset['radialNormalTriangles'], list(range(len(asset['triangles']))))
                        for a, b, c in asset['triangles']:
                            v, w, x = (asset['vertices'][i] for i in (a, b, c))
                            normal = cross(sub(w, v), sub(x, v))
                            self.assertGreater(math.sqrt(dot(normal, normal)), 1e-12)
                            self.assertGreater(dot(normal, v), 0)
                            if component['role'] == 'body':
                                p, q, r = (directions[i] for i in (a, b, c))
                                area += 2*math.atan2(abs(dot(p, cross(q, r))), 1+dot(p, q)+dot(q, r)+dot(r, p))
                self.assertAlmostEqual(area, 4*math.pi, delta=.003)
                by_caps = {frozenset(p['caps']): c for p, c in zip(model.pieces, model.cells)}
                # A separate uniform sphere sampling checks all six ray colors
                # belong to existing piece/port identities, with no exposed core.
                for i in range(4096):
                    z = 1-2*(i+.5)/4096
                    angle = i*math.pi*(3-math.sqrt(5))
                    p = (math.sqrt(1-z*z)*math.cos(angle), math.sqrt(1-z*z)*math.sin(angle), z)
                    caps = frozenset(k for k, n in enumerate(model.axes) if dot(n, p) > DEPTH)
                    self.assertTrue(caps)
                    axis = max(range(6), key=lambda j: dot(model.axes[j], p))
                    self.assertIn(port_labels.index(LABELS[axis]), by_caps[caps]['ports'])
                print(f'Melty {family}: aligned axes, smooth spherical meshes, nearest-ray colors; area {area:.8f}', flush=True)


if __name__ == '__main__':
    unittest.main()
