#!/usr/bin/env python3
"""Author the four- and six-disk 2222 pillowcases on exact square cutting grids."""
from ..environment import workspace_root, explicit_tool, selected
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import subprocess
import tempfile

from . import flat_surface_packages as grid
ROOT = workspace_root()
ROTATIONS = grid.ROTATIONS
IDENTITY = grid.IDENTITY


class Pillowcase:
    def __init__(self, six):
        self.six = six
        self.radius = .6 if six else .846
        self.scale = math.sqrt(.5) if six else 1
        self.names = "ABCDEF" if six else "ABCD"
        self.centers = dict(zip(self.names, [(1,1),(3,-1),(3,3),(5,1),(3,1),(1,-1)]
                                if six else [(1,1),(3,1),(1,3),(3,3)]))
        self.translations = ((4,-4),(4,4)) if six else ((4,0),(0,4))

    def world(self, t):
        return ((t[0]-t[1]+2)/4, (t[0]+t[1])/4) if self.six else (t[0]/2,t[1]/2)

    def frame(self, t, q):
        if self.six:
            a,b,c,d = ((q[0]-q[2])*self.scale, (q[1]-q[3])*self.scale,
                       (q[0]+q[2])*self.scale, (q[1]+q[3])*self.scale)
        else:
            a,b,c,d = q
        x,y = self.world(t)
        return [a,c,0,0,b,d,0,0,0,0,1,0,x,y,0,1]

    def canonical(self, t, q):
        candidates = []
        for sign in (1,-1):
            p = t if sign == 1 else (2-t[0],2-t[1])
            orientation = tuple(sign*v for v in q)
            x,y = self.world(p)
            for i in range(-math.floor(x/2)-1,-math.floor(x/2)+2):
                for j in range(-math.floor(y/2)-1,-math.floor(y/2)+2):
                    u,v = self.translations
                    anchor = (p[0]+i*u[0]+j*v[0],p[1]+i*u[1]+j*v[1])
                    m = self.frame(anchor,orientation)
                    if abs(m[12]-1)+abs(m[13]-1) <= 1+1e-9:
                        score = tuple(round(m[k]*1e9) for k in (12,13,0,1,4,5))
                        candidates.append((score,anchor,orientation))
        assert candidates, (t,q)
        _,anchor,orientation = min(candidates)
        return anchor,orientation

    def location(self, t, q, family):
        prototype = set(grid.PROTOTYPES[family])
        return min(self.canonical(t,grid.mm(q,r)) for r in ROTATIONS
                   if {grid.mv(r,p) for p in prototype} == prototype)

    def circle_name(self, t):
        p,_ = self.canonical(t,IDENTITY)
        return next(name for name,c in self.centers.items()
                    if self.canonical(c,IDENTITY)[0] == p)

    def cone(self, t):
        x,y = self.world(t)
        return abs(x-.5-round(x-.5))+abs(y-.5-round(y-.5)) < 1e-9

    def assets(self, family, half):
        poly = grid.boundary(family)
        _,center,_ = grid.star_mesh(poly)
        inset = [tuple(center[i]+(p[i]-center[i])*.94 for i in (0,1)) for p in poly]
        body = grid.clip(poly,0,0,1) if half else poly
        assets = {"body": grid.star_mesh(body)[0]}
        for port,(cx,cy) in enumerate(grid.PORT_CENTERS[family]):
            patch = inset
            for axis,c in ((0,cx/2),(1,cy/2)):
                patch = grid.clip(patch,axis,c-.5,1)
                patch = grid.clip(patch,axis,c+.5,-1)
            if half:
                patch = grid.clip(patch,0,0,1)
            assets[f"port-{port}"] = grid.star_mesh(patch)[0]
        # A cone representative is one half of the full lift. Its opposite
        # copy supplies the other half and the matching orientation marking.
        _,center,_ = grid.star_mesh(body)
        clearance = min(math.dist(center,p) for p in body)
        s = min(.06,clearance*.3)
        marker = [(0,0),(s,0),(s,s*.28),(s*.28,s*.28),(s*.28,s*.8),(0,s*.8)]
        marker = [(x+center[0]-s*.4,y+center[1]-s*.35) for x,y in marker]
        assets["mark"] = grid.mesh(marker,grid.triangulate(marker))
        for asset in assets.values():
            asset["vertices"] = [[round(x*self.scale,12),round(y*self.scale,12),0]
                                  for x,y,_ in asset["vertices"]]
        area = abs(sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(body,body[1:]+body[:1])))/2
        return assets,area*self.scale**2

    def generate(self):
        grid.RADIUS = self.radius/self.scale
        puzzle_id = "flat-pillowcase-6-v1" if self.six else "flat-pillowcase-v1"
        source = dict(kind="relational-puzzle",languageVersion=1,id=puzzle_id,
                      domains=[],predicates={},requests=[])
        catalog = dict(kind="relational-port-bindings",version=1,puzzleId=puzzle_id,
                       resources=[],domains=[],pieces={},placements={},requests={})
        realization = dict(schemaVersion=1,id=puzzle_id,kind="flat-quotient",
                           requiredCapabilities=["triangle-meshes","rigid-transforms","quotient-surface","quotient-orbifold"],
                           flatSurface=dict(topology="pillowcase",chart="pillowcase-square",sideLength=2,
                                            cutRadius=self.radius,
                                            diskCenters={name:list(self.world(c)) for name,c in self.centers.items()}),
                           assets={},componentsByDomain={},fidelity="flat-pillowcase-circle-arrangement")
        invariants,goals,counts = [],[],{}
        total_area = 0
        for domain,base in [("cone","face")] + ([("face","face")] if self.six else []) + [
                ("edge","edge"),("vertex","vertex"),("vertex-core","vertex-core")]:
            poses = set()
            for x in range(4):
                for y in range(8 if self.six else 4):
                    if base == "face" and not (x%2 and y%2):
                        continue
                    if base == "edge" and x%2 == y%2:
                        continue
                    if base in ("vertex","vertex-core") and (x%2 or y%2):
                        continue
                    if base == "face" and self.cone((x,y)) != (domain == "cone"):
                        continue
                    for q in ROTATIONS:
                        if base == "edge" and (q[0] != 0) != (x%2 == 0):
                            continue
                        poses.add(self.canonical((x,y),q))
            poses = sorted(poses)
            by_key = {grid.key(t,q):(t,q) for t,q in poses}
            locations = sorted({self.location(t,q,base) for t,q in poses})
            indices = {p:i for i,p in enumerate(locations)}
            counts[domain] = len(locations)
            members,groups = [],defaultdict(list)
            ports = [str(i) for i in range(len(grid.PORT_CENTERS[base]))]
            for t,q in poses:
                key = grid.key(t,q)
                location = f"location/{domain}/{indices[self.location(t,q,base)]:02}"
                groups[location].append(domain+"/"+key)
                catalog["placements"][domain+"/"+key] = dict(resources=[location],ports={
                    str(i):dict(cell=location,attachment=self.circle_name(
                        (t[0]+grid.mv(q,p)[0],t[1]+grid.mv(q,p)[1])))
                    for i,p in enumerate(grid.PORT_CENTERS[base])})
            for i,location in enumerate(locations):
                name = f"{domain}-{i:02}"
                key = grid.key(*location)
                members.append(dict(id=name,initial=key))
                bindings = catalog["placements"][domain+"/"+key]["ports"]
                catalog["pieces"][name] = dict(domain=domain,home=domain+"/"+key,
                                                portLabels={p:bindings[p]["attachment"] for p in ports})
                goals.append({"at":dict(piece=name,placements=[domain+"/"+key])})
            for location,placements in groups.items():
                catalog["resources"].append(location)
                invariants.append({"count":dict(min=1,max=1,of=[
                    {"at":dict(piece=m["id"],placements=placements)} for m in members])})
            catalog["domains"].append(dict(id=domain,pieces=[m["id"] for m in members],
                                           placements=[domain+"/"+k for k in by_key],localPorts=ports))
            moves,tracks = [],{}
            for name in self.names:
                for suffix,turns in (("",-1),("'",1),("2",-2)):
                    operation = name+suffix
                    transport,paths = {},{}
                    rotation = ROTATIONS[turns%4]
                    for key,(t,q) in by_key.items():
                        circles = [(t[0]+grid.mv(q,p)[0],t[1]+grid.mv(q,p)[1])
                                   for p in grid.PROTOTYPES[base]]
                        driving = [c for c in circles if self.circle_name(c) == name]
                        if not driving:
                            continue
                        assert len(driving) == 1
                        c = driving[0]
                        delta = grid.mv(rotation,(t[0]-c[0],t[1]-c[1]))
                        target = self.canonical((c[0]+delta[0],c[1]+delta[1]),grid.mm(rotation,q))
                        target_key = grid.key(*target)
                        assert target_key in by_key, (domain,operation,key,target_key)
                        transport[key] = target_key
                        pivot = self.world(c)
                        paths[key] = [dict(axis=[0,0,1],pivot=[*pivot,0],
                                           angle=turns*math.pi/2,space="world")]
                    assert set(transport) == set(transport.values())
                    moves.append(dict(id=operation,support=list(transport),transport=transport,
                                      event=operation+".motion"))
                    tracks[operation] = paths
            source["domains"].append(dict(id=domain,placements=list(by_key),pieces=members,moves=moves))
            assets,area = self.assets(base,domain == "cone")
            total_area += area*len(locations)
            frames = {k:self.frame(t,q) for k,(t,q) in by_key.items()}
            components = []
            for component,asset in assets.items():
                asset_id = domain+"/"+component
                realization["assets"][asset_id] = asset
                binding = dict(id=component,meshAssetId=asset_id,
                               role="port" if component.startswith("port-") else "body",
                               placementFrames=frames,operationTracks=tracks)
                if component.startswith("port-"):
                    binding["portId"] = component[5:]
                else:
                    binding["materialBindingId"] = "ink" if component == "mark" else "body"
                components.append(binding)
            realization["componentsByDomain"][domain] = components
        for name in self.names:
            for suffix in ("","'","2"):
                operation = name+suffix
                inverse = name+{"":"'","'":"","2":"2"}[suffix]
                source["requests"].append(dict(id=operation,relation={"all":[
                    {"chosen":dict(domain=d["id"],moves=[operation])} for d in source["domains"]]},
                    profiles=[dict(id="default",choices={d["id"]:operation for d in source["domains"]})],
                    defaultProfile="default"))
                catalog["requests"][operation] = dict(inverse=inverse,family=name,event=operation+".motion",
                                                       profile="default",unavailablePlacements=[])
        source["invariant"],source["goal"] = {"all":invariants},{"all":goals}
        expected = dict(cone=4,edge=4,vertex=8,**{"vertex-core":2})
        if self.six:
            expected = dict(cone=4,face=2,edge=8,vertex=16,**{"vertex-core":4})
        assert counts == expected, counts
        assert abs(total_area-2) < 1e-9, total_area
        for d in source["domains"]:
            maps = {m["id"]:m["transport"] for m in d["moves"]}
            for operation,transport in maps.items():
                inverse = maps[catalog["requests"][operation]["inverse"]]
                assert all(inverse[v] == k for k,v in transport.items())
        manifest = dict(pieceCounts=counts,cutRadius=self.radius,topology="pillowcase",
                        diskCount=len(self.names),uniqueTriangles=sum(
                            len(a["triangles"]) for a in realization["assets"].values()))
        return source,catalog,realization,manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write",action="store_true")
    parser.add_argument("--native",default=None)
    args = parser.parse_args()
    for six in (False,True):
        if not selected('flat-pillowcase-6' if six else 'flat-pillowcase'):
            continue
        model = Pillowcase(six)
        source,catalog,realization,manifest = model.generate()
        package = dict(kind="relational-package",formatVersion=1,source=source,catalog=catalog)
        with tempfile.TemporaryDirectory(prefix="twisty-pillowcase-authoring-") as temp:
            path = Path(temp)/"package.json"
            path.write_text(json.dumps(package))
            result = json.loads(subprocess.check_output(
                [str(explicit_tool(args.native, 'twisty')),"compile","--definition",str(path),"--json"],text=True))
        assert result["status"] == "Compiled", result
        digest = result["definition"]["definitionDigest"]
        realization["compatibleDefinitionDigest"] = digest
        manifest["definitionDigest"] = digest
        folder = ROOT/"packages"/("flat-pillowcase-6" if six else "flat-pillowcase")
        documents = {"puzzle.pzl.json":source,"bindings.json":catalog,"package.json":package,
                     "flat-quotient.json":realization,"manifest.json":manifest}
        if args.write:
            folder.mkdir(parents=True,exist_ok=True)
        for name,document in documents.items():
            path = folder/name
            if args.write:
                path.write_text(json.dumps(document,indent=2,sort_keys=True)+"\n")
            elif not path.exists() or json.loads(path.read_text()) != document:
                raise SystemExit(f"{path.relative_to(ROOT)} is stale; regenerate with --write")
        print(f"{folder.name}: {sum(manifest['pieceCounts'].values())} pieces, "
              f"{manifest['uniqueTriangles']} triangles, {digest}")


if __name__ == "__main__":
    main()
