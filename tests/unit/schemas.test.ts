import Ajv from 'ajv';
import { readFileSync } from 'node:fs';
import { expect, it } from 'vitest';
import createModule from '../../kernel/generated/twisty.mjs';

it('shares immutable definitions while keeping histories and ownership independent', async () => {
  const module = await createModule();
  const original = new module.Session(readFileSync('packages/cube3/package.json', 'utf8'));
  const origin = original.stateJSON();
  expect(JSON.parse(original.executeJSON('{"operation":"U","parameters":{}}', '0')).status).toBe('Committed');
  const fresh = module.createSessionFromDefinition(original);
  if (!fresh) throw new Error('Shared session factory returned no handle.');
  try {
    expect(fresh.stateJSON()).toBe(origin);
    expect(fresh.revision()).toBe('0');
    expect(JSON.parse(fresh.snapshotJSON()).history).toEqual([]);
    original.delete();
    expect(JSON.parse(fresh.executeJSON('{"operation":"F","parameters":{}}', '0')).status).toBe('Committed');
    expect(fresh.stateJSON()).not.toBe(origin);
  } finally { fresh.delete(); }
});

it('validates the experimental relational AST and compiles the example', async () => {
  const ajv = new Ajv({ allErrors: true, strict: false });
  ajv.addSchema(JSON.parse(readFileSync('schemas/compact.schema.json','utf8')));
  ajv.addSchema(JSON.parse(readFileSync('schemas/compact-v2.schema.json','utf8')));
  ajv.addSchema(JSON.parse(readFileSync('schemas/puzzle-v2.schema.json','utf8')));
  ajv.addSchema(JSON.parse(readFileSync('schemas/puzzle-v3.schema.json','utf8')));
  const validateV1 = ajv.compile(JSON.parse(readFileSync('schemas/puzzle.schema.json', 'utf8')));
  const validate = (value: any) => value.languageVersion >= 2 ? ajv.getSchema(`urn:twisty:schema:relational-puzzle:${value.languageVersion}`)!(value) : validateV1(value);
  // Current schemas must resolve without registering the retired definition schema.
  ajv.addSchema(JSON.parse(readFileSync('schemas/state.schema.json', 'utf8')));
  const validatePackage = ajv.compile(JSON.parse(readFileSync('schemas/package.schema.json', 'utf8')));
  const validateSession = ajv.compile(JSON.parse(readFileSync('schemas/session-v2.schema.json', 'utf8')));
  const text = readFileSync('examples/language/coupled.pzl.json', 'utf8');
  expect(validate(JSON.parse(text)), JSON.stringify(ajv.errors)).toBe(true);
  const malformed = JSON.parse(text);
  malformed.requests[0].relation = { unregisteredGuard: true };
  expect(validate(malformed)).toBe(false);
  const module = await createModule();
  expect(JSON.parse(module.compilePuzzleJSON(text)).status).toBe('Compiled');
  for (const [name, bandaged] of [['cube3', false], ['bandaged', true]] as const) {
    const source = JSON.parse(readFileSync(`packages/${name}/puzzle.pzl.json`, 'utf8'));
    const catalog = JSON.parse(readFileSync(`packages/${name}/bindings.json`, 'utf8'));
    expect(validate(source), JSON.stringify(ajv.errors)).toBe(true);
    expect(JSON.parse(module.cubeModuleJSON(bandaged))).toEqual({ source, catalog });
    expect(JSON.parse(module.compilePuzzleJSON(JSON.stringify(source))).status).toBe('Compiled');
    const packageText = readFileSync(`packages/${name}/package.json`, 'utf8');
    expect(validatePackage(JSON.parse(packageText)), JSON.stringify(validatePackage.errors)).toBe(true);
    const session = new module.Session(packageText);
    try {
      expect(JSON.parse(session.runJSON('U R', 'transactional', '0')).status).toBe('Committed');
      const saved = JSON.parse(session.saveJSON());
      expect(validateSession(saved), JSON.stringify(validateSession.errors)).toBe(true);
      delete saved.logical.requests[0].choices;
      expect(validateSession(saved)).toBe(false);
    } finally { session.delete(); }
  }
  const validateGear = ajv.compile(JSON.parse(readFileSync('schemas/gear-source.schema.json', 'utf8')));
  for (const name of ['gear', 'gear-extreme', 'gear-plain']) {
    const wrapper = JSON.parse(readFileSync(`packages/${name}/source.json`, 'utf8'));
    expect(validateGear(wrapper), JSON.stringify(validateGear.errors)).toBe(true);
    const packageDocument = JSON.parse(readFileSync(`packages/${name}/package.json`, 'utf8'));
    expect(validatePackage(packageDocument), JSON.stringify(validatePackage.errors)).toBe(true);
    expect(validate(packageDocument.source), JSON.stringify(ajv.errors)).toBe(true);
    expect(JSON.parse(module.gearModuleJSON(JSON.stringify(wrapper)))).toEqual({ source: packageDocument.source, catalog: packageDocument.catalog });
    const session = new module.Session(JSON.stringify(wrapper));
    try {
      const operation = name === 'gear' ? 'R' : name === 'gear-extreme' ? 'Uq' : 'Rq';
      const result = JSON.parse(session.runJSON(operation, 'transactional', '0'));
      expect(result.status).toBe('Committed');
      expect(result.transitions[0].pieceActions.every((a: { event: string }) => a.event.endsWith('.motion'))).toBe(true);
      expect(validateSession(JSON.parse(session.saveJSON())), JSON.stringify(validateSession.errors)).toBe(true);
    } finally { session.delete(); }
  }
  const malformedGear = JSON.parse(readFileSync('packages/gear/source.json', 'utf8'));
  malformedGear.gearedEdges = ['UF', 'UF'];
  expect(validateGear(malformedGear)).toBe(false);
  expect(JSON.parse(module.compileExecutionJSON(JSON.stringify(malformedGear))).status).toBe('Invalid');
  const helicopter = readFileSync('packages/helicopter/package.json', 'utf8');
  const packageDocument = JSON.parse(helicopter);
  expect(validatePackage(packageDocument), JSON.stringify(validatePackage.errors)).toBe(true);
  expect(validate(packageDocument.source), JSON.stringify(ajv.errors)).toBe(true);
  const session = new module.Session(helicopter);
  try {
    const origin = JSON.parse(session.snapshotJSON());
    expect(origin.candidateRequests).toHaveLength(60);
    expect(JSON.parse(session.runJSON('UF_ab UL_af', 'transactional', '0')).status).toBe('Committed');
    const saved = JSON.parse(session.saveJSON());
    expect(validateSession(saved), JSON.stringify(validateSession.errors)).toBe(true);
    const definition = JSON.parse(session.definitionJSON());
    expect(definition.pieceTypes).toHaveLength(3);
    for (const kind of ['euclidean', 'port-diagram', 'spherical']) {
      const realization = JSON.parse(readFileSync(`packages/helicopter/helicopter-${kind}.json`, 'utf8'));
      expect(realization.compatibleDefinitionDigest).toBe(definition.definitionDigest);
    }
  } finally { session.delete(); }
  const baguaPackage = JSON.parse(readFileSync('packages/bagua/package.json', 'utf8'));
  expect(validatePackage(baguaPackage), JSON.stringify(validatePackage.errors)).toBe(true);
  expect(validate(baguaPackage.source), JSON.stringify(ajv.errors)).toBe(true);

});

it('validates every shipped realization, state, fixture, and generated save', async () => {
  const ajv = new Ajv({ allErrors: true, strict: false });
  for (const name of ['state', 'realization', 'fixture']) {
    ajv.addSchema(JSON.parse(readFileSync(`schemas/${name}.schema.json`, 'utf8')));
  }
  ajv.addSchema(JSON.parse(readFileSync('schemas/session-v2.schema.json', 'utf8')));
  const check = (schema: string, value: unknown): void => {
    const valid = ajv.getSchema(schema)!;
    expect(valid(value), JSON.stringify(valid.errors)).toBe(true);
  };
  const module = await createModule();
  const inventory = JSON.parse(readFileSync('scripts/puzzle-inventory.json', 'utf8'));
  for (const puzzle of inventory.puzzles) {
    for (const file of puzzle.realizations) {
      check('urn:twisty:schema:realization:1', JSON.parse(readFileSync(`packages/${puzzle.id}/${file}`, 'utf8')));
    }
    const session = new module.Session(readFileSync(`packages/${puzzle.id}/package.json`, 'utf8'));
    try {
      check('urn:twisty:schema:state:1', JSON.parse(session.stateJSON()));
      const operation = JSON.parse(session.snapshotJSON()).legalRequests[0].operation;
      expect(JSON.parse(session.executeJSON(JSON.stringify({ operation, parameters: {} }), '0')).status).toBe('Committed');
      check('urn:twisty:schema:session:2', JSON.parse(session.saveJSON()));
    } finally { session.delete(); }
  }
  check('urn:twisty:schema:fixture:1', JSON.parse(readFileSync('tests/fixtures/core.json', 'utf8')));
  for (const kind of ['cube-prototypes', 'gear-cube-prototypes', 'finite-definition']) {
    const rejected = JSON.parse(module.compileExecutionJSON(JSON.stringify({ kind, schemaVersion: 1 })));
    expect(rejected.diagnostics[0].reasonCode).toBe('definition.unsupported');
  }
});
