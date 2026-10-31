// Draft structure and canonical-byte conformance only; this is not a loader.
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

export async function checkDraftContracts() {
  const read = file => readFile(new URL(`../../${file}`, import.meta.url), 'utf8').then(JSON.parse);
  const ajv = new Ajv({ strict: false, allErrors: true });
  ajv.addSchema(await read('schemas/compact-v2.schema.json'));
  for (const name of ['common', 'abstract-artifact', 'interpretation', 'controller', 'realization'])
    ajv.addSchema(await read(`docs/contracts/${name}.schema.json`));
  const zero = '0'.repeat(64);
  const abstract = {
    kind: 'twisty-abstract-artifact', artifactVersion: 1, requiredCapabilities: [], executionDigest: zero,
    program: { predicateVersion: 1, transportVersion: 1, pieceCount: 1, placementCount: 2,
      initialPlacements: [0], finiteSets: [[1]], predicateNodes: [{ op: 'IN', bank: 'choice', input: 0, set: 0 }], childReferences: [], invariantRoot: 1, goalRoot: 1,
      actionRecords: [{ piece: 0, map: 0, selectionRoot: 2, event: 0 }],
      requestPrograms: [{ choiceSizes: [2], firstAction: 0, actionCount: 1, relationRoot: 1 }],
      compact: { groups: [], actions: [], collections: [], spaces: [], positionMaps: [], bindings: [], families: [],
        maps: [{ kind: 'explicit', rows: [[0, 1], [1, 0]] }] } },
    bindings: { puzzleId: 'toggle', pieces: ['piece'], placements: ['a', 'b'], events: ['turn'],
      choiceSlots: ['slot'], choiceValues: [['idle', 'turn']], requests: [{ id: 'turn', defaultProfile: 0,
        profiles: [{ id: 'default', choices: [1], whenRoot: 1 }] }] },
    session: { requestInverses: [0], choiceInverses: [[0, 1]] },
  };
  const interpretation = { kind: 'twisty-interpretation', contractVersion: 1, executionDigest: zero, interpretationDigest: zero,
    domains: [{ id: 'domain', ports: ['p'] }], resources: ['a', 'b'],
    pieces: [{ piece: 0, domain: 0, portLabels: [{ port: 'p', material: 'red' }] }],
    placements: [0, 1].map(i => ({ placement: i, domain: 0, resources: [i], ports: [{ id: 'p', resource: i, attachment: 'out' }] })),
    certificates: [{ id: 'turn', domain: 0, event: 0, transport: 0, actions: [0], unavailablePlacements: [] }] };
  const controller = { kind: 'twisty-controller', contractVersion: 1, executionDigest: zero, controlDigest: zero,
    movements: [{ slot: 0, value: 1, actions: [0] }],
    drivers: [{ id: 'driver', piece: 0, stops: ['a', 'b'], reader: [{ placement: 0, stop: 0 }, { placement: 1, stop: 1 }] }],
    families: [{ id: 'family', drivers: [0], routes: [0, 1].map(i => ({ from: [i], to: [1 - i], request: 0, choices: [1], pathId: 'toggle', preferred: true })) }] };
  const realization = { kind: 'twisty-realization', contractVersion: 1, id: 'view', interpretationDigest: zero, realizationDigest: zero,
    requiredCapabilities: [], backend: { id: 'test-only', version: 1 }, payload: { path: 'assets/view.json', sha256: zero },
    eventTracks: [{ certificate: 0, pathId: 'toggle' }] };
  const examples = { 'abstract-artifact': abstract, interpretation, controller, realization };
  let checks = 0;
  function validate(name, value, expected = true) {
    const validator = ajv.getSchema(`urn:twisty:draft:${name}:1`);
    assert.ok(validator, name);
    assert.equal(validator(value), expected, `${name}: ${ajv.errorsText(validator.errors)}`);
    ++checks;
  }
  for (const [name, value] of Object.entries(examples)) validate(name, value);
  const bare = structuredClone(abstract); delete bare.bindings; delete bare.session;
  validate('abstract-artifact', bare); // A core program requires no inverse or geometry.
  const mutate = (name, edit) => { const value = structuredClone(examples[name]); edit(value); validate(name, value, false); };
  mutate('abstract-artifact', v => { v.source = {}; });
  mutate('abstract-artifact', v => { v.program.coordinates = []; });
  mutate('abstract-artifact', v => { v.program.actionRecords[0].event = 0.5; });
  mutate('abstract-artifact', v => { v.program.actionRecords[0].map = 4294967295; });
  mutate('abstract-artifact', v => { v.program.initialPlacements = [-1]; });
  mutate('abstract-artifact', v => { delete v.bindings; });
  mutate('abstract-artifact', v => { v.artifactVersion = 2; });
  mutate('abstract-artifact', v => { v.program.compact.collections = [{ id: 'source-only', action: 0, placements: [0] }]; });
  mutate('interpretation', v => { v.certificates[0].axis = [0, 1, 0]; });
  mutate('controller', v => { v.families[0].routes[0].angle = 1; });
  mutate('controller', v => { v.drivers[0].stops = ['a', 'a']; });
  mutate('realization', v => { v.requiredCapabilities = ['x', 'x']; });
  mutate('realization', v => { v.interpretationDigest = 'bad'; });
  for (const path of ['../view.json', '/view.json', 'a/../view.json', 'a/./view.json', 'a//view.json', 'C:\\view.json', 'https://example/view.json', 'a/'])
    mutate('realization', v => { v.payload.path = path; });
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
  console.log(`${await checkDraftContracts()} draft structure/hash conformance checks passed (no runtime loader).`);
