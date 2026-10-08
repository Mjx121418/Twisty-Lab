import Ajv from 'ajv';
import { readFileSync } from 'node:fs';
import { expect, it } from 'vitest';
import createModule from '../../web/generated/twisty.mjs';

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
    const session = new module.Session(readFileSync(`packages/${name}/source.json`, 'utf8'));
    try {
      check('state', JSON.parse(session.stateJSON()));
      session.runJSON('U F', 'transactional', '0');
      check('session', JSON.parse(session.saveJSON()));
    } finally { session.delete(); }
  }
  check('fixture', JSON.parse(readFileSync('tests/fixtures/core.json', 'utf8')));
});
