// Current wire-schema and independent canonical-byte conformance; not a runtime loader.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';
import Ajv from 'ajv';

// Reference encoder for integer-only contract envelopes, not geometric payloads.
export function canonicalIntegerJSON(value) {
  function normalize(v) {
    if (v === null || typeof v === 'boolean') return v;
    if (typeof v === 'string') {
      assert.ok(v.isWellFormed(), 'Unpaired Unicode surrogate');
      return v;
    }
    if (typeof v === 'number') {
      assert.ok(Number.isInteger(v) && v >= 0 && v <= 4294967294, 'Expected uint32 excluding reserved sentinel');
      return v === 0 ? 0 : v;
    }
    if (Array.isArray(v)) return v.map(normalize);
    assert.ok(v && typeof v === 'object' && Object.getPrototypeOf(v) === Object.prototype, 'Expected a JSON object');
    return Object.fromEntries(Object.keys(v).sort().map(key => {
      assert.match(key, /^[A-Za-z][A-Za-z0-9]*$/, 'Envelope property names must be ASCII contract fields');
      return [key, normalize(v[key])];
    }));
  }
  return JSON.stringify(normalize(value));
}

export async function checkWireContracts() {
  const read = file => readFile(new URL(`../../${file}`, import.meta.url), 'utf8').then(JSON.parse);
  const ajv = new Ajv({ strict: false, allErrors: true });
  for (const name of ['compact-v2','artifact-common','abstract-artifact','interpretation','interpretation-v2',
    'control-artifact','presentation-artifact','realization-envelope','artifact-backend','backend-payload','artifact-realization'])
    ajv.addSchema(await read(`schemas/${name}.schema.json`));
  const runtime = 'runtime-puzzles/cube3/';
  const view = runtime+'realizations/cube-euclidean/';
  const samples = {
    abstract: ['abstract-artifact:1', await read('examples/artifact/data/cube3/abstract.json')],
    grouped: ['abstract-artifact:1', await read(runtime+'abstract.json')],
    interpretation: ['interpretation:1', await read('examples/artifact/data/cube3/interpretation.json')],
    correspondence: ['interpretation:2', await read(runtime+'interpretation.json')],
    controls: ['controls:1', await read('runtime-puzzles/helicopter/controls.json')],
    presentation: ['presentation:1', await read(runtime+'presentation.json')],
    realization: ['realization-envelope:1', await read(view+'realization.json')],
    payload: ['backend-payload:1', await read(view+'geometry.json')],
    legacy: ['artifact-realization:1', await read('examples/artifact/data/cube3/realization.json')],
  };
  let checks = 0;
  function validate(name, value, expected = true) {
    const validator = ajv.getSchema(`urn:twisty:schema:${samples[name][0]}`);
    assert.ok(validator,name);
    assert.equal(validator(value),expected,`${name}: ${ajv.errorsText(validator.errors)}`);
    ++checks;
  }
  for (const [name,[,value]] of Object.entries(samples)) validate(name,value);
  const bare = structuredClone(samples.abstract[1]); delete bare.bindings; delete bare.session;
  validate('abstract',bare);
  const mutate = (name,edit) => { const value=structuredClone(samples[name][1]);edit(value);validate(name,value,false); };
  mutate('abstract',v=>{v.source={};});
  mutate('abstract',v=>{v.program.coordinates=[];});
  mutate('abstract',v=>{v.program.actionRecords[0].event=0.5;});
  mutate('abstract',v=>{v.program.actionRecords[0].map=4294967295;});
  mutate('abstract',v=>{v.program.initialPlacements[0]=-1;});
  mutate('abstract',v=>{delete v.bindings;});
  mutate('abstract',v=>{v.artifactVersion=2;});
  mutate('abstract',v=>{v.program.compact.collections=[{id:'source-only',action:0,placements:[0]}];});
  mutate('interpretation',v=>{v.certificates[0].axis=[0,1,0];});
  mutate('controls',v=>{v.kind='twisty-controller';});
  mutate('controls',v=>{delete v.catalog;});
  mutate('controls',v=>{v.catalog.families[0].routes[0].angle=1;});
  mutate('controls',v=>{v.catalog.drivers[0].stops=['a','a'];});
  mutate('presentation',v=>{v.display.version=2;});
  mutate('presentation',v=>{v.display.operations.R.label=0;});
  mutate('realization',v=>{v.requiredCapabilities=['x','x'];});
  mutate('realization',v=>{v.interpretationDigest='bad';});
  for(const path of ['../view.json','/view.json','a/../view.json','a/./view.json','a//view.json','C:\\view.json','https://example/view.json','a/'])
    mutate('realization',v=>{v.payload.path=path;});
  const vectors = await read('docs/contracts/hash-vectors.json');
  for (const vector of vectors) {
    const canonical = canonicalIntegerJSON(vector.value);
    assert.equal(canonical, vector.canonical);
    assert.equal(createHash('sha256').update(canonical).digest('hex'), vector.sha256);
    ++checks;
  }
  for (const bad of [0.1, -1, Infinity, 4294967295, '\ud800', { value: undefined }]) {
    assert.throws(() => canonicalIntegerJSON(bad)); ++checks;
  }
  return checks;
}
if (process.argv[1] && pathToFileURL(resolve(process.argv[1])).href === import.meta.url)
  console.log(`${await checkWireContracts()} current wire-schema/hash conformance checks passed (no runtime loader).`);
