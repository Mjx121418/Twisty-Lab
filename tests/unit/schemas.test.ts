import Ajv from 'ajv';
import { readFileSync } from 'node:fs';
import { expect, it } from 'vitest';
import createModule from '../../kernel/generated/twisty.mjs';

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
  for (const kind of ['euclidean', 'port-diagram']) {
    check('realization', JSON.parse(readFileSync(`packages/bagua/bagua-${kind}.json`, 'utf8')));
  }
  const bagua = new module.Session(baguaText);
  try {
    check('state', JSON.parse(bagua.stateJSON()));
    expect(JSON.parse(bagua.runJSON("U+ R' L' D2 R L U-", 'transactional', '0')).status).toBe('Committed');
    check('session', JSON.parse(bagua.saveJSON()));
  } finally { bagua.delete(); }
  check('fixture', JSON.parse(readFileSync('tests/fixtures/core.json', 'utf8')));
});
