"""Independent correspondence-graph checks using frozen pre-refactor rows."""
from collections import defaultdict
import gzip
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from compact_transports import Builder, prefer_explicit, prune


def components(tables, pairs):
    # Separate graph traversal, rather than the compressor's disjoint-set code.
    graph = defaultdict(set)
    for name, rows in tables.items():
        for s, t in pairs[name]:
            for a, b in rows.items():
                graph[s, a].add((t, b))
                graph[t, b].add((s, a))
    result = []
    unseen = set(graph)
    while unseen:
        pending, found = [unseen.pop()], set()
        while pending:
            node = pending.pop()
            if node in found:
                continue
            found.add(node)
            pending.extend(graph[node] - found)
        unseen -= found
        assert len({s for s, _ in found}) == len(found), 'Inconsistent correspondence'
        result.append(found)
    return result


source = dict(domains=[dict(id='toy', placements=['a', 'b'], moves=[])])
b = Builder(source)
# More anchors than references, with a component absent at nominal position 0.
tables = {'ab': {0: 1}, 'bc': {0: 1}, 'ac': {1: 0}}
pairs = {'ab': [(0, 1)], 'bc': [(1, 2)], 'ac': [(0, 2)]}
found = b.factor(tables, 3, pairs, 'toy')
assert len(components(tables, pairs)) == 3
assert len(b.c['families']) == 1 and b.factorings == [dict(label='toy', anchors=3, references=2, positions=3)]
for name, image in tables.items():
    assert b.image(found[name]) == image
assert len(b.c['bindings'][0]['placements']) == 2
assert not b.fallback
# Distinct source/target space IDs must participate in position interning.
b.c['spaces'].append(dict(size=3))
other = len(b.c['spaces']) - 1
first = b.position(1, 0, {0: 0})
second = b.position(other, 0, {0: 0})
assert first != second
b.c['spaces'].append(dict(size=2))
assert first != b.position(1, len(b.c['spaces']) - 1, {0: 0})
# Shared templates must be considered together: none of these three tiny maps
# exclusively owns the family, but retaining all of it costs more than rows.
b.source['domains'][0]['moves'] = [dict(id=name, map=found[name]) for name in tables]
assert prefer_explicit(b) == 3
prune(b)
assert not b.c['families'] and not b.c['positionMaps']
assert all(b.image(move['map']) == tables[move['id']] for move in b.source['domains'][0]['moves'])

root = Path(__file__).resolve().parents[1]
with gzip.open(root / 'tests/fixtures/reference-baseline.jsonl.gz', 'rt') as fixtures:
    baseline = next(json.loads(line) for line in fixtures if '"name":"helicopter"' in line)
original = baseline['package']
source = original['source']
requests = {r['id']: r for r in source['requests']}
controls = original['catalog']['controls']
drivers = {d['id']: d for d in controls['drivers']}
offset = 0
reports = defaultdict(set)
for domain_index, domain in enumerate(source['domains']):
    qs = {q: offset+i for i, q in enumerate(domain['placements'])}
    offset += len(qs)
    move_indices = {m['id']: i+1 for i, m in enumerate(domain['moves'])}
    for family in controls['families']:
        assert len(family['drivers']) == 1
        stops = drivers[family['drivers'][0]]['stops']
        pairs = defaultdict(list)
        for route in family['routes']:
            pairs[route['request']].append((stops.index(route['from'][0]), stops.index(route['to'][0])))
        tables = {}
        for name in pairs:
            choice = requests[name]['profiles'][0]['choices'][domain['id']]
            signature = baseline['moveMaps'][domain_index][move_indices[choice]]
            tables[name] = dict(baseline['maps'][signature])
            assert set(tables[name]) <= set(qs.values())
            assert set(tables[name].values()) <= set(qs.values())
        graph = components(tables, pairs)
        references = {q for comp in graph for _, q in comp}
        report = (len(graph), len(references), len(stops))
        reports[domain['id']].add(report)
        b = Builder(source)
        found = b.factor(tables, len(stops), pairs, domain['id']+'/'+family['id'])
        assert len(b.c['families']) == 1, 'Artificial charts reintroduced'
        assert not b.fallback
        assert all(b.image(found[name]) == rows for name, rows in tables.items())
        f = b.c['families'][0]
        assert (b.c['spaces'][f['anchorSpace']]['size'], b.c['spaces'][f['referenceSpace']]['size'], len(f['positions'])) == report
        assert len(b.c['bindings'][0]['placements']) == len(references)
for category, values in reports.items():
    print('Helicopter', category, sorted(values), 'anchors / references / positions', flush=True)
print('Correspondence factoring matches the independently traversed graph; no anchor binding entries.')
