import Ajv from 'ajv';
import { readFileSync } from 'node:fs';
import { expect, it } from 'vitest';
import createModule from '../../kernel/generated/twisty.mjs';

it('shares immutable definitions while keeping histories and ownership independent', async () => {
  const module = await createModule();
  const original = new module.Session(readFileSync('packages/cube3/relational-package.json', 'utf8'));
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
  const validate = ajv.compile(JSON.parse(readFileSync('schemas/relational-puzzle.schema.json', 'utf8')));
  ajv.addSchema(JSON.parse(readFileSync('schemas/definition.schema.json', 'utf8')));
  const validatePackage = ajv.compile(JSON.parse(readFileSync('schemas/relational-package.schema.json', 'utf8')));
  const validateSession = ajv.compile(JSON.parse(readFileSync('schemas/session-v2.schema.json', 'utf8')));
  const text = readFileSync('examples/relational/coupled.pzl.json', 'utf8');
  expect(validate(JSON.parse(text)), JSON.stringify(validate.errors)).toBe(true);
  const malformed = JSON.parse(text);
  malformed.requests[0].relation = { unregisteredGuard: true };
  expect(validate(malformed)).toBe(false);
  const module = await createModule();
  expect(JSON.parse(module.compileRelationalJSON(text)).status).toBe('Compiled');
  for (const [name, bandaged] of [['cube3', false], ['bandaged', true]] as const) {
    const source = JSON.parse(readFileSync(`packages/${name}/relational.pzl.json`, 'utf8'));
    const catalog = JSON.parse(readFileSync(`packages/${name}/relational-bindings.json`, 'utf8'));
    expect(validate(source), JSON.stringify(validate.errors)).toBe(true);
    expect(JSON.parse(module.relationalCubeModuleJSON(bandaged))).toEqual({ source, catalog });
    expect(JSON.parse(module.compileRelationalJSON(JSON.stringify(source))).status).toBe('Compiled');
    const packageText = readFileSync(`packages/${name}/relational-package.json`, 'utf8');
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
  const validateGear = ajv.compile(JSON.parse(readFileSync('schemas/relational-gear-source.schema.json', 'utf8')));
  for (const name of ['gear', 'gear-extreme', 'gear-plain']) {
    const wrapper = JSON.parse(readFileSync(`packages/${name}/relational-source.json`, 'utf8'));
    expect(validateGear(wrapper), JSON.stringify(validateGear.errors)).toBe(true);
    const packageDocument = JSON.parse(readFileSync(`packages/${name}/relational-package.json`, 'utf8'));
    expect(validatePackage(packageDocument), JSON.stringify(validatePackage.errors)).toBe(true);
    expect(validate(packageDocument.source), JSON.stringify(validate.errors)).toBe(true);
    expect(JSON.parse(module.relationalGearModuleJSON(JSON.stringify(wrapper)))).toEqual({ source: packageDocument.source, catalog: packageDocument.catalog });
    const session = new module.Session(JSON.stringify(wrapper));
    try {
      const operation = name === 'gear' ? 'R' : name === 'gear-extreme' ? 'Uq' : 'Rq';
      const result = JSON.parse(session.runJSON(operation, 'transactional', '0'));
      expect(result.status).toBe('Committed');
      expect(result.transitions[0].pieceActions.every((a: { event: string }) => a.event.endsWith('.motion'))).toBe(true);
      expect(validateSession(JSON.parse(session.saveJSON())), JSON.stringify(validateSession.errors)).toBe(true);
    } finally { session.delete(); }
  }
  const malformedGear = JSON.parse(readFileSync('packages/gear/relational-source.json', 'utf8'));
  malformedGear.gearedEdges = ['UF', 'UF'];
  expect(validateGear(malformedGear)).toBe(false);
  expect(JSON.parse(module.compileExecutionJSON(JSON.stringify(malformedGear))).status).toBe('Invalid');
  const helicopter = readFileSync('packages/helicopter/relational-package.json', 'utf8');
  const packageDocument = JSON.parse(helicopter);
  expect(validatePackage(packageDocument), JSON.stringify(validatePackage.errors)).toBe(true);
  expect(validate(packageDocument.source), JSON.stringify(validate.errors)).toBe(true);
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
      const realization = JSON.parse(readFileSync(`packages/helicopter/relational-helicopter-${kind}.json`, 'utf8'));
      expect(realization.compatibleDefinitionDigest).toBe(definition.definitionDigest);
    }
  } finally { session.delete(); }
  const baguaPackage = JSON.parse(readFileSync('packages/bagua/relational-package.json', 'utf8'));
  expect(validatePackage(baguaPackage), JSON.stringify(validatePackage.errors)).toBe(true);
  expect(validate(baguaPackage.source), JSON.stringify(validate.errors)).toBe(true);

});

it('validates shipped sources, compiled definitions, realizations, fixtures, and generated saves', async () => {
  const ajv = new Ajv({ allErrors: true, strict: false });
  for (const name of ['definition', 'source', 'state', 'session', 'realization', 'fixture']) {
    ajv.addSchema(JSON.parse(readFileSync(`schemas/${name}.schema.json`, 'utf8')));
  }
  const check = (schema: string, value: unknown): void => {
    const valid = ajv.getSchema(`urn:twisty:schema:${schema}:1`)!;
    expect(valid(value), JSON.stringify(valid.errors)).toBe(true);
  };
  const module = await createModule();
  for (const name of ['cube3', 'bandaged']) {
    check('source', JSON.parse(readFileSync(`packages/${name}/source.json`, 'utf8')));
    check('definition', JSON.parse(readFileSync(`packages/${name}/definition.json`, 'utf8')));
    check('realization', JSON.parse(readFileSync(`packages/${name}/cube-euclidean.json`, 'utf8')));
    check('realization', JSON.parse(readFileSync(`packages/${name}/cube-port-diagram.json`, 'utf8')));
    check('realization', JSON.parse(readFileSync(`packages/${name}/cube-spherical.json`, 'utf8')));
    const session = new module.Session(readFileSync(`packages/${name}/source.json`, 'utf8'));
    try {
      check('state', JSON.parse(session.stateJSON()));
      session.runJSON('U F', 'transactional', '0');
      check('session', JSON.parse(session.saveJSON()));
    } finally { session.delete(); }
  }
  const helicopterText = readFileSync('packages/helicopter/definition.json', 'utf8');
  check('definition', JSON.parse(helicopterText));
  for (const kind of ['euclidean', 'port-diagram', 'spherical']) {
    check('realization', JSON.parse(readFileSync(`packages/helicopter/helicopter-${kind}.json`, 'utf8')));
  }
  const helicopter = new module.Session(helicopterText);
  try {
    check('state', JSON.parse(helicopter.stateJSON()));
    expect(JSON.parse(helicopter.runJSON('UF_ab UL_af', 'transactional', '0')).status).toBe('Committed');
    check('session', JSON.parse(helicopter.saveJSON()));
  } finally { helicopter.delete(); }
  const baguaText = readFileSync('packages/bagua/definition.json', 'utf8');
  check('definition', JSON.parse(baguaText));
  for (const kind of ['euclidean', 'port-diagram', 'spherical']) {
    check('realization', JSON.parse(readFileSync(`packages/bagua/bagua-${kind}.json`, 'utf8')));
  }
  const bagua = new module.Session(baguaText);
  try {
    check('state', JSON.parse(bagua.stateJSON()));
    expect(JSON.parse(bagua.runJSON("U+ R' L' D2 R L U-", 'transactional', '0')).status).toBe('Committed');
    check('session', JSON.parse(bagua.saveJSON()));
  } finally { bagua.delete(); }
  check('fixture', JSON.parse(readFileSync('tests/fixtures/core.json', 'utf8')));
  for (const name of ['gear', 'gear-extreme', 'gear-plain']) {
    check('source', JSON.parse(readFileSync(`packages/${name}/source.json`, 'utf8')));
    check('definition', JSON.parse(readFileSync(`packages/${name}/definition.json`, 'utf8')));
    for (const kind of ['euclidean', 'port-diagram', 'spherical']) check('realization', JSON.parse(readFileSync(`packages/${name}/${name}-${kind}.json`, 'utf8')));
    const text = readFileSync(`packages/${name}/source.json`, 'utf8');
    expect(JSON.parse(module.compileJSON(text)).definition).toEqual(JSON.parse(readFileSync(`packages/${name}/definition.json`, 'utf8')));
  }
  const guarded = JSON.parse(readFileSync('packages/gear/definition.json', 'utf8'));
  guarded.ruleModule = 'finite-placement-relations@1';
  expect(ajv.getSchema('urn:twisty:schema:definition:1')!(guarded)).toBe(false);
});
