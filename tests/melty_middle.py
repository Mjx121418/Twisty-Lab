"""Bounded geometric audit of middle-aligned Melty catalog coverage.

This deliberately evaluates missing destinations from rotated solid vertices,
instead of treating a missing transport row as a blocked physical turn. It is
numerical evidence, not an exhaustive reachability/completeness proof.
"""
import argparse
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from melty_model import Model, OPPOSITE, apply, role, turn


def audit(family, steps):
    model = Model(family)
    randomizer = random.Random(81223)
    state = [0] * len(model.pieces)
    clear = [[sum(1 << k for k, value in enumerate(row) if value != 'blocked')
              for row in piece] for piece in model.roles]
    checked, new_poses = 0, set()
    for step in range(steps):
        free = 63
        for p, a in enumerate(state):
            free &= clear[p][a]
            if not model.pieces[p]['placements'][a].get('reachabilityWitnessed', True):
                new_poses.add((p, a))
        candidates = []
        for i in range(6):
            if not free & (1 << i) or not model.middle_aligned(state, i):
                continue
            opposite = OPPOSITE[i]
            middle = 63 & ~(1 << i) & ~(1 << opposite)
            moving = []
            for p, a in enumerate(state):
                if model.roles[p][a][i] == 'moving':
                    moving.append((p, a))
                elif model.roles[p][a][opposite] != 'moving':
                    middle &= clear[p][a]
            source = state[model.centers[i]]
            for target in range(len(model.phases[i])):
                if source == target:
                    continue
                delta = model.delta_index(i, source, target)
                aligned, changed, missing = middle, state.copy(), []
                for p, a in moving:
                    b = model.maps[i, delta][p].get(a)
                    if b is None:
                        rotation = turn(model.axes[i], model.deltas[i][delta])
                        vertices = [apply(rotation, v) for v in model.vertices[p][a]]
                        aligned &= sum(1 << k for k, axis in enumerate(model.axes)
                                       if role(vertices, axis, 1/3) != 'blocked')
                        missing.append((p, a))
                    else:
                        changed[p] = b
                        aligned &= clear[p][b]
                if not aligned:
                    continue
                assert not missing, (family, step, i, source, target, 'missing placements', missing)
                assert model.middle_aligned(changed, i)
                assert all(not piece['caps'] or any(changed[model.centers[k]] == s
                           for k, s in piece['placements'][a]['phaseGuards'])
                           for piece, a in zip(model.pieces, changed)), 'Missing phase guard'
                candidates.append(changed)
                checked += 1
        assert candidates, (family, step, 'no reversible turn')
        state = randomizer.choice(candidates)
    print(f'Melty {family}: {steps} single turns, {checked} geometric destinations, '
          f'{len(new_poses)} additional poses visited; no catalog escape', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--family', choices=list('ABCD'))
    parser.add_argument('--steps', type=int, default=3000)
    args = parser.parse_args()
    assert 0 < args.steps <= 100000
    for family in args.family or 'ABCD':
        audit(family, args.steps)
