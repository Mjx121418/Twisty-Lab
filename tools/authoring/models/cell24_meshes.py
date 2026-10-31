"""Cut-surface meshes for the six 24-cell piece families on unit S³."""
import math
from .cell24_model import CENTERS, DEPTH, PROTOTYPES, IDENTITY, dot, unit
from .tesseract_meshes import polygon

# Halve the former 0.88 extent about each intrinsic piece center.
INSET = .56

def basis(normal):
    result=[]
    for e in IDENTITY:
        v=tuple(e[i]-dot(e,normal)*normal[i] for i in range(4))
        for b in result:v=tuple(v[i]-dot(v,b)*b[i] for i in range(4))
        if dot(v,v)>1e-12:result.append(unit(v))
    assert len(result)==3
    return result

def assets():
    result={}
    for name,active,colors,center in PROTOTYPES:
        bounds=[(tuple((1 if i in active else -1)*x for x in c),DEPTH if i in active else -DEPTH) for i,c in enumerate(CENTERS)]
        for port,color in enumerate(colors):
            mesh=dict(vertices=[],normals=[],triangles=[])
            sectors=bounds+[(tuple(CENTERS[color][j]-CENTERS[other][j] for j in range(4)),0.) for other in colors if other!=color]
            for normal,height in bounds:
                chart=basis(normal);radius=math.sqrt(1-height*height);local=[];empty=False
                for n,h in sectors:
                    a=tuple(radius*dot(n,b) for b in chart);b=h-height*dot(n,normal)
                    if dot(a,a)<1e-12:
                        if b>1e-9:empty=True
                    else:local.append((a,b))
                patch=None if empty else polygon(local, edge_steps=3, rings=2)
                if patch is None:continue
                points,triangles=patch;start=len(mesh['vertices'])
                for p in points:
                    q=tuple(height*normal[i]+radius*sum(p[j]*chart[j][i] for j in range(3)) for i in range(4))
                    outward=tuple(height*q[i]-normal[i] for i in range(4))
                    shrunk=unit(tuple((1-INSET)*q[i]+INSET*center[i] for i in range(4)))
                    n=unit(tuple(outward[i]-dot(outward,shrunk)/dot(q,shrunk)*q[i] for i in range(4)))
                    assert all(dot(shrunk,n)>h-1e-8 for n,h in sectors)
                    mesh['vertices'].append(shrunk);mesh['normals'].append(n)
                mesh['triangles'] += [[start+i for i in t] for t in triangles]
            assert mesh['triangles'],(name,port)
            result[name+'/'+str(port)]=mesh
    return result
