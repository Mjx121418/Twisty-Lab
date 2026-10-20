import { cp, mkdir, mkdtemp, readFile, rm, unlink, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { afterEach, beforeAll, beforeEach, expect, it } from 'vitest';
import createModule, { type MainModule } from '../../kernel/generated/twisty.mjs';
import {
  artifactPaths, checkFreshness, hashFiles, inputPaths, makeManifest, manifestPath,
  puzzleFiles, readJSON, root, validatePuzzleData, wasmPuzzleLoader,
} from '../../scripts/puzzle-data.mjs';

let base: string;
let runtime: MainModule;
const model = 'relational-cube-spherical.json';
const folder = 'packages/cube3/';
const inventory = {
  formatVersion: 1, inputs: ['scripts/generate.py'], generators: ['generate.py'],
  puzzles: [{ id: 'cube3', realizations: [model], files: [] }],
};
const save = async (path: string, value: unknown): Promise<void> => {
  await writeFile(join(base, path), JSON.stringify(value, null, 2) + '\n');
};
const compatible = async (): Promise<unknown> => validatePuzzleData(base, inventory, wasmPuzzleLoader(runtime));

beforeAll(async () => { runtime = await createModule(); });
beforeEach(async () => {
  base = await mkdtemp(join(tmpdir(), 'twisty-puzzle-data-'));
  for (const directory of ['scripts', folder, 'schemas', 'tests/fixtures'])
    await mkdir(join(base, directory), { recursive: true });
  await writeFile(join(base, 'scripts/generate.py'), 'import helper\n');
  await writeFile(join(base, 'scripts/helper.py'), 'from leaf import value\n');
  await writeFile(join(base, 'scripts/leaf.py'), 'value = 1\n');
  for (const file of puzzleFiles(inventory.puzzles[0]))
    await cp(join(root, folder, file), join(base, folder, file));
  for (const name of ['relational-puzzle', 'relational-package', 'relational-gear-source', 'realization', 'fixture'])
    await cp(join(root, `schemas/${name}.schema.json`), join(base, `schemas/${name}.schema.json`));
  const fixtures = await readJSON(root, 'tests/fixtures/core.json');
  await save('tests/fixtures/core.json', { ...fixtures, cases: [fixtures.cases[0]] });
  await save(manifestPath, await makeManifest(base, inventory));
});
afterEach(async () => { await rm(base, { recursive: true, force: true }); });

it('validates real kernel data without modifying it and produces a deterministic manifest', async () => {
  const paths = [...await inputPaths(base, inventory), ...artifactPaths(inventory), manifestPath, 'tests/fixtures/core.json'];
  const before = await hashFiles(base, paths);
  expect(await makeManifest(base, inventory)).toEqual(await makeManifest(base, inventory));
  await checkFreshness(base, inventory);
  expect(await compatible()).toEqual({ puzzles: 1, realizations: 1 });
  expect(await hashFiles(base, paths)).toEqual(before);
});

it('detects a change in a transitive authoring helper', async () => {
  await writeFile(join(base, 'scripts/leaf.py'), 'value = 2\n');
  await expect(checkFreshness(base, inventory)).rejects.toThrow(/scripts\/leaf.py: authoring input changed/);
});

it('discovers newly imported authoring helpers and detects inventory differences', async () => {
  await writeFile(join(base, 'scripts/extra.py'), 'value = 1\n');
  await writeFile(join(base, 'scripts/generate.py'), 'import helper, extra as other\n');
  expect(await inputPaths(base, inventory)).toContain('scripts/extra.py');
  await expect(checkFreshness(base, inventory)).rejects.toThrow(/scripts\/extra.py: inputs inventory differs/);
});

it('follows transitive native authoring headers', async () => {
  await mkdir(join(base, 'src'));
  await writeFile(join(base, 'src/native.cpp'), '#include "helper.hpp"\n');
  await writeFile(join(base, 'src/helper.hpp'), '#include "nested.hpp"\n');
  await writeFile(join(base, 'src/nested.hpp'), 'constexpr int value = 1;\n');
  const expanded = { ...inventory, inputs: [...inventory.inputs, 'src/native.cpp'] };
  await save(manifestPath, await makeManifest(base, expanded));
  await writeFile(join(base, 'src/nested.hpp'), 'constexpr int value = 2;\n');
  await expect(checkFreshness(base, expanded)).rejects.toThrow(/src\/nested.hpp: authoring input changed/);
});

it('rejects modified artifacts even when their JSON meaning is unchanged', async () => {
  const path = join(base, folder, model);
  await writeFile(path, await readFile(path, 'utf8') + '\n');
  await expect(checkFreshness(base, inventory)).rejects.toThrow(/prebuilt artifact changed/);
});

it('rejects missing artifacts and unlisted manifest entries', async () => {
  const manifest = await readJSON(base, manifestPath);
  manifest.artifacts['packages/unlisted.json'] = '0'.repeat(64);
  await save(manifestPath, manifest);
  await expect(checkFreshness(base, inventory)).rejects.toThrow(/packages\/unlisted.json: artifacts inventory differs/);
  await unlink(join(base, folder, model));
  await expect(checkFreshness(base, inventory)).rejects.toThrow(/relational-cube-spherical.json: cannot hash file/);
});

it('rejects unsupported manifest and puzzle schema versions', async () => {
  const manifest = await readJSON(base, manifestPath);
  await save(manifestPath, { ...manifest, formatVersion: 2 });
  await expect(checkFreshness(base, inventory)).rejects.toThrow(/unsupported manifest format/);
  await save(manifestPath, null);
  await expect(checkFreshness(base, inventory)).rejects.toThrow(/packages\/build-manifest.json: unsupported manifest format/);
  const realization = await readJSON(base, folder + model);
  await save(folder + model, { ...realization, schemaVersion: 2 });
  await expect(compatible()).rejects.toThrow(/relational-cube-spherical.json: schema validation failed/);
});

it('identifies malformed JSON artifact files', async () => {
  await save(folder + model, null);
  await expect(compatible()).rejects.toThrow(/relational-cube-spherical.json: expected a JSON object/);
});

it('rejects source and catalog files that disagree with their package', async () => {
  const source = await readJSON(base, folder + 'relational.pzl.json');
  await save(folder + 'relational.pzl.json', { ...source, id: 'different' });
  await expect(compatible()).rejects.toThrow(/relational.pzl.json: does not match package source/);
  await save(folder + 'relational.pzl.json', source);
  const catalog = await readJSON(base, folder + 'relational-bindings.json');
  await save(folder + 'relational-bindings.json', { ...catalog, puzzleId: 'different' });
  await expect(compatible()).rejects.toThrow(/relational-bindings.json: does not match package catalog/);
});

it('rejects incorrect realization and fixture semantic pins', async () => {
  const realization = await readJSON(base, folder + model);
  await save(folder + model, { ...realization, compatibleDefinitionDigest: '0'.repeat(64) });
  await expect(compatible()).rejects.toThrow(/realization semantic pin differs/);
  await save(folder + model, realization);
  const fixtures = await readJSON(base, 'tests/fixtures/core.json');
  fixtures.cases[0].definitionDigest = '0'.repeat(64);
  await save('tests/fixtures/core.json', fixtures);
  await expect(compatible()).rejects.toThrow(/tests\/fixtures\/core.json: fixture .* incorrect semantic pin/);
});

it('checks geometry with the kernel after schema validation', async () => {
  const realization = await readJSON(base, folder + model);
  // Schema-valid, but too large relative to the radius for the interpreter.
  await save(folder + model, { ...realization, surfaceLift: 1 });
  await expect(compatible()).rejects.toThrow(/relational-cube-spherical.json: kernel rejected the realization/);
});

it('requires explicit fixture updates in the authoring driver', async () => {
  await cp(join(root, 'scripts/compile-packages.py'), join(base, 'scripts/compile-packages.py'));
  await save('scripts/puzzle-inventory.json', { generators: [] });
  await mkdir(join(base, 'build/native'), { recursive: true });
  const digest = (await readJSON(base, folder + model)).compatibleDefinitionDigest;
  // Isolate the fixture-update policy from expensive model generation. The
  // stand-in compiler returns the real cube's current semantic identity.
  await writeFile(join(base, 'build/native/twisty'),
    `#!/usr/bin/env node\nprocess.stdout.write(${JSON.stringify(JSON.stringify({ definition: { definitionDigest: digest } }))});\n`,
    { mode: 0o755 });
  const fixtures = await readJSON(base, 'tests/fixtures/core.json');
  fixtures.cases[0].definitionDigest = '0'.repeat(64);
  await save('tests/fixtures/core.json', fixtures);
  const before = await readFile(join(base, 'tests/fixtures/core.json'), 'utf8');
  const driver = join(base, 'scripts/compile-packages.py');
  expect(() => execFileSync('python3', [driver, '--write'], { stdio: 'pipe' })).toThrow(/different semantic pin/);
  expect(await readFile(join(base, 'tests/fixtures/core.json'), 'utf8')).toBe(before);
  execFileSync('python3', [driver, '--write', '--update-fixtures'], { stdio: 'pipe' });
  expect((await readJSON(base, 'tests/fixtures/core.json')).cases[0].definitionDigest).toBe(digest);
  expect(await compatible()).toEqual({ puzzles: 1, realizations: 1 });
});
