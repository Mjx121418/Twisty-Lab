import { createHash } from 'node:crypto';
import { readFile, readdir, stat } from 'node:fs/promises';
import { join, posix } from 'node:path';
import { fileURLToPath } from 'node:url';
import { isDeepStrictEqual } from 'node:util';

export const root = fileURLToPath(new URL('../', import.meta.url));
export const manifestPath = 'packages/build-manifest.json';
const commonFiles = ['puzzle.pzl.json', 'bindings.json', 'package.json'];
const repair = 'Run npm run build:puzzles and review the generated changes.';

function fail(path, message) { throw new Error(`${path}: ${message}. ${repair}`); }
function relative(path) {
  if (typeof path !== 'string' || !path || path === '.' || path === '..' || path.startsWith('/') || path.includes('\\') || posix.normalize(path) !== path || path.startsWith('../'))
    fail(String(path), 'expected a repository-relative path');
  return path;
}
export async function readJSON(base, path) {
  try { return JSON.parse(await readFile(join(base, relative(path)), 'utf8')); }
  catch (error) { fail(path, `cannot read JSON (${error.message})`); }
}
export async function readInventory(base = root) {
  const inventory = await readJSON(base, 'scripts/puzzle-inventory.json');
  if (!inventory || inventory.formatVersion !== 1 || !Array.isArray(inventory.inputs) || !Array.isArray(inventory.generators) || !Array.isArray(inventory.puzzles))
    fail('scripts/puzzle-inventory.json', 'unsupported inventory');
  const ids = new Set();
  for (const puzzle of inventory.puzzles) {
    if (!puzzle || !/^[a-z0-9-]+$/.test(puzzle.id) || ids.has(puzzle.id) || !Array.isArray(puzzle.files) || !Array.isArray(puzzle.realizations) || !puzzle.realizations.length)
      fail('scripts/puzzle-inventory.json', `invalid puzzle entry ${puzzle?.id}`);
    ids.add(puzzle.id);
    for (const file of puzzleFiles(puzzle)) relative(file);
  }
  return inventory;
}
export function puzzleFiles(puzzle) {
  const files = [...commonFiles, ...puzzle.realizations, ...puzzle.files];
  if (new Set(files).size !== files.length) fail(puzzle.id, 'duplicate runtime artifact');
  return files;
}
export function artifactPaths(inventory) {
  return inventory.puzzles.flatMap((puzzle) => puzzleFiles(puzzle).map((file) => `packages/${puzzle.id}/${file}`)).sort();
}

export async function inputPaths(base, inventory) {
  const paths = new Set();
  async function collect(path) {
    relative(path);
    let info;
    try { info = await stat(join(base, path)); }
    catch { fail(path, 'missing authoring input'); }
    if (info.isDirectory()) {
      for (const child of (await readdir(join(base, path))).sort()) await collect(`${path}/${child}`);
    } else if (info.isFile()) paths.add(path);
    else fail(path, 'expected an authoring file');
  }
  for (const path of inventory.inputs) await collect(path);
  // Follow local Python imports, including imports inside functions. Standard
  // library modules have no matching file in scripts/ and are ignored.
  const visited = new Set();
  async function python(path) {
    relative(path);
    if (visited.has(path)) return;
    visited.add(path);
    await collect(path);
    const source = await readFile(join(base, path), 'utf8');
    for (const match of source.matchAll(/^\s*(?:from\s+([\w.]+)\s+import\b|import\s+([^\r\n#]+))/gm)) {
      const modules = match[1] ? [match[1]] : match[2].split(',').map((entry) => entry.trim().split(/\s+/)[0]);
      for (const module of modules) {
        const dependency = `scripts/${module.split('.')[0]}.py`;
        const exists = await stat(join(base, dependency)).then((info) => info.isFile(), () => false);
        if (exists) await python(dependency);
      }
    }
  }
  for (const generator of inventory.generators) await python(`scripts/${relative(generator)}`);
  // Follow project-local headers from the declared native producer sources.
  // System headers and generated build files are outside the data inventory.
  const native = [...paths].filter((path) => /\.(cpp|hpp|h)$/.test(path));
  for (const path of native) {
    const source = await readFile(join(base, path), 'utf8');
    for (const match of source.matchAll(/^\s*#\s*include\s*[<"]([^>"]+)[>"]/gm)) {
      for (const candidate of [posix.join(posix.dirname(path), match[1]), `include/${match[1]}`, `third_party/${match[1]}`]) {
        const dependency = posix.normalize(candidate);
        if (posix.isAbsolute(dependency) || dependency.startsWith('../')) continue;
        const exists = await stat(join(base, dependency)).then((info) => info.isFile(), () => false);
        if (!exists) continue;
        if (!paths.has(dependency)) { await collect(dependency); native.push(dependency); }
        break;
      }
    }
  }
  return [...paths].sort();
}
export async function hashFiles(base, paths) {
  const hashes = {};
  for (const path of paths) {
    try { hashes[relative(path)] = createHash('sha256').update(await readFile(join(base, path))).digest('hex'); }
    catch (error) { fail(path, `cannot hash file (${error.message})`); }
  }
  return hashes;
}
export async function inputHashes(base, inventory) { return hashFiles(base, await inputPaths(base, inventory)); }
export async function makeManifest(base, inventory) {
  return { formatVersion: 1, inputs: await inputHashes(base, inventory), artifacts: await hashFiles(base, artifactPaths(inventory)) };
}
export async function checkFreshness(base = root, inventory) {
  inventory ??= await readInventory(base);
  const recorded = await readJSON(base, manifestPath);
  if (!recorded || recorded.formatVersion !== 1 || !isDeepStrictEqual(Object.keys(recorded).sort(), ['artifacts', 'formatVersion', 'inputs']))
    fail(manifestPath, 'unsupported manifest format');
  const actual = await makeManifest(base, inventory);
  for (const group of ['inputs', 'artifacts']) {
    const entries = recorded[group];
    if (!entries || typeof entries !== 'object' || Array.isArray(entries)) fail(manifestPath, `invalid ${group} map`);
    for (const path of new Set([...Object.keys(actual[group]), ...Object.keys(entries)])) {
      if (!(path in actual[group]) || !(path in entries)) fail(path, `${group} inventory differs from the manifest`);
      if (!/^[0-9a-f]{64}$/.test(entries[path]) || actual[group][path] !== entries[path])
        fail(path, group === 'inputs' ? 'authoring input changed since puzzle generation' : 'prebuilt artifact changed since puzzle generation');
    }
  }
  // Keep retired/unlisted data from accumulating beside the current packages.
  const listed = new Set([manifestPath, ...Object.keys(actual.inputs), ...Object.keys(actual.artifacts)]);
  async function checkDirectory(path) {
    for (const entry of await readdir(join(base, path), { withFileTypes: true })) {
      const file = `${path}/${entry.name}`;
      if (entry.isDirectory()) await checkDirectory(file);
      else if (entry.name.endsWith('.json') && !listed.has(file)) fail(file, 'unlisted puzzle data; add an authoring input or runtime artifact to the inventory, or remove the file');
    }
  }
  await checkDirectory('packages');
  return actual;
}

// The native authoring command supplies digest-only loaders. The web gate
// supplies a WASM loader that also constructs each declared realization.
export async function validatePuzzleData(base, inventory, openPuzzle) {
  const { default: Ajv } = await import('ajv');
  const ajv = new Ajv({ strict: false, allErrors: true });
  for (const name of ['compact', 'puzzle', 'puzzle-v2', 'package', 'gear-source', 'realization', 'fixture']) {
    const path = `schemas/${name}.schema.json`;
    try { ajv.addSchema(await readJSON(base, path)); }
    catch (error) { fail(path, `cannot load schema (${error.message})`); }
  }
  const validate = (schema, data, path) => {
    const check = ajv.getSchema(`urn:twisty:schema:${schema}:${schema === 'relational-puzzle' && data.languageVersion === 2 ? 2 : 1}`);
    if (!check(data)) fail(path, `schema validation failed (${ajv.errorsText(check.errors?.slice(0, 3))})`);
  };
  const digests = new Map();
  let realizations = 0;
  for (const puzzle of inventory.puzzles) {
    const directory = `packages/${puzzle.id}/`;
    const documents = {};
    for (const file of puzzleFiles(puzzle)) {
      const document = await readJSON(base, directory + file);
      if (!document || typeof document !== 'object' || Array.isArray(document)) fail(directory + file, 'expected a JSON object');
      documents[file] = document;
    }
    const bundle = documents['package.json'];
    validate('relational-package', bundle, directory + 'package.json');
    validate('relational-puzzle', documents['puzzle.pzl.json'], directory + 'puzzle.pzl.json');
    for (const [field, file] of [['source', 'puzzle.pzl.json'], ['catalog', 'bindings.json']])
      if (!isDeepStrictEqual(bundle[field], documents[file])) fail(directory + file, `does not match package ${field}`);
    const packagePath = directory + 'package.json';
    let loaded;
    try {
      loaded = await openPuzzle(packagePath, JSON.stringify(bundle));
      if (!/^[0-9a-f]{64}$/.test(loaded.digest)) fail(packagePath, 'kernel returned an invalid semantic digest');
      digests.set(packagePath, loaded.digest);
      for (const file of puzzle.realizations) {
        const path = directory + file, model = documents[file];
        validate('realization', model, path);
        if (model.compatibleDefinitionDigest !== loaded.digest) fail(path, 'realization semantic pin differs from the current kernel');
        try { await loaded.checkRealization?.(JSON.stringify(model)); }
        catch (error) { fail(path, `kernel rejected the realization (${error.message ?? error})`); }
        ++realizations;
      }
      for (const file of puzzle.files) {
        const path = directory + file, document = documents[file];
        if ('definitionDigest' in document && document.definitionDigest !== loaded.digest) fail(path, 'manifest semantic pin differs from the current kernel');
        if (file === 'source.json') {
          validate('relational-gear-source', document, path);
          let wrapper;
          try {
            wrapper = await openPuzzle(path, JSON.stringify(document));
            if (wrapper.digest !== loaded.digest) fail(path, 'Gear wrapper differs from the prebuilt package');
            digests.set(path, wrapper.digest);
          } finally { wrapper?.dispose?.(); }
        }
      }
    } catch (error) {
      if (error.message?.endsWith(repair)) throw error;
      fail(packagePath, `kernel rejected the package (${error.message ?? error})`);
    } finally { loaded?.dispose?.(); }
  }
  const path = 'tests/fixtures/core.json', fixtures = await readJSON(base, path);
  validate('fixture', fixtures, path);
  for (const fixture of fixtures.cases) {
    if (!digests.has(fixture.source)) fail(path, `fixture ${fixture.name} refers to an unlisted puzzle ${fixture.source}`);
    if (digests.get(fixture.source) !== fixture.definitionDigest) fail(path, `fixture ${fixture.name} has an incorrect semantic pin`);
  }
  return { puzzles: inventory.puzzles.length, realizations };
}
export function wasmPuzzleLoader(module) {
  return (_path, text) => {
    const session = new module.Session(text);
    try {
      const digest = JSON.parse(session.snapshotJSON()).definitionDigest;
      return {
        digest,
        checkRealization(model) {
          const geometry = module.createGeometry(session, model);
          if (!geometry) throw new Error('Cannot construct geometric interpreter');
          try {
            const scene = JSON.parse(geometry.sceneJSON());
            if (!scene.visualParts.length) throw new Error('Empty realization');
          } finally { geometry.delete(); }
        },
        dispose() { session.delete(); },
      };
    } catch (error) { session.delete(); throw error; }
  };
}
