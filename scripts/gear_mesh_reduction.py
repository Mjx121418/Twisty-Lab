"""Reduce cubic mesh subdivisions in complete symmetry orbits, offline.

Flat patches keep their surfaces. Only gently curved, inward-facing patches
may change, within a cumulative distance limit. No vertices are moved, and
open boundaries can lose a vertex only along an existing straight segment.
"""
from itertools import islice, permutations, product

import gear_symmetry_mesh as mesh


def operations(kind):
    if kind == 'corner/':
        axes = [1, 0, 2]

        def material(name, order):
            if '/port-' not in name:
                return name
            axis = axes[int(name[-1])]
            return 'corner/port-'+str(axes.index(order.index(axis)))

        return [(lambda p, order=order: mesh.permute(p, order), mesh.parity(order),
                 lambda name, order=order: material(name, order)) for order in permutations(range(3))]
    if kind == 'center/':
        return [(lambda p, sx=sx, sz=sz, swap=swap: mesh.center_transform(p, sx, sz, swap),
                 sx*sz*(-1 if swap else 1), lambda name: name)
                for sx in [1, -1] for sz in [1, -1] for swap in [False, True]]
    return [(lambda p, flip=flip, swap=swap: mesh.edge_transform(p, flip, swap),
             flip*(-1 if swap else 1),
             lambda name, swap=swap: name[:-1]+str(1-int(name[-1]))
             if swap and 'port-' in name else name)
            for flip in [1, -1] for swap in [False, True]]


def surface_error(old, new, normal):
    """Bound separation by comparing heights over every triangle intersection.

Both stars are graphs over the same plane. Their height difference is affine
on each intersection polygon, so checking its vertices bounds the complete
surface, rather than just samples. Height separation bounds Euclidean distance.
"""
    axis = min([(1, 0, 0), (0, 1, 0), (0, 0, 1)], key=lambda a: abs(mesh.dot(a, normal)))
    u = mesh.cross(normal, axis)
    u = mesh.scale(u, 1/mesh.length(u))
    v = mesh.cross(normal, u)

    def flat(triangle):
        return [(mesh.dot(p, u), mesh.dot(p, v), mesh.dot(p, normal)) for p in triangle]

    def cross(a, b): return a[0]*b[1]-a[1]*b[0]
    def sub(a, b): return (a[0]-b[0], a[1]-b[1])

    def height(triangle, p):
        a, b, c = triangle
        e, f, d = sub(b, a), sub(c, a), sub(p, a)
        denominator = cross(e, f)
        if denominator <= 1e-12:
            return None
        s, t = cross(d, f)/denominator, cross(e, d)/denominator
        return a[2]+s*(b[2]-a[2])+t*(c[2]-a[2])

    worst = 0
    for a in map(flat, old):
        for b in map(flat, new):
            poly = [p[:2] for p in a]
            for x, y in zip(b, b[1:]+b[:1]):
                clipped = []
                for p, q in zip(poly, poly[1:]+poly[:1]):
                    dp, dq = cross(sub(y, x), sub(p, x)), cross(sub(y, x), sub(q, x))
                    if dp >= -1e-12:
                        clipped.append(p)
                    if dp > 1e-12 and dq < -1e-12 or dp < -1e-12 and dq > 1e-12:
                        t = dp/(dp-dq)
                        clipped.append((p[0]+t*(q[0]-p[0]), p[1]+t*(q[1]-p[1])))
                poly = clipped
                if not poly:
                    break
            for p in poly:
                ha, hb = height(a, p), height(b, p)
                if ha is None or hb is None:
                    return float('inf')
                worst = max(worst, abs(ha-hb))
    return worst


def star_candidates(vertex, faces, incident, points, tolerance):
    """Keep material/crease boundaries, allowing only closed inward curved stars."""
    groups = []
    for i in sorted(incident):
        name, triangle = faces[i]
        a, b, c = [points[w] for w in triangle]
        normal = mesh.cross(mesh.sub(b, a), mesh.sub(c, a))
        if mesh.length(normal) < 1e-10:
            return None
        normal = mesh.scale(normal, 1/mesh.length(normal))
        group = next((g for g in groups if g[0] == name and mesh.dot(normal, g[1]) > 1-1e-12), None)
        if group is None:
            group = [name, normal, []]
            groups.append(group)
        group[2].append(i)
    curved = False
    if tolerance and len({g[0] for g in groups}) == 1:
        normals = [g[1] for g in groups]
        normal = tuple(sum(p[i] for p in normals) for i in range(3))
        if mesh.length(normal) > 1e-10:
            normal = mesh.scale(normal, 1/mesh.length(normal))
            if (min(mesh.dot(p, normal) for p in normals) > .92
                    and all(mesh.dot(g[1], points[vertex]) < -1e-8 for g in groups)):
                groups = [[groups[0][0], normal, sorted(incident)]]
                curved = True
    result = []
    for name, normal, ids in groups:
        links, incoming = {}, {}
        for i in ids:
            triangle = faces[i][1]
            k = triangle.index(vertex)
            a, b = triangle[(k+1) % 3], triangle[(k+2) % 3]
            if a in links or b in incoming:
                return None
            links[a], incoming[b] = b, a
        starts, ends = set(links)-set(incoming), set(incoming)-set(links)
        if not starts and not ends:
            start = min(links)
        elif len(starts) == len(ends) == 1 and not curved:
            start, end = min(starts), min(ends)
            a, b = points[start], points[end]
            edge, delta = mesh.sub(b, a), mesh.sub(points[vertex], a)
            if (mesh.length(mesh.cross(edge, delta)) > 1e-9*mesh.length(edge)
                    or not -mesh.EPS <= mesh.dot(delta, edge) <= mesh.dot(edge, edge)+mesh.EPS):
                return None
        else:
            return None
        polygon = [start]
        while polygon[-1] in links:
            following = links[polygon[-1]]
            if following == start:
                break
            if following in polygon:
                return None
            polygon.append(following)
        if len(polygon) < 3 or len(polygon) != len(set(links)|set(incoming)):
            return None
        candidates = []
        old = [[points[w] for w in faces[i][1]] for i in ids]
        old_area = sum(mesh.dot(mesh.cross(mesh.sub(t[1], t[0]), mesh.sub(t[2], t[0])), normal) for t in old)
        for k in range(len(polygon)):
            new = mesh.triangulate(polygon[k:]+polygon[:k], points, normal)
            if new is None:
                continue
            geometry = [[points[w] for w in triangle] for triangle in new]
            normals = [mesh.cross(mesh.sub(t[1], t[0]), mesh.sub(t[2], t[0])) for t in geometry]
            if any(mesh.dot(n, normal) < .85*mesh.length(n) for n in normals):
                continue
            if curved and any(mesh.dot(n, t[0]) >= -1e-10 for n, t in zip(normals, geometry)):
                continue
            if abs(sum(mesh.dot(n, normal) for n in normals)-old_area) > 1e-8:
                continue
            error = surface_error(old, geometry, normal)
            limit = tolerance if curved else 1e-9
            if error <= limit and (new, error) not in candidates:
                candidates.append((new, error))
        if not candidates:
            return None
        result.append((name, candidates))
    return result


def signature(name, triangle):
    return name, min(tuple(triangle[i:]+triangle[:i]) for i in range(3))


def transport(triangles, mapping, orientation, material):
    result = []
    for name, triangle in triangles:
        mapped = [mapping[w] for w in triangle]
        result.append((material(name), tuple(mapped if orientation > 0 else mapped[::-1])))
    return result


def simplify_orbits(assets, symmetries, tolerance):
    points = sorted({tuple(p) for asset in assets.values() for p in asset['vertices']})
    lookup = {p: i for i, p in enumerate(points)}
    maps = [([lookup[tuple(transform(p))] for p in points], orientation, material)
            for transform, orientation, material in symmetries]
    faces, neighbors = {}, {}
    for name, asset in assets.items():
        for triangle in asset['triangles']:
            ids = tuple(lookup[tuple(asset['vertices'][i])] for i in triangle)
            i = len(faces)
            faces[i] = name, ids
            for vertex in ids:
                neighbors.setdefault(vertex, set()).add(i)
    next_id, seen = len(faces), set()
    errors = {i: 0 for i in faces}
    for vertex in range(len(points)):
        if vertex in seen:
            continue
        orbit = sorted({mapping[vertex] for mapping, _, _ in maps})
        seen.update(orbit)
        incidents = [neighbors.get(w, set()) for w in orbit]
        old = set().union(*incidents)
        incident = neighbors.get(vertex, set())
        # Adjacent orbit stars would require a joint polygon replacement. Keep
        # those vertices; disjoint stars can be replaced without topology changes.
        if len(incident) < 2 or len(incident) > 48 or sum(map(len, incidents)) != len(old):
            continue
        groups = star_candidates(vertex, faces, incident, points, tolerance)
        if not groups:
            continue
        for choice in islice(product(*(candidates for _, candidates in groups)), 256):
            new = [(group[0], triangle) for group, (triangles, _) in zip(groups, choice) for triangle in triangles]
            error = max(errors[i] for i in old)+max(e for _, e in choice)
            if error > tolerance:
                continue
            if len(new) >= len(incident):
                break
            signatures = {signature(name, list(triangle)) for name, triangle in new}
            # A point on a mirror/rotation axis has a nontrivial stabilizer. Its
            # replacement must respect that subgroup as well as the complete orbit.
            if any({signature(name, list(triangle)) for name, triangle in transport(new, mapping, orientation, material)} != signatures
                   for mapping, orientation, material in maps if mapping[vertex] == vertex):
                continue
            replacement = {signature(name, list(triangle)): (name, triangle)
                           for mapping, orientation, material in maps
                           for name, triangle in transport(new, mapping, orientation, material)}
            if len(replacement) >= len(old):
                continue
            for i in sorted(old):
                for w in faces[i][1]:
                    neighbors[w].remove(i)
                del faces[i], errors[i]
            for name, triangle in replacement.values():
                faces[next_id] = name, triangle
                errors[next_id] = error
                for w in triangle:
                    neighbors.setdefault(w, set()).add(next_id)
                next_id += 1
            break
    result = {name: [] for name in assets}
    for i in sorted(faces):
        name, triangle = faces[i]
        result[name].append([points[w] for w in triangle])
    return {name: mesh.pack(triangles) for name, triangles in result.items()}, max(errors.values(), default=0)


def reduce(assets, tolerance):
    result, error = dict(assets), 0
    for kind in ['corner/', 'center/', 'edge/', 'gear-edge/carrier', 'cog']:
        names = [name for name in assets if
                 (name.startswith('gear-edge/') and not name.startswith('gear-edge/carrier')
                  if kind == 'cog' else name.startswith(kind))]
        if names:
            updated, bound = simplify_orbits({name: assets[name] for name in names}, operations(kind), tolerance)
            result.update(updated)
            error = max(error, bound)
    return result, error
