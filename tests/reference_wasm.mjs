// Frozen format-2 fingerprints captured with the 20fcdce native executor.
import assert from 'node:assert/strict';
import { createReadStream } from 'node:fs';
import { createInterface } from 'node:readline';
import { createGunzip } from 'node:zlib';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

export async function checkReferenceCompatibility(module) {
  const input = createReadStream(new URL('./fixtures/reference-baseline.jsonl.gz', import.meta.url)).pipe(createGunzip());
  const lines = createInterface({ input, crlfDelay: Infinity });
  const names = new Set();
  for await (const line of lines) {
    const frozen = JSON.parse(line);
    assert.equal(frozen.originCommit, '20fcdce');
    assert.equal(frozen.package.source.languageVersion, 2);
    assert.ok(!names.has(frozen.name));
    names.add(frozen.name);
    const session = new module.Session(JSON.stringify(frozen.package));
    try {
      assert.equal(JSON.parse(session.stateSnapshotJSON()).definitionDigest, frozen.digest, frozen.name);
    } finally { session.delete(); }
  }
  assert.equal(names.size, 11);
  console.log(`${names.size} frozen format-2 fingerprints match the WASM executor.`);
}

if (process.argv[1] && pathToFileURL(resolve(process.argv[1])).href === import.meta.url) {
  const runtime = process.argv[2] ? pathToFileURL(resolve(process.argv[2])) : new URL('../kernel/generated/twisty.mjs', import.meta.url);
  const { default: createModule } = await import(runtime.href);
  await checkReferenceCompatibility(await createModule());
}
