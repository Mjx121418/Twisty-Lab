import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const native = resolve(root, process.argv[2] ?? 'build/native/twisty_relational');
const sdk = resolve(root, process.argv[3] ?? 'build/kernel/index.js');
const temporary = await mkdtemp(join(tmpdir(), 'twisty-relational-'));
try {
  const oracle = join(temporary, 'oracle.json');
  execFileSync('python3', [join(root, 'tests/relational_oracle.py'), native,
    join(root, 'examples/relational/coupled.pzl.json'), '--emit', oracle], { stdio: 'inherit' });
  const migrationCases = join(temporary, 'cube-migration.json');
  execFileSync(join(dirname(native), 'cube_relational_tests'), ['--emit', migrationCases], { stdio: 'inherit' });
  const { createKernel, KernelSession, ExperimentalRelationalPuzzle, createRelationalCubeSource } = await import(pathToFileURL(sdk).href);
  const module = await createKernel();
  const info = JSON.parse(module.kernelInfoJSON());
  assert.equal(info.experimentalAPIs.relational, 1);
  assert.deepEqual(info.relationalLibraries, ['cube3', 'bandaged-uf-ufr']);
  assert.equal(JSON.parse(module.compileRelationalJSON('{}')).status, 'Invalid');
  let count = 0;
  for (const suite of JSON.parse(await readFile(oracle, 'utf8'))) {
    const puzzle = new ExperimentalRelationalPuzzle(module, suite.source);
    try {
      assert.deepEqual(puzzle.preview, suite.preview, 'Native/WASM compiler output differs.');
      for (let i = 0; i < suite.queries.length; ++i) {
        assert.deepEqual(puzzle.query(suite.queries[i]), suite.expected[i], JSON.stringify(suite.queries[i]));
        ++count;
      }
    } finally { puzzle.dispose(); }
  }
  let migrationCount = 0;
  for (const suite of JSON.parse(await readFile(migrationCases, 'utf8'))) {
    const bandaged = suite.catalog.puzzleId === 'bandaged-uf-ufr';
    assert.deepEqual(createRelationalCubeSource(module, bandaged), { source: JSON.parse(suite.source), catalog: suite.catalog });
    const puzzle = new ExperimentalRelationalPuzzle(module, suite.source);
    try {
      assert.deepEqual(puzzle.preview, suite.preview);
      for (let i = 0; i < suite.queries.length; ++i) {
        assert.deepEqual(puzzle.query(suite.queries[i]), suite.expected[i], JSON.stringify(suite.queries[i]));
        ++migrationCount;
      }
    } finally { puzzle.dispose(); }
  }
  const source = await readFile(join(root, 'examples/relational/coupled.pzl.json'), 'utf8');
  const puzzle = new ExperimentalRelationalPuzzle(module, source);
  try {
    assert.equal(puzzle.query({ op: 'bind', state: [0, 2, 4], request: 'synchronize' }).status, 'ChoiceRequired');
    assert.deepEqual(puzzle.query({ op: 'bind', state: [0, 2, 4], request: 'fixed' }),
      { status: 'Selected', choices: ['turn', 'turn', 'idle'] });
    assert.equal(puzzle.query({ op: 'discover', state: [0, 2, 4], request: 'none', candidateLimit: 1, resultLimit: 10 }).complete, false);
    assert.equal(puzzle.query({ op: 'discover', state: [0, 2, 4], request: 'none', candidateLimit: 100, resultLimit: 10 }).complete, true);
    assert.equal(puzzle.query({ op: 'plan', state: [0, 2, 4], request: 'fixed', choices: [] }).status, 'Invalid');
  } finally { puzzle.dispose(); }
  for (const name of ['cube3', 'bandaged']) {
    const path = join(root, 'packages', name, 'relational-package.json');
    const text = await readFile(path, 'utf8');
    const session = new KernelSession(module, text);
    const restored = new KernelSession(module, text);
    const legacy = new KernelSession(module, await readFile(join(root, 'packages', name, 'definition.json'), 'utf8'));
    try {
      const algorithm = "U R R' U'";
      const reference = JSON.parse(execFileSync(join(dirname(native), 'twisty'), ['run', '--definition', path,
        '--algorithm', algorithm, '--policy', 'transactional', '--json'], { encoding: 'utf8' }));
      const result = session.run(algorithm, 'transactional');
      assert.equal(result.status, 'Committed');
      assert.deepEqual(result.snapshot, reference.snapshot);
      assert.deepEqual(result.transitions, reference.transitions);
      assert.equal(session.definition.kind, 'relational-definition');
      const saved = JSON.parse(session.save());
      assert.equal(saved.schemaVersion, 2);
      assert.ok(saved.logical.requests.every((r) => Array.isArray(r.choices)));
      assert.equal(restored.load(JSON.stringify(saved)).status, 'Loaded');
      assert.equal(restored.snapshot().stateDigest, session.snapshot().stateDigest);
      assert.equal(session.undo().status, 'Committed');
      assert.equal(session.redo().status, 'Committed');
      assert.equal(session.snapshot().stateDigest, reference.snapshot.stateDigest);
      const before = restored.snapshot();
      delete saved.logical.requests[0].choices;
      assert.equal(restored.load(JSON.stringify(saved)).status, 'Invalid');
      assert.deepEqual(restored.snapshot(), before);
      assert.equal(legacy.run(algorithm, 'transactional').status, 'Committed');
      legacy.undo();
      const conversion = session.convertLegacy(legacy.save({ marker: 'preserved' }), legacy.definitionText);
      assert.equal(conversion.status, 'Converted');
      assert.deepEqual(conversion.session.presentation, { marker: 'preserved' });
      assert.equal(restored.load(JSON.stringify(conversion.session)).status, 'Loaded');
      assert.deepEqual(restored.snapshot().state, legacy.snapshot().state);
      assert.equal(restored.redo().status, 'Committed');
      assert.equal(restored.snapshot().stateDigest, reference.snapshot.stateDigest);
    } finally { session.dispose(); restored.dispose(); legacy.dispose(); }
  }
  console.log('Cube/bandage portable sessions preserve full native/WASM witnesses, replay, and explicit legacy conversion.');
  console.log(`Portable relational API: ${count} WASM results match the independent source oracle and native consumer.`);
  console.log(`Cube/bandage migration: ${migrationCount} WASM results match the current executor's states, legality and full event witnesses.`);
} finally { await rm(temporary, { recursive: true, force: true }); }
