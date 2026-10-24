import { readFileSync } from 'node:fs';
import { expect, it } from 'vitest';
import createModule from '../../kernel/generated/twisty.mjs';

it('executes compact reference differences and finite actions through WASM', async () => {
  const module = await createModule();
  const source=JSON.parse(readFileSync('examples/language/reference.pzl.json','utf8'));
  const puzzle=new module.Puzzle(JSON.stringify(source));
  try {
    const query=(q: unknown) => JSON.parse(puzzle.queryJSON(JSON.stringify(q)));
    expect(JSON.parse(puzzle.inspectJSON()).experimentalVersion).toBe(2);
    expect(query({op:'act',collection:0,element:2,placement:2}).placement).toBe(1);
    for(const [q,target] of [[0,1],[1,2],[2,null]]) {
      expect(query({op:'transport',map:0,placement:q})).toEqual({status:'Evaluated',placement:target,participates:target!==null});
    }
    const result=query({op:'plan',state:[0],request:'turn',choices:['step']});
    expect(result.status).toBe('Permitted');
    expect(result.transition.after).toEqual([1]);
    expect(result.transition.actions).toEqual([{piece:0,from:0,to:1,event:0}]);
  } finally {puzzle.delete();}
  for(const corrupt of [
    (s:any) => {s.compact.groups[0].products[1]=2;},
    (s:any) => {s.compact.permutations[1].base=1;},
    (s:any) => {s.compact.bindings[0].placements=[0,0,2];},
  ]) {
    const bad=structuredClone(source);corrupt(bad);
    expect(JSON.parse(module.compilePuzzleJSON(JSON.stringify(bad))).status).toBe('Invalid');
  }
});
