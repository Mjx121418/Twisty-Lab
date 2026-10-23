import assert from 'node:assert/strict';
import Ajv from 'ajv';
import { cp, mkdir, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const nativeBuild = process.argv[2] ?? 'build/native';
const temporary = await mkdtemp(join(tmpdir(), 'twisty-kernel-'));
try {
  // Both consumers run outside the source tree using copied/installed artifacts.
  const installed = join(temporary, 'native');
  execFileSync('cmake', ['--install', nativeBuild, '--prefix', installed], { cwd: root, stdio: 'inherit' });
  const headers = await readdir(join(installed, 'include/twisty'));
  assert.ok(headers.includes('catalog.hpp'));
  assert.ok(headers.includes('core.hpp') && headers.includes('completion.hpp'));
  assert.ok(!headers.includes('flat_core.hpp') && !headers.includes('compiler.hpp'));
  assert.ok(!headers.some((name) => name.startsWith('relational_')));
  assert.match(await readFile(join(installed, 'include/twisty/core.hpp'), 'utf8'), /namespace twisty::core/);
  assert.match(await readFile(join(installed, 'include/twisty/authoring/compiler.hpp'), 'utf8'), /namespace twisty::language/);
  const exported = await readFile(join(installed, 'lib/cmake/TwistyKernel/TwistyKernelTargets.cmake'), 'utf8');
  assert.match(exported, /Twisty::core/);
  assert.match(exported, /Twisty::language/);
  assert.ok(!/Twisty::(?:flat_core|relational|compiler)|legacy_test_reference/.test(exported));
  const consumer = join(temporary, 'consumer');
  await cp(new URL('../examples/native/', import.meta.url), consumer, { recursive: true });
  execFileSync('cmake', ['-S', consumer, '-B', join(consumer, 'build'), '-G', 'Ninja', `-DCMAKE_PREFIX_PATH=${installed}`], { stdio: 'inherit' });
  execFileSync('cmake', ['--build', join(consumer, 'build'), '--parallel', '1'], { stdio: 'inherit' });
  const javascript = join(temporary, 'javascript');
  await cp(new URL('../build/kernel/', import.meta.url), javascript, { recursive: true });
  const schemaDirectory = join(javascript, 'schemas');
  const schemas = new Ajv({ strict: false, allErrors: true });
  const schemaIDs = [];
  for (const file of await readdir(schemaDirectory)) {
    const schema = JSON.parse(await readFile(join(schemaDirectory, file), 'utf8'));
    assert.ok(!schema.$id.includes('schema:definition:'), 'A retired definition schema was packaged.');
    schemas.addSchema(schema);
    schemaIDs.push(schema.$id);
  }
  for (const id of schemaIDs) assert.ok(schemas.getSchema(id), `Packaged schema cannot resolve: ${id}`);
  const { createKernel, KernelSession, GeometryInstance, Puzzle, createCubeSource, createGearSource } = await import(pathToFileURL(join(javascript, 'index.js')).href);
  const module = await createKernel();
  const info = JSON.parse(module.kernelInfoJSON());
  const manifest = JSON.parse(await readFile(join(javascript, 'package.json'), 'utf8'));
  assert.equal(info.apiVersion, 4); assert.equal(info.frameBufferVersion, 1);
  assert.equal(info.geometryAPIVersion, 1); assert.equal(info.poseBufferVersion, 1);
  assert.equal(info.version, manifest.version);
  assert.deepEqual(info.ruleModules, ['relational@1']);
  assert.deepEqual(info.sessionSchemaVersions, [2]);
  assert.equal(typeof module.compileJSON, 'undefined');
  assert.equal(typeof module.Session.prototype.convertLegacyJSON, 'undefined');
  assert.ok(info.realizationKinds.includes('articulated-spherical'));
  assert.equal(info.languageAPIVersion, 1);
  assert.equal(typeof module.RelationalPuzzle, 'undefined');
  assert.equal(typeof module.compileRelationalJSON, 'undefined');
  assert.deepEqual(info.authoringLibraries, ['cube3', 'bandaged-uf-ufr', 'gear-family']);
  const languageConsumer = join(temporary, 'language-consumer');
  await cp(join(javascript, 'examples/language'), languageConsumer, { recursive: true });
  execFileSync('cmake', ['-S', languageConsumer, '-B', join(languageConsumer, 'build'), '-G', 'Ninja',
    `-DCMAKE_PREFIX_PATH=${installed}`], { stdio: 'inherit' });
  execFileSync('cmake', ['--build', join(languageConsumer, 'build'), '--parallel', '1'], { stdio: 'inherit' });
  const languageSource = join(languageConsumer, 'coupled.pzl.json');
  const puzzle = new Puzzle(module, await readFile(languageSource, 'utf8'));
  try {
    const executable = join(languageConsumer, 'build/twisty_language');
    assert.deepEqual(JSON.parse(execFileSync(executable, [languageSource, '--inspect'], { encoding: 'utf8' })), puzzle.preview);
    const queries = [
      { op: 'bind', state: [0, 2, 4], request: 'fixed' },
      { op: 'bind', state: [0, 2, 4], request: 'synchronize' },
      { op: 'plan', state: [0, 2, 4], request: 'fixed', choices: ['turn', 'turn', 'idle'] },
      { op: 'plan', state: [0, 2, 4], request: 'alternative', choices: ['turn', 'turn', 'idle'] },
      { op: 'discover', state: [0, 2, 4], request: 'none', candidateLimit: 1, resultLimit: 10 },
    ];
    const native = execFileSync(executable, [languageSource], { encoding: 'utf8',
      input: queries.map((q) => JSON.stringify(q)).join('\n') + '\n' }).trim().split('\n').map((line) => JSON.parse(line));
    assert.deepEqual(queries.map((q) => puzzle.query(q)), native);
    console.log('Installed native and packaged WASM relational consumers agree.');
    for (const [name, bandaged] of [['cube3', false], ['bandaged', true]]) {
      const data = join(javascript, 'examples/canvas/data', name);
      const source = JSON.parse(await readFile(join(data, 'puzzle.pzl.json'), 'utf8'));
      const catalog = JSON.parse(await readFile(join(data, 'bindings.json'), 'utf8'));
      assert.deepEqual(createCubeSource(module, bandaged), { source, catalog });
      assert.deepEqual(JSON.parse(execFileSync(executable, [bandaged ? '--bandaged-module' : '--cube-module'], { encoding: 'utf8' })), { source, catalog });
      const cube = new Puzzle(module, JSON.stringify(source));
      try {
        const initial = cube.preview.program.initialPlacements;
        const selection = cube.query({ op: 'bind', state: initial, request: 'U' });
        assert.equal(selection.status, 'Selected');
        const turn = { op: 'plan', state: initial, request: 'U', choices: selection.choices };
        const expected = cube.query(turn);
        assert.equal(expected.status, 'Permitted');
        assert.equal(expected.transition.actions.length, bandaged ? 8 : 9);
        const actual = JSON.parse(execFileSync(executable, [join(data, 'puzzle.pzl.json')], {
          encoding: 'utf8', input: JSON.stringify(turn) + '\n' }));
        assert.deepEqual(actual, expected);
      } finally { cube.dispose(); }
    }
    console.log('Installed native and packaged WASM cube/bandage authoring libraries agree.');
    for (const name of ['gear', 'gear-extreme', 'gear-plain']) {
      const data = join(javascript, 'examples/canvas/data', name);
      const wrapper = JSON.parse(await readFile(join(data, 'source.json'), 'utf8'));
      const expected = { source: JSON.parse(await readFile(join(data, 'puzzle.pzl.json'), 'utf8')),
        catalog: JSON.parse(await readFile(join(data, 'bindings.json'), 'utf8')) };
      assert.deepEqual(createGearSource(module, wrapper), expected);
      assert.deepEqual(JSON.parse(execFileSync(executable, [join(data, 'source.json'), '--gear-module'], { encoding: 'utf8', maxBuffer: 4 * 1024 * 1024 })), expected);
    }

  } finally { puzzle.dispose(); }
  const customDirectory = join(javascript, 'examples/canvas/data/gear-custom');
  await mkdir(customDirectory, { recursive: true });
  const custom = JSON.parse(module.compileExecutionJSON(JSON.stringify({ formatVersion: 1,
    kind: 'relational-gear-prototypes', puzzleId: 'gear-custom-experimental-v1', gearedEdges: ['UF', 'UR'] })));
  assert.equal(custom.status, 'Compiled');
  const customRealization = JSON.parse(await readFile(join(javascript, 'examples/canvas/data/gear-extreme/gear-extreme-supplied-euclidean.json'), 'utf8'));
  customRealization.compatibleDefinitionDigest = custom.definition.definitionDigest;
  await writeFile(join(customDirectory, 'package.json'), JSON.stringify(custom.definition));
  await writeFile(join(customDirectory, 'gear-custom-euclidean.json'), JSON.stringify(customRealization));
  const customSphere = JSON.parse(await readFile(join(javascript, 'examples/canvas/data/gear/gear-spherical.json'), 'utf8'));
  customSphere.compatibleDefinitionDigest = custom.definition.definitionDigest;
  await writeFile(join(customDirectory, 'gear-custom-spherical.json'), JSON.stringify(customSphere));
  for (const [puzzle, algorithm, firstMove, kind] of [
    ['cube3', "R U R' U'", 'U', 'cube-port-diagram'],
    ['bandaged', 'U R', 'U', 'cube-port-diagram'],
    ['helicopter', 'UF_ab UL_af', 'UF_ab', 'helicopter-port-diagram'],
    ['helicopter', 'UF_ab UL_af', 'UF_ab', 'helicopter-spherical'],
    ['cube3', 'U R F D L B', 'U', 'cube-spherical'],
    ['bandaged', 'U R', 'U', 'cube-spherical'],
    ['bagua', "U+ R' L' D2 R L U-", 'U+', 'bagua-port-diagram'],
    ['bagua', '[[U+ R2:U+],[R R+ L-:D2]]', 'U+', 'bagua-euclidean'],
    ['bagua', "U+ R F- (U+ R F-)'", 'U+', 'bagua-spherical'],
    ['gear', "R U F (R U F)'", 'R', 'gear-normal-euclidean'],
    ['gear', "F U R (F U R)'", 'F', 'gear-spherical'],
    ['gear-extreme', "Uq R Fq (Uq R Fq)'", 'Uq', 'gear-extreme-spherical'],
    ['gear-plain', "Rq Uq Fq (Rq Uq Fq)'", 'Rq', 'gear-plain-spherical'],
    ['gear-extreme', "Uq R Fq (Uq R Fq)'", 'Uq', 'gear-extreme-supplied-euclidean'],
    ['gear-plain', "Rq Uq Fq (Rq Uq Fq)'", 'Rq', 'gear-plain-supplied-euclidean'],
    ['gear-extreme', "Uq R Fq (Uq R Fq)'", 'Uq', 'gear-extreme-port-diagram'],
    ['gear-custom', "F U R Dq L Bq (F U R Dq L Bq)'", 'F', 'gear-custom-euclidean'],
    ['gear-custom', "F U R Dq L Bq (F U R Dq L Bq)'", 'F', 'gear-custom-spherical'],
    ['flat-torus', "A B C D (A B C D)'", 'A', 'flat-quotient'],
    ['flat-klein-bottle', "A B C D (A B C D)'", 'A', 'flat-quotient'],
    ['flat-hex-torus', "A B C2 D3 (A B C2 D3)'", 'A', 'flat-quotient'],
    ['flat-hex-klein-bottle', "A B C2 D3 (A B C2 D3)'", 'A', 'flat-quotient'],
    ['flat-pillowcase', "A B C D (A B C D)'", 'A', 'flat-quotient'],
    ['flat-pillowcase-6', "A B E F C D (A B E F C D)'", 'E', 'flat-quotient'],
    ['flat-333', "A B C (A B C)'", 'A', 'flat-quotient'],
    ['flat-333-6', "A D E2 F3 B C (A D E2 F3 B C)'", 'D', 'flat-quotient'],
  ]) {
    const directory = join(javascript, 'examples/canvas/data', puzzle);
    const shipped = await readdir(directory);
    assert.ok(!shipped.includes('definition.json') && !shipped.some((name) => name.startsWith('relational')));
    const sourcePath = join(directory, 'package.json');
    const source = await readFile(sourcePath, 'utf8');
    const realizationPath = join(directory, kind === 'flat-quotient' ? 'flat-quotient.json' : `${kind}.json`);
    const realization = await readFile(realizationPath, 'utf8');
    const session = new KernelSession(module, source);
    const geometry = new GeometryInstance(module, session.native, realization);
    assert.ok(geometry);
    try {
      if (kind === 'flat-quotient') {
        const scene = geometry.scene;
        const pillowcase=puzzle.startsWith('flat-pillowcase');
        const orbifold333=puzzle.startsWith('flat-333');
        assert.equal(scene.flatSurface.deckGenerators.length, pillowcase||orbifold333 ? 3 : 2);
        const parts=orbifold333 ? puzzle.endsWith('-6') ? 122 : 23 : pillowcase ? puzzle.endsWith('-6') ? 122 : 64 : puzzle.startsWith('flat-hex-') ? 100 : 116;
        assert.equal(scene.visualParts.length, parts, 'Interpreter emits each physical part once.');
        assert.ok(!('images' in scene.flatSurface), 'Renderer owns displayed copies.');
      }
      if (puzzle.startsWith('gear')) {
        const scene = geometry.scene;
        const positions = Array.from(geometry.definition.positions());
        const normals = Array.from(geometry.definition.normals());
        for (const asset of scene.meshAssets.filter((asset) => asset.id === 'core-ball'
          || (kind.endsWith('-spherical') && asset.id.includes('port-')))) {
          const end = asset.positionOffset + asset.vertexCount * 3;
          for (let v = asset.positionOffset; v < end; v += 3) {
            const radius = Math.hypot(...positions.slice(v, v + 3));
            for (let axis = 0; axis < 3; ++axis)
              assert.ok(Math.abs(normals[v + axis] - positions[v + axis] / radius) < 1e-6,
                'Independent clients receive smooth ball and spherical sticker normals.');
          }
        }
      }
      const origin = session.snapshot();
      assert.equal(session.validateState(session.stateText()).status, 'Valid');
      assert.equal(session.validateState('{}').status, 'Invalid');
      const request = { operation: firstMove, parameters: {} };
      const preview = session.plan(request);
      assert.equal(preview.status, 'Legal');
      assert.deepEqual(session.snapshot(), origin, 'Planning must not commit state/history.');
      assert.equal(session.plan({ operation: firstMove, parameters: { unsupported: 1 } }).status, 'Invalid');
      if (puzzle === 'bandaged') assert.equal(session.plan({ operation: 'R', parameters: {} }).reasonCode, 'relation.rejected');
      if (puzzle === 'gear-custom') {
        assert.deepEqual(session.plan({ operation: 'R', parameters: {} }).transition.afterState,
          session.plan({ operation: 'L', parameters: {} }).transition.afterState);
        assert.equal(session.plan({ operation: 'U', parameters: {} }).reasonCode, 'relation.rejected');
        assert.equal(session.plan({ operation: 'Uq', parameters: {} }).status, 'Legal');
        assert.equal(session.plan({ operation: 'Fq', parameters: {} }).reasonCode, 'relation.rejected');
      }
      if (puzzle === 'helicopter') {
        assert.equal(session.plan({ operation: 'UR_ad', parameters: {} }, JSON.stringify(preview.transition.afterState)).reasonCode, 'relation.rejected');
        assert.deepEqual(session.snapshot(), origin, 'Planning a different source state remains pure.');
      }
      const result = session.run(algorithm, 'transactional', origin.revision);
      assert.equal(result.status, 'Committed');
      for (const record of result.transitionRecords) {
        assert.ok(geometry.prepare(record) >= 0);
        geometry.evaluate(0.5); geometry.evaluate(1);
      }
      const reference = JSON.parse(execFileSync(join(consumer, 'build/custom_kernel_consumer'), [sourcePath, realizationPath, algorithm], { encoding: 'utf8' }));
      assert.deepEqual(session.snapshot(), reference.snapshot);
      assert.deepEqual(geometry.scene, reference.scene);
      assert.deepEqual(info, reference.kernel);
      const matrices = Array.from(geometry.matrices);
      assert.equal(matrices.length, reference.transforms.length);
      matrices.forEach((value, i) => assert.ok(Math.abs(value - reference.transforms[i]) < 1e-6));
      assert.equal(session.move(firstMove, origin.revision).status, 'StaleRevision');
      const saved = session.save({ frontend: 'external-node-consumer' });
      const restored = new KernelSession(module, session);
      try {
        assert.equal(restored.load(saved).status, 'Loaded');
        assert.equal(restored.snapshot().stateDigest, session.snapshot().stateDigest);
      } finally { restored.dispose(); }
      const part = reference.scene.visualParts[0];
      assert.equal(geometry.resolvePart(part.visualPartId).pieceId, part.pieceId);
      console.log(`Portable native/WASM consumers agree: ${puzzle} / ${kind}`);
    } finally { geometry.dispose(); session.dispose(); }
  }
} finally { await rm(temporary, { recursive: true, force: true }); }
