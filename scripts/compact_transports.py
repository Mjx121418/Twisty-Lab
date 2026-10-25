"""Exact finite factoring of reviewed transports; no geometric rule inference.

Correspondence anchors match positions; placement references bind to placements.
Neither coordinate set consists of pieces or requires a legal anchor state.
Every rewritten local map is compared including its entire participating domain.
"""
from collections import defaultdict
import json


def _key(x):
    return json.dumps(x, sort_keys=True, separators=(',', ':'))


class Builder:
    def __init__(self, source, actions=None):
        self.source = source
        self.qnames = [d['id']+'/'+q for d in source['domains'] for q in d['placements']]
        self.qid = {q:i for i,q in enumerate(self.qnames)}
        self.c = {k:[] for k in ('groups','actions','collections','spaces','positionMaps','bindings','families','maps')}
        self.interns = defaultdict(dict)
        self.images = {}
        self.world = actions
        self.qcollection = {}
        self.fallback = []
        self.factorings = []
        if actions:
            self.world_group = self.group_from_permutations(actions)
            unseen = set(range(len(self.qnames)))
            while unseen:
                seed = min(unseen)
                orbit = list(dict.fromkeys(p[seed] for p in actions))
                assert set(orbit) <= unseen, 'Placement action is not a partition'
                if len(orbit) == len(actions):
                    a = self.intern('actions', dict(group=self.world_group, size=len(orbit),kind='regular'))
                else:
                    local = {q:i for i,q in enumerate(orbit)}
                    table = [local[p[q]] for p in actions for q in orbit]
                    a = self.intern('actions',dict(group=self.world_group,size=len(orbit),kind='tabulated',table=table))
                c = len(self.c['collections'])
                self.c['collections'].append(dict(action=a,placements=orbit))
                for q in orbit:self.qcollection[q]=c
                unseen -= set(orbit)
        else:
            g = self.intern('groups',dict(order=1,products=[0],inverses=[0]))
            offset = 0
            for d in source['domains']:
                n = len(d['placements'])
                a=self.intern('actions',dict(group=g,size=n,kind='trivial'))
                c=len(self.c['collections']);qs=list(range(offset,offset+n))
                self.c['collections'].append(dict(action=a,placements=qs))
                for q in qs:self.qcollection[q]=c
                offset+=n

    def intern(self, field, value):
        k=_key(value)
        if k not in self.interns[field]:
            self.interns[field][k]=len(self.c[field]);self.c[field].append(value)
        return self.interns[field][k]

    def group_from_permutations(self, permutations):
        n=len(permutations); lookup={tuple(p):i for i,p in enumerate(permutations)}
        assert permutations[0]==list(range(len(permutations[0])))
        products=[lookup[tuple(a[b[q]] for q in range(len(a)))] for a in permutations for b in permutations]
        inverses=[next(b for b in range(n) if products[a*n+b]==0 and products[b*n+a]==0) for a in range(n)]
        return self.intern('groups',dict(order=n,products=products,inverses=inverses))

    def bind_values(self, i):
        b=self.c['bindings'][i]
        if 'placements' in b:return b['placements']
        return [self.world[b['element']][q] for q in self.bind_values(b['base'])]

    def binding(self, qs):
        # Equivalent caps share one binding with a whole-catalog group element.
        space=self.intern('spaces',dict(size=len(qs)))
        wanted=set(qs)
        if self.world:
            for i,b in enumerate(self.c['bindings']):
                if b['space']!=space:continue
                original=self.bind_values(i)
                for g,p in enumerate(self.world):
                    image=[p[q] for q in original]
                    if set(image)==wanted:
                        return space,self.intern('bindings',dict(space=space,base=i,group=self.world_group,element=g)),image
        return space,self.intern('bindings',dict(space=space,placements=qs)),qs

    def position_rows(self, i):
        p=self.c.get('positionMaps',self.c.get('permutations',[]))[i]
        if 'rows' in p:return dict(p['rows'])
        result=self.position_rows(p['base'])
        def act(a,g,q):
            a=self.c['actions'][a]
            if a['kind']=='trivial':return q
            if a['kind']=='regular':return self.c['groups'][a['group']]['products'][g*a['size']+q]
            return a['table'][g*a['size']+q]
        if 'inputAction' in p:
            result={act(p['inputAction'],p.get('inputElement',0),a):b for a,b in result.items()}
        if 'outputAction' in p:
            result={a:act(p['outputAction'],p.get('outputElement',0),b) for a,b in result.items()}
        return result

    def position(self, source_space, target_space, rows, action=None):
        explicit=dict(sourceSpace=source_space,targetSpace=target_space,rows=[list(row) for row in sorted(rows.items())])
        if _key(explicit) in self.interns['positionMaps']:
            return self.interns['positionMaps'][_key(explicit)]
        if action is not None:
            a=self.c['actions'][action];order=self.c['groups'][a['group']]['order']
            for i,p in enumerate(self.c['positionMaps']):
                if (p['sourceSpace'],p['targetSpace'])!=(source_space,target_space) or 'rows' not in p:continue
                old=dict(p['rows'])
                if old.keys()!=rows.keys():continue
                for g in range(1,order):
                    if all(a['table'][g*a['size']+v]==rows[k] for k,v in old.items()):
                        return self.intern('positionMaps',dict(sourceSpace=source_space,targetSpace=target_space,base=i,outputAction=action,outputElement=g))
        return self.intern('positionMaps',explicit)

    def image(self, i):
        if i in self.images:return self.images[i]
        m=self.c['maps'][i];kind=m['kind']
        if kind=='explicit':result=dict(m['rows'])
        elif kind=='group':result={q:self.world[m['element']][q] for q in m['support']}
        elif kind=='difference':
            f=self.c['families'][m['family']];b=self.bind_values(m['binding'])
            a=self.position_rows(f['positions'][m['source']]);z=self.position_rows(f['positions'][m['target']])
            result={b[v]:b[z[k]] for k,v in a.items() if k in z}
        elif kind=='union':
            result={}
            for part in m['parts']:
                for a,b in self.image(part).items():
                    assert a not in result or result[a]==b,'Union rows disagree'
                    result[a]=b
            assert len(set(result.values()))==len(result),'Union is not injective'
        self.images[i]=result
        return result

    def union(self, maps):
        maps=list(dict.fromkeys(maps))
        if len(maps)==1:return maps[0]
        return self.intern('maps',dict(kind='union',parts=maps))

    def explicit(self, table):
        return self.intern('maps',dict(kind='explicit',rows=[list(row) for row in sorted(table.items())]))

    def group_map(self, table):
        if not table:return None
        c=self.qcollection[next(iter(table))]
        if not self.world or any(self.qcollection[q]!=c for q in table):return None
        for g,p in enumerate(self.world):
            if all(p[q]==to for q,to in table.items()):
                return self.intern('maps',dict(kind='group',collection=c,element=g,support=sorted(table)))
        return None

    def factor(self, tables, count, pairs, label):
        # The graph identifies one anchor coordinate across its available stops.
        parent={}
        def find(x):
            parent.setdefault(x,x)
            while parent[x]!=x:
                parent[x]=parent[parent[x]];x=parent[x]
            return x
        def join(a,b):
            a,b=find(a),find(b)
            if a!=b:parent[b]=a
        for name,table in tables.items():
            for s,t in pairs[name]:
                for a,b in table.items():join((s,a),(t,b))
        components=defaultdict(dict)
        for node in list(parent):
            root=find(node);s,q=node
            if s in components[root] and components[root][s]!=q:
                self.fallback.append(label+': inconsistent phase chart')
                return {}
            components[root][s]=q
        if not components:return {k:self.explicit({}) for k in tables}
        qs=sorted({q for comp in components.values() for q in comp.values()})
        space,binding,qs=self.binding(qs);local={q:r for r,q in enumerate(qs)}
        comps=sorted(components.values(),key=lambda v:tuple(local.get(v.get(s),len(qs)) for s in range(count)))
        action=None
        if self.world:
            stabilizer=[p for p in self.world if {p[q] for q in qs}==set(qs)]
            if len(stabilizer)>1:
                perms=[list(p) for p in dict.fromkeys(tuple(local[p[q]] for q in qs) for p in stabilizer)]
                g=self.group_from_permutations(perms)
                action=self.intern('actions',dict(group=g,size=len(qs),kind='tabulated',table=[v for p in perms for v in p]))
        # One anchor per connected correspondence component. Anchors need no
        # binding entries, even when they outnumber bound placement references.
        anchor_space=self.intern('spaces',dict(size=len(comps)))
        positions=[]
        for s in range(count):
            rows={a:local[v[s]] for a,v in enumerate(comps) if s in v}
            positions.append(self.position(anchor_space,space,rows,action))
        family=self.intern('families',dict(anchorSpace=anchor_space,referenceSpace=space,positions=positions))
        self.factorings.append(dict(label=label,anchors=len(comps),references=len(qs),positions=count))
        result={}
        for name,table in tables.items():
            parts=[self.intern('maps',dict(kind='difference',family=family,binding=binding,source=s,target=t))
                   for s,t in pairs[name]]
            m=self.union(parts)
            if self.image(m)==table:result[name]=m
            else:self.fallback.append(label+'/'+name+': incomplete correspondence coverage')
        return result

    def finish(self):
        self.source['languageVersion']=3;self.source['compact']=self.c


def compress(source, catalog, phases, actions=None):
    """phases: family -> {count, pairs: request name -> [(source,target), ...]}.

    Paired opposite-cap requests retain their current local choices and events.
    """
    b=Builder(source,actions);old_rows=0;fallback=[]
    requests={r['id']:r for r in source['requests']}
    offset=0
    for d in source['domains']:
        qids={q:offset+i for i,q in enumerate(d['placements'])};offset+=len(qids)
        moves={m['id']:m for m in d['moves']}
        def table(request):
            choice=requests[request]['profiles'][0]['choices'][d['id']]
            return {qids[a]:qids[z] for a,z in moves[choice]['transport'].items()}
        assigned={};by_table={}
        for family,description in phases.items():
            ts={r:table(r) for r in description['pairs']}
            found=b.factor(ts,description['count'],description['pairs'],d['id']+'/'+family)
            assigned.update(found)
            for name,m in found.items():by_table[tuple(sorted(ts[name].items()))]=m
        # Opposite caps are simultaneous, not a sequential passage through stops.
        for request in requests:
            if '+' not in request:continue
            parts=request.split('+')
            if all(part in assigned for part in parts):
                m=b.union([assigned[part] for part in parts]);t=table(request)
                if b.image(m)==t:by_table[tuple(sorted(t.items()))]=m
        for move in d['moves']:
            t={qids[a]:qids[z] for a,z in move['transport'].items()};old_rows+=len(t)
            m=by_table.get(tuple(sorted(t.items())))
            if m is None:m=b.group_map(t)
            if m is None:
                m=b.explicit(t)
                if t:fallback.append(d['id']+'/'+move['id'])
            assert b.image(m)==t,('Transport parity',d['id'],move['id'])
            move.pop('support');move.pop('transport');move['map']=m
        b.images.clear()
    storage_choices = prefer_explicit(b)
    # Remove unused derivations/fallback attempts; compact descriptors are a DAG.
    prune(b)
    b.finish()
    notes=b.fallback+(['explicit maps: '+', '.join(fallback[:8])+(' ...' if len(fallback)>8 else '')] if fallback else [])
    stats=dict(originalRows=old_rows,explicitFallbacks=len(fallback),explicitStorageChoices=storage_choices,notes=notes,
               groups=len(b.c['groups']),collections=len(b.c['collections']),maps=len(b.c['maps']),
               storedRows=sum(len(m.get('rows',[])) for m in b.c['maps'])+sum(len(p.get('rows',[])) for p in b.c['positionMaps']),
               derivedPositions=sum('base' in p for p in b.c['positionMaps']),factorings=b.factorings)
    print(source['id']+' compact transports: '+_key({**stats,'notes':notes[:3],'factorings':b.factorings[:3]}),flush=True)
    return stats


def prefer_explicit(b):
    """Keep a sparse leaf when it costs less than its exclusive descriptor graph.

    Count shared families, actions, bindings and inverse rows only once. Changing
    a node in place preserves its IDs and all incoming references. This changes
    representation only; the replacement is the exact evaluated partial map.
    """
    from collections import Counter
    c=b.c
    def edges(key):
        field,i=key;v=c[field][i];out=[]
        if field=='maps':
            if v['kind']=='union':out += [('maps',x) for x in v['parts']]
            elif v['kind']=='difference':out += [('families',v['family']),('bindings',v['binding'])]
        elif field=='families':out += [('spaces',v['anchorSpace']),('spaces',v['referenceSpace'])]+[('positionMaps',x) for x in v['positions']]
        elif field=='positionMaps':
            out += [('spaces',v['sourceSpace']),('spaces',v['targetSpace'])]
            if 'base' in v:out.append(('positionMaps',v['base']))
            for a in ('inputAction','outputAction'):
                if a in v:out.append(('actions',v[a]))
        elif field=='bindings':
            out.append(('spaces',v['space']))
            if 'base' in v:out += [('bindings',v['base']),('groups',v['group'])]
        elif field=='actions':out.append(('groups',v['group']))
        return out
    def words(key):
        field,i=key;v=c[field][i]
        if field=='maps':return 8+len(v.get('parts',[]))+len(v.get('support',[]))+2*len(v.get('rows',[]))
        if field=='positionMaps':return 7+4*len(v.get('rows',[]))
        if field=='bindings':return 4+3*len(v.get('placements',[]))
        if field=='families':return 2+len(v['positions'])
        if field=='actions':return 3+len(v.get('table',[]))
        if field=='groups':return 1+len(v['products'])+len(v['inverses'])
        return 1
    roots=[('maps',m['map']) for d in b.source['domains'] for m in d['moves']]
    roots += [('actions',v['action']) for v in c['collections']]
    incoming=Counter(roots);live=set();pending=list(roots)
    while pending:
        key=pending.pop()
        if key in live:continue
        live.add(key)
        for child in edges(key):incoming[child]+=1;pending.append(child)
    def replace(ids):
        ids=sorted({i for i in ids if c['maps'][i]['kind'] in ('difference','union')})
        if not ids:return 0
        released=Counter();freed=[]
        def release(child):
            released[child]+=1
            if released[child]==incoming[child]:
                freed.append(child)
                for descendant in edges(child):release(descendant)
        for i in ids:
            for child in edges(('maps',i)):release(child)
        images={i:b.image(i) for i in ids}
        old=sum(words(('maps',i)) for i in ids)+sum(words(k) for k in freed)
        new=sum(8+2*len(images[i]) for i in ids)
        if new>=old:return 0
        for i in ids:
            c['maps'][i]=dict(kind='explicit',rows=[list(row) for row in sorted(images[i].items())])
        incoming.subtract(released)
        return len(ids)

    root_maps={i for field,i in roots if field=='maps'}
    changed=sum(replace([i]) for i in sorted(root_maps,reverse=True))
    # A family shared by many small moves can be more expensive than all its
    # sparse images, although no single move exclusively owns enough data to
    # release it. Evaluate such choices together, without expanding at runtime.
    for _ in range(3):
        families=defaultdict(set)
        for i in root_maps:
            seen=set();pending=[i]
            while pending:
                m=pending.pop()
                if m in seen:continue
                seen.add(m);node=c['maps'][m]
                if node['kind']=='union':pending.extend(node['parts'])
                elif node['kind']=='difference':families[node['family']].add(i)
        count=sum(replace(families[f]) for f in sorted(families))
        count+=sum(replace([m['map'] for m in d['moves']]) for d in b.source['domains'])
        count+=replace(root_maps)
        changed+=count
        if not count:break
    return changed


def prune(b):
    c=b.c;live=defaultdict(set)
    def use(field,i):
        if i in live[field]:return
        live[field].add(i);v=c[field][i]
        if field=='maps':
            if v['kind']=='union':
                for p in v['parts']:use('maps',p)
            elif v['kind']=='difference':use('families',v['family']);use('bindings',v['binding'])
        elif field=='families':
            use('spaces',v['anchorSpace']);use('spaces',v['referenceSpace'])
            for p in v['positions']:use('positionMaps',p)
        elif field=='positionMaps':
            use('spaces',v['sourceSpace']);use('spaces',v['targetSpace'])
            if 'base' in v:use('positionMaps',v['base'])
            for a in ('inputAction','outputAction'):
                if a in v:use('actions',v[a])
        elif field=='bindings':
            use('spaces',v['space'])
            if 'base' in v:use('bindings',v['base']);use('groups',v['group'])
        elif field=='actions':use('groups',v['group'])
    for d in b.source['domains']:
        for m in d['moves']:use('maps',m['map'])
    for v in c['collections']:use('actions',v['action'])
    remap={k:{old:new for new,old in enumerate(sorted(live[k]))} for k in c if k!='collections'}
    for field,ids in remap.items():c[field]=[c[field][i] for i in sorted(ids)]
    for v in c['actions']:v['group']=remap['groups'][v['group']]
    for v in c['collections']:v['action']=remap['actions'][v['action']]
    for v in c['positionMaps']:
        v['sourceSpace']=remap['spaces'][v['sourceSpace']];v['targetSpace']=remap['spaces'][v['targetSpace']]
        if 'base' in v:v['base']=remap['positionMaps'][v['base']]
        for a in ('inputAction','outputAction'):
            if a in v:v[a]=remap['actions'][v[a]]
    for v in c['bindings']:
        v['space']=remap['spaces'][v['space']]
        if 'base' in v:v['base']=remap['bindings'][v['base']];v['group']=remap['groups'][v['group']]
    for v in c['families']:
        v['anchorSpace']=remap['spaces'][v['anchorSpace']];v['referenceSpace']=remap['spaces'][v['referenceSpace']];v['positions']=[remap['positionMaps'][p] for p in v['positions']]
    for v in c['maps']:
        if v['kind']=='difference':v['family']=remap['families'][v['family']];v['binding']=remap['bindings'][v['binding']]
        if v['kind']=='union':v['parts']=[remap['maps'][p] for p in v['parts']]
    for d in b.source['domains']:
        for m in d['moves']:m['map']=remap['maps'][m['map']]
    b.images.clear()


def helicopter_module(source,catalog,model):
    from helicopter_model import IDENTITY,matmul,transformed,EDGE
    matrices=[IDENTITY]+[g for g in model.symmetries if g!=IDENTITY]
    frames={}
    for d in source['domains']:
        frames[d['id']]={q:model.visual_frames[q] for q in d['placements']}
    marked = len(frames['mechanism']) == 72
    by_frame={d:{v:k for k,v in qs.items()} for d,qs in frames.items() if d!='mechanism' or marked}
    by_shape={transformed(v,EDGE):q for q,v in frames['mechanism'].items()}
    qid={d['id']+'/'+q:i for i,(d,q) in enumerate((d,q) for d in source['domains'] for q in d['placements'])}
    actions=[]
    for g in matrices:
        row=[]
        for d in source['domains']:
            for q in d['placements']:
                f=matmul(g,frames[d['id']][q])
                target=by_shape[transformed(f,EDGE)] if d['id']=='mechanism' and not marked else by_frame[d['id']][f]
                row.append(qid[d['id']+'/'+target])
        actions.append(row)
    phases={}
    for r in source['requests']:
        family,stops=r['id'].split('_')
        phases.setdefault(family,dict(count=6,pairs={}))['pairs'][r['id']]=[(ord(stops[0])-97,ord(stops[1])-97)]
        if marked:
            phases[family]['pairs'][r['id']].append(((ord(stops[0])-97+3)%6,(ord(stops[1])-97+3)%6))
    return compress(source,catalog,phases,actions)


def bagua_module(source,catalog,model):
    from bagua_model import I,Gs,KINDS,mul
    matrices=[I]+[g for g in Gs if g!=I]
    frames={kind:dict(zip([q for q in next(d for d in source['domains'] if d['id']==kind)['placements']],model.frames[KINDS.index(kind)])) for kind in KINDS}
    by_frame={d:{v:k for k,v in qs.items()} for d,qs in frames.items()}
    qid={d['id']+'/'+q:i for i,(d,q) in enumerate((d,q) for d in source['domains'] for q in d['placements'])}
    actions=[[qid[d['id']+'/'+by_frame[d['id']][mul(g,frames[d['id']][q])]] for d in source['domains'] for q in d['placements']] for g in matrices]
    phases={}
    for r in source['requests']:
        family=catalog['requests'][r['id']]['family']
        event=catalog['requests'][r['id']]['event'];delta=int(event.rsplit('/',1)[1])
        phases.setdefault(family,dict(count=8,pairs={}))['pairs'][r['id']]=[(s,(s+delta)%8) for s in range(8)]
    return compress(source,catalog,phases,actions)


def melty_module(source,catalog,model,ids):
    phases={}
    for (i,d),name in ids.items():
        phases.setdefault(str(i),dict(count=len(model.phases[i]),pairs={}))['pairs'][name]=model.phase_pairs[i][d]
    return compress(source,catalog,phases)


def expanded_source(source):
    """Temporary offline export for existing geometric authoring tools only."""
    if source.get('languageVersion')==1:return source
    import copy
    result=copy.deepcopy(source)
    b=object.__new__(Builder);b.c=source['compact'];b.images={}
    n=sum(len(d['placements']) for d in source['domains'])
    if b.c['groups']:
        b.world=[list(range(n)) for _ in range(b.c['groups'][0]['order'])]
        for col in b.c['collections']:
            a=b.c['actions'][col['action']]
            if a['group']!=0:continue
            for g,row in enumerate(b.world):
                for r,q in enumerate(col['placements']):
                    t=r if a['kind']=='trivial' else b.c['groups'][0]['products'][g*a['size']+r] if a['kind']=='regular' else a['table'][g*a['size']+r]
                    row[q]=col['placements'][t]
    else:b.world=[]
    offset=0
    for d in result['domains']:
        qs=d['placements']
        for m in d['moves']:
            image=b.image(m.pop('map'))
            m['transport']={qs[q-offset]:qs[t-offset] for q,t in image.items() if offset<=q<offset+len(qs)}
            m['support']=sorted(m['transport'])
        offset+=len(qs)
    result.pop('compact');result['languageVersion']=1
    return result
