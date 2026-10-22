#!/usr/bin/env python3
"""Audit the four-cone pillowcase candidate without changing any puzzle package.

The independent quotient/state enumeration uses committed square-torus tables.
Radius areas and the optional illustration reuse the mesh authoring helper.
No generated data, fixtures, application code, or public APIs are updated.
"""
import argparse
from collections import Counter, deque
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
NAMES = "ABCD"


def partner(key):
    """(x,y,q) -> (1-x,1-y,-q), in the author's half-unit coordinates."""
    translation, orientation = key.split("/")
    x, y = map(int, translation.split(","))
    return f"{(2-x)%4},{(2-y)%4}/" + ",".join(
        str(-v) for v in map(int, orientation.split(",")))


def representative(key):
    return min(key, partner(key))


def quotient():
    original = json.loads((ROOT / "packages/flat-torus/package.json").read_text())
    source = dict(kind="relational-puzzle", languageVersion=1,
                  id="pillowcase-four-cone-investigation", domains=[], requests=[])
    catalog = dict(kind="relational-port-bindings", version=1, puzzleId=source["id"],
                   domains=[], pieces={}, placements={}, requests={}, resources=[])
    invariants, goals, counts = [], [], {}
    for domain in original["source"]["domains"]:
        family = domain["id"]
        placements = domain["placements"]
        assert all(partner(p) in placements for p in placements)
        reps = sorted({representative(p) for p in placements})
        old = original["catalog"]["placements"]
        resource = lambda p: old[family + "/" + p]["resources"][0]
        location = lambda p: min(resource(p), resource(partner(p)))
        locations = sorted({location(p) for p in reps})
        names = {r: f"{family}-{i:02}" for i, r in enumerate(locations)}
        counts[family] = len(locations)
        members = []
        for r in locations:
            home = min(representative(p["initial"]) for p in domain["pieces"]
                       if location(p["initial"]) == r)
            piece = names[r]
            members.append(dict(id=piece, initial=home))
            catalog["pieces"][piece] = dict(domain=family, home=family + "/" + home,
                                             portLabels={})
            goals.append({"at": dict(piece=piece, placements=[family + "/" + home])})
            resource_name = "quotient/" + piece
            catalog["resources"].append(resource_name)
        for r in locations:
            group = [family + "/" + p for p in reps if location(p) == r]
            invariants.append({"count": dict(min=1, max=1, of=[
                {"at": dict(piece=m["id"], placements=group)} for m in members
            ])})
        for p in reps:
            catalog["placements"][family + "/" + p] = dict(
                resources=["quotient/" + names[location(p)]], ports={})
        moves = []
        for move in domain["moves"]:
            transport = move["transport"]
            turn = lambda p: transport.get(p, p)
            assert all(turn(partner(p)) == partner(turn(p)) for p in placements)
            if move["id"] not in NAMES:
                continue
            projected = {p: representative(turn(p)) for p in reps if p in transport}
            assert set(projected) == set(projected.values())
            assert all(projected.get(projected.get(p, p), projected.get(p, p)) == p
                       for p in reps)
            moves.append(dict(id=move["id"], support=list(projected),
                              transport=projected, event=move["id"] + ".motion"))
        source["domains"].append(dict(id=family, placements=reps, pieces=members, moves=moves))
        catalog["domains"].append(dict(id=family, pieces=[m["id"] for m in members],
                                       placements=[family + "/" + p for p in reps], localPorts=[]))
    for name in NAMES:
        source["requests"].append(dict(id=name, relation={"all": [
            {"chosen": dict(domain=d["id"], moves=[name])} for d in source["domains"]]},
            profiles=[dict(id="default", choices={d["id"]: name for d in source["domains"]})],
            defaultProfile="default"))
        catalog["requests"][name] = dict(inverse=name, family=name, event=name + ".motion",
                                          profile="default", unavailablePlacements=[])
    source["invariant"], source["goal"] = {"all": invariants}, {"all": goals}
    assert counts == {"face": 4, "edge": 4, "vertex": 8, "vertex-core": 2}
    return dict(kind="relational-package", formatVersion=1, source=source, catalog=catalog), counts


def enumerate_states(package, families=None):
    domains = [d for d in package["source"]["domains"] if families is None or d["id"] in families]
    piece_names, origin, generators = [], [], [[] for _ in NAMES]
    labels, offset = [], 0
    for domain in domains:
        placements = domain["placements"]
        index = {p: i + offset for i, p in enumerate(placements)}
        labels.extend(placements)
        moves = {m["id"]: m["transport"] for m in domain["moves"]}
        for name, generator in zip(NAMES, generators):
            generator.extend(index[moves[name].get(p, p)] for p in placements)
        piece_names.extend(p["id"] for p in domain["pieces"])
        origin.extend(index[p["initial"]] for p in domain["pieces"])
        offset += len(placements)
    origin = tuple(origin)
    words, queue = {origin: ""}, deque([origin])
    while queue:
        state = queue.popleft()
        for name, generator in zip(NAMES, generators):
            target = tuple(generator[p] for p in state)
            if target not in words:
                words[target] = (words[state] + " " + name).strip()
                queue.append(target)
    histogram = Counter(len(word.split()) for word in words.values())
    # Check that the marked initial pieces distinguish every permutation in
    # the generated group. The graph is then a Cayley graph, and the largest
    # distance from solved is also its diameter.
    actions = {tuple(range(offset))}
    pending = deque(actions)
    while pending:
        action = pending.popleft()
        for generator in generators:
            target = tuple(generator[p] for p in action)
            if target not in actions:
                actions.add(target)
                pending.append(target)
    assert len(actions) == len(words)
    return dict(states=len(words), diameter=max(histogram), distanceCounts=dict(sorted(histogram.items()))), \
        (words, piece_names, labels, generators)


def check_native(executable, package, enumeration):
    words, pieces, labels, generators = enumeration
    with tempfile.TemporaryDirectory(prefix="twisty-pillowcase-") as temporary:
        path = Path(temporary) / "package.json"
        path.write_text(json.dumps(package))
        def run(command, **options):
            args = [str(executable), command, "--definition", str(path), "--json"]
            for key, value in options.items():
                args.extend(["--" + key, value])
            return json.loads(subprocess.check_output(args, text=True))
        compiled = run("compile")
        assert compiled["status"] == "Compiled", compiled
        digests, transitions = set(), 0
        for state, word in words.items():
            result = run("run", algorithm=(word + " A A B B C C D D").strip(), policy="transactional")
            assert result["status"] == "Committed", result
            expected = dict(zip(pieces, (labels[p] for p in state)))
            assert result["snapshot"]["state"]["placementOf"] == expected, (word, expected, result["snapshot"]["state"])
            assert len(result["snapshot"]["legalRequests"]) == 4
            digests.add(result["snapshot"]["stateDigest"])
            # Each A A / B B / C C / D D loop checks both directions from
            # this state and its neighbor. Verify all intermediate piece maps.
            current = state
            for name, record in zip("AABBCCDD", result["transitions"][-8:]):
                generator = generators[NAMES.index(name)]
                target = tuple(generator[p] for p in current)
                actual = dict(zip(pieces, (labels[p] for p in current)))
                for action in record["pieceActions"]:
                    assert actual[action["pieceId"]] == action["from"]
                    actual[action["pieceId"]] = action["to"]
                assert actual == dict(zip(pieces, (labels[p] for p in target)))
                current = target
                transitions += 1
        assert len(digests) == len(words)
        return dict(states=len(digests), uniqueDirectedEdges=4 * len(words),
                    checkedLoopTransitions=transitions)


def curved_area(poly, family, radius, prototypes):
    """Restore each sampled chord's circular segment using Green's theorem."""
    if family == "face":
        circles = [(x,y) for x in range(-2,3) for y in range(-2,3)]
    elif family == "edge":
        circles = [(x/2,y) for x in range(-5,6,2) for y in range(-2,3)]
    else:
        circles = [(x/2,y/2) for x in range(-5,6,2) for y in range(-5,6,2)]
    positive = {(x/2,y/2) for x,y in prototypes[family]}
    area = 0.
    for a,b in zip(poly, poly[1:]+poly[:1]):
        area += (a[0]*b[1]-b[0]*a[1])/2
        corrections = []
        for c in circles:
            if max(abs(math.dist(p,c)-radius) for p in (a,b)) > 1e-8:
                continue
            u,v = (a[0]-c[0],a[1]-c[1]),(b[0]-c[0],b[1]-c[1])
            angle = math.atan2(u[0]*v[1]-u[1]*v[0], u[0]*v[0]+u[1]*v[1])
            if (angle > 0) == (c in positive):
                corrections.append(radius**2*(angle-math.sin(angle))/2)
        assert len(corrections) == 1, (family, radius, a, b, corrections)
        area += corrections[0]
    assert area > 0
    return area


def six_disk_study(cone_radius=.846, ordinary_radius=.6):
    """Check the proposed extra ordinary-center disks, without state enumeration."""
    assert .5 < cone_radius < 1
    assert .5 < ordinary_radius < math.sqrt(.5)

    # Half-unit integer coordinates make the lattice symmetries exact.
    # Cone lifts are odd/odd, E lifts are even/even with (x+y)/2 even,
    # and F lifts have (x+y)/2 odd.
    def family(p):
        x,y = p
        if x%2 and y%2:
            return "cone"
        assert x%2 == y%2 == 0
        return "E" if ((x+y)//2)%2 == 0 else "F"
    pivots = ((1,1), (3,1), (1,3), (3,3), (2,2), (2,0))
    for pivot in pivots:
        for x in range(-4,5):
            for y in range(-4,5):
                if x%2 != y%2:
                    continue
                target = (pivot[0]-(y-pivot[1]), pivot[1]+(x-pivot[0]))
                before, after = family((x,y)), family(target)
                assert (before == "cone") == (after == "cone")
                if pivot[0]%2 == 0:
                    assert before == after
                elif before != "cone":
                    assert before != after

    # Intersect lifted circles in one half-open 2x2 torus cell. The extra
    # half-turn acts freely on crossings, so each quotient count is half.
    circles = [(i+.5,j+.5,cone_radius,"cone")
               for i in range(-3,4) for j in range(-3,4)]
    circles += [(i,j,ordinary_radius,"E" if (i+j)%2 == 0 else "F")
                for i in range(-3,5) for j in range(-3,5)]
    crossings = {}
    for i,a in enumerate(circles):
        for b in circles[i+1:]:
            dx,dy = b[0]-a[0], b[1]-a[1]
            distance = math.hypot(dx,dy)
            if not abs(a[2]-b[2]) < distance < a[2]+b[2]:
                continue
            along = (a[2]**2-b[2]**2+distance**2)/(2*distance)
            height = math.sqrt(a[2]**2-along**2)
            x,y = a[0]+along*dx/distance, a[1]+along*dy/distance
            for sign in (-1,1):
                p = x-sign*height*dy/distance, y+sign*height*dx/distance
                if not all(-1e-8 <= v < 2-1e-8 for v in p):
                    continue
                key = tuple(round(v,10) for v in p)
                assert key not in crossings, ("multiple crossing", p)
                assert sum(abs(math.dist(p,c[:2])-c[2]) < 1e-8 for c in circles) == 2
                crossings[key] = tuple(sorted((a[3],b[3])))
    counts = Counter(crossings.values())
    assert all(n%2 == 0 for n in counts.values())
    counts = {"+".join(k): n//2 for k,n in sorted(counts.items())}
    assert set(counts) == {"cone+cone", "E+cone", "F+cone", "E+F"}
    # The old cut graph is already connected; both added circles cross it.
    return dict(coneRadius=cone_radius, ordinaryRadius=ordinary_radius,
                crossings=counts, regions=sum(counts.values())+2,
                moveOrders=dict(A=2,B=2,C=2,D=2,E=4,F=4),
                stateCount="not-enumerated")


def radius_study(svg_path):
    # Import only for sampled circular outlines; the exact quotient audit above
    # deliberately does not import the generator's topology or move functions.
    sys.dont_write_bytecode = True
    import flat_surface_packages as geometry
    rows = []
    for radius in (.60, .75, .80, .84, .845, .85, .90):
        geometry.RADIUS = radius
        families = ["face", "edge"] if radius < math.sqrt(.5) else list(geometry.PROTOTYPES)
        areas = {}
        for family in families:
            poly = geometry.boundary(family)
            area = curved_area(poly, family, radius, geometry.PROTOTYPES)
            areas[family] = area/2 if family == "face" else area
        if radius < math.sqrt(.5):
            lens = 2*radius**2*math.acos(1/(2*radius)) - math.sqrt(4*radius**2-1)/2
            assert abs(areas["edge"]-lens) < 1e-10
            areas["fixed-gap"] = (2 - 4*areas["face"] - 4*areas["edge"])/2
        counts = {"face": 4, "edge": 4, "vertex": 8, "vertex-core": 2, "fixed-gap": 2}
        assert abs(sum(counts[f]*a for f, a in areas.items()) - 2) < 1e-8
        rows.append(dict(radius=radius, pieceAreas={f: round(a, 6) for f, a in areas.items()}))
    # Near this crossing, the face and lobe are the two smallest pieces.
    # It is a useful area-balancing choice, rather than a new move system.
    lo, hi = .84, .85
    for _ in range(30):
        geometry.RADIUS = radius = (lo+hi)/2
        face = curved_area(geometry.boundary("face"), "face", radius, geometry.PROTOTYPES)/2
        lobe = curved_area(geometry.boundary("vertex"), "vertex", radius, geometry.PROTOTYPES)
        if face > lobe:
            lo = radius
        else:
            hi = radius
    balanced_radius = (lo+hi)/2
    if svg_path:
        original = json.loads((ROOT / "packages/flat-torus/puzzle.pzl.json").read_text())
        colors = {"face": "#7ab9ed", "edge": "#f2bf65", "vertex": "#84c99a", "vertex-core": "#b79bd8"}
        svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 960 370">',
               '<rect width="960" height="370" fill="white"/>',
               '<style>text{font-family:Arial,sans-serif;fill:#253344} .title{font-size:19px;font-weight:bold} .note{font-size:14px}</style>']
        for panel, radius in enumerate((.60, .80, .845)):
            geometry.RADIUS = radius
            left, top, scale = 30 + panel*320, 65, 125
            def point(p):
                return left + p[0]*scale, top + (2-p[1])*scale
            def points(poly):
                return " ".join(f"{x:.3f},{y:.3f}" for x, y in map(point, poly))
            diamond = [(1,0), (2,1), (1,2), (0,1)]
            outline = points(diamond)
            svg.append(f'<defs><clipPath id="panel{panel}"><polygon points="{outline}"/></clipPath></defs>')
            svg.append(f'<text class="title" x="{left+125}" y="30" text-anchor="middle">r = {radius:g}</text>')
            svg.append(f'<g clip-path="url(#panel{panel})"><polygon points="{outline}" fill="#e4e7eb"/>')
            for domain in original["domains"]:
                family = domain["id"]
                if radius < math.sqrt(.5) and family not in ("face", "edge"):
                    continue
                poly = geometry.boundary(family)
                local = " ".join(f"{x:.9f},{y:.9f}" for x,y in poly)
                shape = f"shape-{panel}-{family}"
                svg.append(f'<defs><polygon id="{shape}" points="{local}" fill="{colors[family]}" stroke="white" stroke-width="0.0052" stroke-linejoin="round"/></defs>')
                for member in domain["pieces"]:
                    t, q = member["initial"].split("/")
                    t, q = list(map(int, t.split(","))), list(map(int, q.split(",")))
                    for i in (-1,0,1):
                        for j in (-1,0,1):
                            transformed = [(t[0]/2 + q[0]*x + q[1]*y + 2*i,
                                            t[1]/2 + q[2]*x + q[3]*y + 2*j) for x,y in poly]
                            if any(max(p[axis] for p in transformed) < 0 or
                                   min(p[axis] for p in transformed) > 2 for axis in (0,1)):
                                continue
                            tx, ty = point((t[0]/2+2*i, t[1]/2+2*j))
                            matrix = (scale*q[0], -scale*q[2], scale*q[1], -scale*q[3], tx, ty)
                            values = " ".join(map(str, matrix))
                            svg.append(f'<use href="#{shape}" transform="matrix({values})"/>')
            svg.append('</g>')
            svg.append(f'<polygon points="{outline}" fill="none" stroke="#253344" stroke-width="1.4"/>')
            for name, (x,y) in zip(NAMES, ((.5,.5),(1.5,.5),(.5,1.5),(1.5,1.5))):
                px, py = point((x,y))
                svg.append(f'<circle cx="{px}" cy="{py}" r="3.5" fill="#253344"/><text class="note" x="{px+7}" y="{py-7}">{name}</text>')
            caption = "10 regions; two fixed gaps" if radius < math.sqrt(.5) else "18 regions; every region can turn"
            svg.append(f'<text class="note" x="{left+125}" y="343" text-anchor="middle">{caption}</text>')
        svg.append('</svg>')
        svg_path.parent.mkdir(parents=True, exist_ok=True)
        svg_path.write_text("\n".join(svg) + "\n")
    return dict(areas=rows, faceLobeBalanceRadius=round(balanced_radius, 6))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, help="Check all 384 states and all directed edges with this existing CLI")
    parser.add_argument("--svg", type=Path, help="Write the radius comparison illustration")
    args = parser.parse_args()
    package, counts = quotient()
    full, enumeration = enumerate_states(package)
    smaller, _ = enumerate_states(package, {"face", "edge"})
    assert full["states"] == 384 and full["diameter"] == 12
    assert smaller["states"] == 192
    six = six_disk_study()
    equal = six_disk_study(.6, .6)
    assert six["crossings"] == {"cone+cone":16, "E+cone":8, "F+cone":8, "E+F":8}
    assert equal["crossings"] == {"cone+cone":8, "E+cone":8, "F+cone":8, "E+F":8}
    report = dict(pieceCounts=counts, largerRadius=full, smallerRadius=smaller,
                  radiusStudy=radius_study(args.svg), sixDiskGeometry=six,
                  equalRadiusSixDiskGeometry=equal)
    if args.native:
        report["native"] = check_native(args.native.resolve(), package, enumeration)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
