"""Lossless factoring of the finite solid-conflict graph into IN/RANGE guards."""
from collections import Counter, defaultdict
from heapq import nlargest
from math import sqrt


def bits(mask):
    while mask:
        bit = mask & -mask
        yield bit.bit_length() - 1
        mask -= bit


def aggregate(rows):
    combined = {}
    for left, right in rows:
        if right:
            combined[right] = combined.get(right, 0) | left
    return [(left, right) for right, left in combined.items()]


def cost(rows, left_width, right_width):
    return sum(95 + a.bit_count()*left_width + b.bit_count()*right_width for a, b in rows)


def rectangles(matrix, left_width, right_width):
    """Extract profitable shared neighborhoods; preserve every conflict edge."""
    original = matrix.copy()
    remaining = aggregate([(1 << a, b) for a, b in matrix.items()])
    result = []
    for _ in range(96):
        candidates = Counter()
        for i, (_, right) in enumerate(remaining):
            for _, other in remaining[:i]:
                common = right & other
                if common.bit_count() >= 6:
                    candidates[common] += 1
        best, saving = None, 0
        for common, _ in nlargest(24, candidates.items(),
                                  key=lambda x: x[0].bit_count()*sqrt(x[1])):
            covered = [(a, b) for a, b in remaining if b & common == common]
            left = 0
            for a, _ in covered:
                left |= a
            residual = [(a, b ^ common) for a, b in covered if b != common]
            gain = (cost(covered, left_width, right_width) - cost(residual, left_width, right_width)
                    - cost([(left, common)], left_width, right_width))
            if gain > saving:
                best, saving = (left, common), gain
        if best is None:
            break
        left, common = best
        result.append(best)
        remaining = aggregate([(a, b ^ common if b & common == common else b) for a, b in remaining])
    result += remaining
    reconstructed = defaultdict(int)
    for left, right in result:
        for a in bits(left):
            reconstructed[a] |= right
    assert dict(reconstructed) == original, 'Collision factoring changed an edge'
    return result


def exclusion_groups(model, conflicts):
    matrices = defaultdict(dict)
    for (p, a, r), neighbors in sorted(conflicts.items()):
        matrices[p, r][a] = sum(1 << b for b in neighbors)
    edges = []
    for (p, r), matrix in matrices.items():
        left_width, right_width = len(model.pieces[p]['id'])+8, len(model.pieces[r]['id'])+8
        forward = aggregate([(1 << a, b) for a, b in matrix.items()])
        transposed = defaultdict(int)
        for a, neighbors in matrix.items():
            for b in bits(neighbors):
                transposed[b] |= 1 << a
        backward = aggregate([(1 << a, b) for a, b in transposed.items()])
        if cost(backward, right_width, left_width) < cost(forward, left_width, right_width):
            p, r, matrix = r, p, dict(transposed)
            left_width, right_width = right_width, left_width
        edges.extend(((p, left), (r, right)) for left, right in rectangles(matrix, left_width, right_width))
    frequency = Counter(atom for edge in edges for atom in edge)
    groups = {}
    for a, b in edges:
        if (frequency[b], b) > (frequency[a], a):
            a, b = b, a
        neighbors = groups.setdefault(a, {})
        neighbors[b[0]] = neighbors.get(b[0], 0) | b[1]
    # not(A and B1) and not(A and B2) = not(A and (B1 or B2)).
    return [(a, sorted(neighbors.items())) for a, neighbors in groups.items()]
