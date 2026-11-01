"""Matched spherical and conically cut regular-icosahedron meshes."""
from .icosahedron_geometry import AXES, add, sub, scale, dot, unit
from .icosahedron_model import H
# Retain the formerly defined region helpers and sampling constant for callers.
from .icosahedron_mesh_common import DISPLAY_RADIUS, INSET, mesh, region, inside as inside, SEGMENTS as SEGMENTS

INRADIUS = DISPLAY_RADIUS*H
RINGS = 5


def bounds(piece):
    return [(n,H) if piece.mask & (1<<i) else (scale(-1,n),-H) for i,n in enumerate(AXES)]


def append_patch(asset, center, boundary, point, inset=0., smooth=False):
    points=[center]
    boundary=[unit(add(scale(inset,center),scale(1-inset,p))) for p in boundary]
    for ring in range(1,RINGS+1):
        points += [unit(add(scale(1-ring/RINGS,center),scale(ring/RINGS,p))) for p in boundary]
    offset=len(asset['vertices']);asset['vertices'] += [point(p) for p in points]
    count=len(boundary)
    triangles=[]
    for i in range(count):
        j=(i+1)%count
        triangles.append([0,1+i,1+j])
        for ring in range(1,RINGS):
            a,b=1+(ring-1)*count+i,1+(ring-1)*count+j
            c,d=1+ring*count+i,1+ring*count+j
            triangles.extend([[a,c,d],[a,d,b]])
    if smooth:asset['radialNormalTriangles'] += list(range(len(asset['triangles']),len(asset['triangles'])+len(triangles)))
    asset['triangles'] += [[offset+i for i in t] for t in triangles]


def assets(piece, spherical):
    body=mesh();ports={}
    caps=[i for i in range(20) if piece.mask & (1<<i)]
    for color in caps:
        sector=bounds(piece)+[(sub(AXES[color],AXES[other]),0.) for other in caps if other!=color]
        loops=region(sector)
        if not loops:continue
        port=ports[str(color)]=mesh()
        for center,boundary in loops:
            if spherical:
                backing=lambda p:scale(DISPLAY_RADIUS-.018,p)
                exterior=lambda p:scale(DISPLAY_RADIUS,p)
            else:
                backing=lambda p:scale(INRADIUS*.994/dot(AXES[color],p),p)
                exterior=lambda p:scale(INRADIUS/dot(AXES[color],p),p)
            append_patch(body,center,boundary,backing,smooth=spherical)
            append_patch(port,center,boundary,exterior,INSET,spherical)
    for center,boundary in region(bounds(piece)):
        outer=[scale(DISPLAY_RADIUS-.018,p) if spherical else scale(INRADIUS*.994/max(dot(n,p) for n in AXES),p) for p in boundary]
        origin=len(body['vertices']);body['vertices'].append((0.,0.,0.))
        start=len(body['vertices']);body['vertices']+=outer
        body['triangles'] += [[origin,start+(i+1)%len(outer),start+i] for i in range(len(outer))]
    # Authoring is z-up. The application's reflection to y-up reverses winding.
    for asset in [body,*ports.values()]:
        asset['vertices']=[[v[0],v[2],v[1]] for v in asset['vertices']]
        asset['triangles']=[t[::-1] for t in asset['triangles']]
        if not asset['radialNormalTriangles']:asset.pop('radialNormalTriangles')
    return body,ports


if __name__=='__main__':
    from .icosahedron_model import PIECES
    for kind in (1,2,3):
        p=next(p for p in PIECES if p.mask.bit_count()==kind)
        for spherical in (False,True):
            body,ports=assets(p,spherical)
            print(kind,spherical,len(body['vertices']),{k:len(v['vertices']) for k,v in ports.items()},flush=True)
