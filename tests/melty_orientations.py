"""Selected Melty cube orientations, measured independently of the optimizer."""
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from melty_model import cross, dot, partition, rays, rotation, unit


def rms(axes):
    angles = [math.acos(min(1., max(abs(x) for x in n))) for n in axes]
    return math.degrees(math.sqrt(sum(a*a for a in angles)/6))


class CubeOrientations(unittest.TestCase):
    def test_selected_orientations(self):
        expected = dict(A=7.558829402, B=7.578041124, C=8.157399449, D=9.381828053)
        face_ids = (5, 1, 3, 0, 2, 4)
        for family in 'ABCD':
            with self.subTest(family=family):
                axes = rays(family, 85)
                self.assertAlmostEqual(rms(axes), expected[family], delta=1e-6)
                self.assertGreater(dot(axes[0], cross(axes[1], axes[2])), 0)
                self.assertEqual(sum(abs(dot(a, b) - math.cos(math.radians(85))) < 1e-12
                                     for i, a in enumerate(axes) for b in axes[i+1:]), 9)
                for n in axes:
                    self.assertAlmostEqual(dot(n, n), 1., delta=1e-12)
                expected_faces = ((2, 1), (0, 1), (1, 1), (0, -1), (1, -1), (2, -1))
                for n, (axis, sign) in zip(axes, expected_faces):
                    self.assertEqual(max(range(3), key=lambda i: abs(n[i])), axis)
                    self.assertGreater(sign*n[axis], 0)
                cells, volume = partition(axes, 1/3, 'cube')
                self.assertEqual(len(cells), 27)
                self.assertAlmostEqual(volume, 8., delta=1e-10)
                for cell in cells:
                    self.assertEqual(set(cell['ports']), {face_ids[i] for i in cell['caps']})

    def test_full_ray_and_piece_symmetries(self):
        cycle = (lambda p: (p[2], p[0], p[1]), (1, 2, 0, 4, 5, 3))
        mirror = (lambda p: (p[1], p[0], p[2]), (0, 2, 1, 4, 3, 5))
        generators = dict(A=[cycle, mirror],
                          B=[(lambda p: (p[2], -p[1], p[0]), (1, 0, 4, 5, 2, 3))],
                          C=[mirror],
                          D=[cycle, (lambda p: (-p[2], -p[1], -p[0]), (3, 5, 4, 0, 2, 1))])
        for family, operations in generators.items():
            axes = rays(family, 85)
            cells, _ = partition(axes, 1/3, 'cube')
            by_caps = {frozenset(c['caps']): c for c in cells}
            for transform, permutation in operations:
                with self.subTest(family=family, permutation=permutation):
                    for i, n in enumerate(axes):
                        self.assertLess(math.dist(transform(n), axes[permutation[i]]), 1e-11)
                    for cell in cells:
                        target = by_caps[frozenset(permutation[i] for i in cell['caps'])]
                        self.assertAlmostEqual(cell['volume'], target['volume'], delta=1e-10)
                        for v in cell['vertices']:
                            self.assertLess(min(math.dist(transform(v), w) for w in target['vertices']), 1e-9)

    def test_bc_minimum_is_angular_rms(self):
        for family, axis in [('B', unit((1., 0., 1.))), ('C', unit((1., -1., 0.)))]:
            axes = rays(family, 85)
            best = rms(axes)
            # Scan the full opposite-edge-axis period, including small
            # perturbations on both sides of the selected optimum.
            for degrees in [-.01, -.0001, .0001, .01, *range(5, 180, 5)]:
                with self.subTest(family=family, degrees=degrees):
                    changed = [rotation(axis, math.radians(degrees), n) for n in axes]
                    self.assertGreater(rms(changed), best)


if __name__ == '__main__':
    unittest.main()
