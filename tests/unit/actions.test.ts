import { readFileSync } from 'node:fs';
import { expect, it } from 'vitest';
import createModule from '../../kernel/generated/twisty.mjs';
import { execFileSync } from 'node:child_process';
import { Puzzle } from '../../kernel/index';

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

it('matches native rectangular position queries, permissions and participating self-loops', async () => {
  const module = await createModule();
  const file = 'examples/language/anchors.pzl.json';
  const source = JSON.parse(readFileSync(file, 'utf8'));
  const puzzle = new Puzzle(module, JSON.stringify(source));
  try {
    expect(puzzle.preview.experimentalVersion).toBe(3);
    expect(puzzle.preview.program.placementCount).toBe(3);
    expect(puzzle.preview.program.compact!.bindings![0].placements).toHaveLength(3);
    const queries: any[] = [];
    for (let map=0;map<4;++map) {
      for(let point=0;point<5;++point) queries.push({op:'positionMap',map,point});
      for(let point=0;point<3;++point) queries.push({op:'positionMap',map,point,inverse:true});
      for(let placement=0;placement<3;++placement) queries.push({op:'transport',map,placement});
    }
    for(const state of [[0],[1],[2]]) {
      queries.push({op:'plan',state,request:'turn',choices:['step']});
      queries.push({op:'plan',state,request:'turn',choices:['back']});
      queries.push({op:'plan',state,request:'reverse',choices:['back']});
    }
    const native = execFileSync('build/native/twisty_language',[file], {
      encoding:'utf8', input:queries.map(q=>JSON.stringify(q)).join('\n')+'\n'
    }).trim().split('\n').map(s=>JSON.parse(s));
    expect(queries.map(q=>puzzle.query(q))).toEqual(native);
    expect(puzzle.query({op:'positionMap',map:0,point:3})).toEqual({status:'Evaluated',point:null,defined:false});
    expect(puzzle.query({op:'positionMap',map:1,point:0,inverse:true})).toEqual({status:'Evaluated',point:3,defined:true});
    const loop = puzzle.query({op:'plan',state:[0],request:'turn',choices:['step']});
    expect(loop).toMatchObject({status:'Permitted',transition:{after:[0],actions:[{piece:0,from:0,to:0,event:0}]}});
    expect(puzzle.query({op:'positionMap',map:0,point:3,inverse:true}).status).toBe('Invalid');
  } finally {puzzle.dispose();}
  for(const mutate of [
    (s:any)=>{s.compact.positionMaps[1].rows[2][0]=5;},
    (s:any)=>{s.compact.positionMaps[1].rows[2][1]=3;},
    (s:any)=>{s.compact.positionMaps[1].rows[2][1]=0;},
    (s:any)=>{s.compact.positionMaps[3].inputAction=1;},
    (s:any)=>{s.compact.positionMaps[3].outputAction=0;},
    (s:any)=>{s.compact.positionMaps[3].base=3;},
    (s:any)=>{s.compact.families[0].referenceSpace=0;},
    (s:any)=>{s.compact.bindings[0].space=0;},
    (s:any)=>{s.compact.positionMaps[3].rows=[];},
  ]) {
    const bad=structuredClone(source);mutate(bad);
    expect(JSON.parse(module.compilePuzzleJSON(JSON.stringify(bad))).status).toBe('Invalid');
  }
});
