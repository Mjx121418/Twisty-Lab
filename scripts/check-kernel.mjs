import assert from 'node:assert/strict';
import { cp, mkdtemp, readFile, rm } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const temporary = await mkdtemp(join(tmpdir(), 'twisty-kernel-'));
try {
  // Both consumers run outside the source tree using copied/installed artifacts.
  const installed = join(temporary, 'native');
  execFileSync('cmake', ['--install', 'build/native', '--prefix', installed], { cwd: root, stdio: 'inherit' });
  const consumer = join(temporary, 'consumer');
  await cp(new URL('../examples/native/', import.meta.url), consumer, { recursive: true });
  execFileSync('cmake', ['-S', consumer, '-B', join(consumer, 'build'), '-G', 'Ninja', `-DCMAKE_PREFIX_PATH=${installed}`], { stdio: 'inherit' });
  execFileSync('cmake', ['--build', join(consumer, 'build'), '--parallel', '1'], { stdio: 'inherit' });
  const javascript = join(temporary, 'javascript');
  await cp(new URL('../build/kernel/', import.meta.url), javascript, { recursive: true });
  const { createKernel, KernelSession } = await import(pathToFileURL(join(javascript, 'index.js')).href);
  const module = await createKernel();
  const info = JSON.parse(module.kernelInfoJSON());
  const manifest = JSON.parse(await readFile(join(javascript, 'package.json'), 'utf8'));
  assert.equal(info.apiVersion, 1); assert.equal(info.frameBufferVersion, 1);
  assert.equal(info.version, manifest.version);
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
  ]) {
    const directory = join(javascript, 'examples/canvas/data', puzzle);
    const source = await readFile(join(directory, 'definition.json'), 'utf8');
    const realizationPath = join(directory, `${kind}.json`);
    const realization = await readFile(realizationPath, 'utf8');
    const session = new KernelSession(module, source);
    const geometry = module.createGeometry(session.native, realization);
    assert.ok(geometry);
    try {
      const origin = session.snapshot();
      assert.equal(session.validateState(session.stateText()).status, 'Valid');
      assert.equal(session.validateState('{}').status, 'Invalid');
      const request = { operation: firstMove, parameters: {} };
      const preview = session.plan(request);
      assert.equal(preview.status, 'Legal');
      assert.deepEqual(session.snapshot(), origin, 'Planning must not commit state/history.');
      assert.equal(session.plan({ operation: firstMove, parameters: { unsupported: 1 } }).status, 'Invalid');
      if (puzzle === 'bandaged') assert.equal(session.plan({ operation: 'R', parameters: {} }).reasonCode, 'footprint.partial_overlap');
      if (puzzle === 'helicopter') {
        assert.equal(session.plan({ operation: 'UR_ad', parameters: {} }, JSON.stringify(preview.transition.afterState)).reasonCode, 'placement.blocked');
        assert.deepEqual(session.snapshot(), origin, 'Planning a different source state remains pure.');
      }
      const result = session.run(algorithm, 'transactional', origin.revision);
      assert.equal(result.status, 'Committed');
      for (const record of result.transitionRecords) {
        assert.equal(JSON.parse(geometry.prepareAnimationJSON(record)).status, 'Prepared');
        geometry.sample(0.5); geometry.sample(1);
      }
      const reference = JSON.parse(execFileSync(join(consumer, 'build/custom_kernel_consumer'), [join(directory, 'definition.json'), realizationPath, algorithm], { encoding: 'utf8' }));
      assert.deepEqual(session.snapshot(), reference.snapshot);
      assert.deepEqual(JSON.parse(geometry.sceneJSON()), reference.scene);
      assert.deepEqual(info, reference.kernel);
      const matrices = Array.from(geometry.transforms());
      assert.equal(matrices.length, reference.transforms.length);
      matrices.forEach((value, i) => assert.ok(Math.abs(value - reference.transforms[i]) < 1e-6));
      assert.equal(session.move(firstMove, origin.revision).status, 'StaleRevision');
      const saved = session.save({ frontend: 'external-node-consumer' });
      const restored = new KernelSession(module, source);
      try {
        assert.equal(restored.load(saved).status, 'Loaded');
        assert.equal(restored.snapshot().stateDigest, session.snapshot().stateDigest);
      } finally { restored.dispose(); }
      const part = reference.scene.visualParts[0];
      assert.equal(JSON.parse(geometry.bindHitJSON(part.visualPartId)).pieceId, part.pieceId);
      console.log(`Portable native/WASM consumers agree: ${puzzle} / ${kind}`);
    } finally { geometry.delete(); session.dispose(); }
  }
} finally { await rm(temporary, { recursive: true, force: true }); }
