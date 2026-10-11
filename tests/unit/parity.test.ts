import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import createModule, { type MainModule, type Session } from '../../kernel/generated/twisty.mjs';

const read = (path: string): string => readFileSync(path, 'utf8');
let runtime: MainModule;
const handles: Session[] = [];
const session = (path: string): Session => { const value = new runtime.Session(read(path)); handles.push(value); return value; };
const snapshot = (value: Session): any => JSON.parse(value.snapshotJSON());
const native = (args: string[]): any => JSON.parse(execFileSync('build/native/twisty', [...args, '--json'], { encoding: 'utf8', maxBuffer: 4 * 1024 * 1024 }));

beforeAll(async () => { runtime = await createModule(); });
afterEach(() => { for (const handle of handles) handle.delete(); handles.length = 0; });

describe('native / WebAssembly parity', () => {
  it('matches dynamic mixed-slice Gear Cube rules, animation and replay for UF/UR', () => {
    const source = JSON.parse(read('packages/gear/source.json'));
    source.puzzleId = 'gear-custom-experimental-v1'; source.gearedEdges = ['UF', 'UR'];
    const text = JSON.stringify(source);
    const directory = mkdtempSync(join(tmpdir(), 'gear-mask-'));
    const path = join(directory, 'source.json'); writeFileSync(path, text);
    const value = new runtime.Session(text), restored = new runtime.Session(text);
    const compiled = JSON.parse(runtime.compileJSON(text));
    const realization = JSON.parse(read('packages/gear/gear-supplied-euclidean.json'));
    realization.compatibleDefinitionDigest = compiled.definition.definitionDigest;
    const geometry = runtime.createGeometry(value, JSON.stringify(realization));
    try {
      expect(geometry).toBeTruthy();
      expect(compiled).toEqual(native(['compile', '--definition', path]));
      expect(snapshot(value).legalRequests.map((request: any) => request.operation).sort()).toEqual(
        ['Uq', "Uq'", 'Dq', "Dq'", 'R', "R'", 'L', "L'", 'F', "F'", 'B', "B'"].sort());
      const requests: string[] = [];
      const initial = Array.from(geometry!.transforms() as Float32Array);
      for (const face of ['F', 'U', 'R', 'D', 'L', 'B']) {
        const before = snapshot(value);
        const candidates = before.legalRequests.filter((request: any) => request.operation === face || request.operation === `${face}q`);
        expect(candidates).toHaveLength(1);
        const request = candidates[0];
        expect(JSON.parse(value.planJSON(JSON.stringify(request), value.stateJSON())).status).toBe('Legal');
        expect(snapshot(value)).toEqual(before);
        const result = JSON.parse(value.executeJSON(JSON.stringify(request), before.revision));
        expect(result.status).toBe('Committed'); requests.push(request.operation);
        const reference = native(['run', '--definition', path, '--algorithm', requests.join(' '), '--policy', 'transactional']);
        expect(result.transitions[0]).toEqual(reference.transitions.at(-1));
        expect(snapshot(value)).toEqual(reference.snapshot);
        expect(JSON.parse(geometry!.prepareAnimationJSON(result.transitionRecords[0])).status).toBe('Prepared');
        geometry!.sample(.5); geometry!.sample(1);
        const endpoint = Array.from(geometry!.transforms() as Float32Array);
        geometry!.setStateJSON(value.stateJSON());
        expect(Array.from(geometry!.transforms() as Float32Array)).toEqual(endpoint);
      }
      expect(JSON.parse(restored.loadJSON(value.saveJSON())).status).toBe('Loaded');
      expect(snapshot(restored).stateDigest).toBe(snapshot(value).stateDigest);
      for (let i = requests.length - 1; i >= 0; --i) {
        const result = JSON.parse(value.undoJSON(snapshot(value).revision));
        expect(result.status).toBe('Committed');
        expect(JSON.parse(geometry!.prepareAnimationJSON(result.transitionRecords[0])).status).toBe('Prepared');
        geometry!.sample(1);
      }
      expect(snapshot(value).solved).toBe(true);
      expect(Array.from(geometry!.transforms() as Float32Array)).toEqual(initial);
    } finally { geometry?.delete(); value.delete(); restored.delete(); rmSync(directory, { recursive: true, force: true }); }
  });
  it.each([
    ['helicopter', 'helicopter-euclidean', "UF_ab UL_af (UF_ab UL_af)' UF_ad"],
    ['helicopter', 'helicopter-port-diagram', "UF_ab UL_af (UF_ab UL_af)' UF_ad"],
    ['helicopter', 'helicopter-spherical', "UF_ab DR_ab FR_ad DR_ba UF_ba (UF_ab DR_ab FR_ad DR_ba UF_ba)'"],
    ['cube3', 'cube-spherical', "U R F D L B (U R F D L B)'"],
    ['bandaged', 'cube-spherical', "U R R' U' F F'"],
    ['bagua', 'bagua-euclidean', '[[U+ R2:U+],[R R+ L-:D2]]'],
    ['bagua', 'bagua-port-diagram', "U+ R' L' D2 R L U-"],
    ['bagua', 'bagua-spherical', "U+ R F- (U+ R F-)'"],
    ['gear', 'gear-euclidean', "R U F (R U F)' R12"],
    ['gear', 'gear-normal-euclidean', "R U F (R U F)' R12"],
    ['gear-extreme', 'gear-extreme-supplied-euclidean', "Uq R Fq (Uq R Fq)'"],
    ['gear-plain', 'gear-plain-supplied-euclidean', "Rq Uq Fq (Rq Uq Fq)'"],
    ['gear', 'gear-port-diagram', "R U F (R U F)'"],
    ['gear', 'gear-six-teeth-euclidean', "R U F (R U F)' R12"],
    ['gear', 'gear-six-teeth-port-diagram', "R U F (R U F)' R12"],
    ['gear-extreme', 'gear-extreme-euclidean', "Uq R Fq (Uq R Fq)'"],
    ['gear-plain', 'gear-plain-port-diagram', "Rq Uq Fq (Rq Uq Fq)'"],
  ])('matches native %s assets and sampled %s frames', (name, kind, algorithm) => {
    const definition = `packages/${name}/definition.json`;
    const realization = `packages/${name}/${kind}.json`;
    // Detailed authored meshes include expanded positions and normals in this probe.
    const reference = JSON.parse(execFileSync('build/native/geometry_probe', [definition, realization, algorithm], { encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 }));
    const value = new runtime.Session(read(definition));
    const geometry = runtime.createGeometry(value, read(realization));
    if (!geometry) throw new Error('Geometry creation failed.');
    const sameFloats = (actual: Float32Array, expected: number[]): void => {
      expect(actual.length).toBe(expected.length);
      for (let i = 0; i < actual.length; i++) expect(Math.abs(actual[i] - expected[i])).toBeLessThan(1e-6);
    };
    try {
      expect(JSON.parse(geometry.sceneJSON())).toEqual(reference.scene);
      sameFloats(geometry.positions() as Float32Array, reference.positions);
      sameFloats(geometry.normals() as Float32Array, reference.normals);
      expect(Array.from(geometry.indices() as Uint32Array)).toEqual(reference.indices);
      sameFloats(geometry.transforms() as Float32Array, reference.initial);
      const result = JSON.parse(value.runJSON(algorithm, 'transactional', '0'));
      expect(result.status).toBe('Committed');
      for (let i = 0; i < result.transitionRecords.length; i++) {
        expect(JSON.parse(geometry.prepareAnimationJSON(result.transitionRecords[i])).status).toBe('Prepared');
        sameFloats(geometry.transforms() as Float32Array, reference.frames[i].source);
        geometry.sample(0.5);
        sameFloats(geometry.transforms() as Float32Array, reference.frames[i].middle);
        geometry.sample(1);
        sameFloats(geometry.transforms() as Float32Array, reference.frames[i].target);
        const endpoint = Array.from(geometry.transforms() as Float32Array);
        geometry.setStateJSON(JSON.stringify(result.transitions[i].afterState));
        expect(Array.from(geometry.transforms() as Float32Array)).toEqual(endpoint);
      }
      expect(snapshot(value).stateDigest).toBe(reference.stateDigest);
    } finally { geometry.delete(); value.delete(); }
  });
  it('keeps a shared geometric definition alive after its session is disposed', () => {
    const value = new runtime.Session(read('packages/helicopter/definition.json'));
    const state = value.stateJSON();
    const geometry = runtime.createGeometry(value, read('packages/helicopter/helicopter-euclidean.json'));
    if (!geometry) throw new Error('Geometry creation failed.');
    value.delete();
    try {
      const initial = Array.from(geometry.transforms() as Float32Array);
      geometry.setStateJSON(state);
      geometry.sample(1);
      expect(Array.from(geometry.transforms() as Float32Array)).toEqual(initial);
      const part = JSON.parse(geometry.sceneJSON()).visualParts[0];
      expect(JSON.parse(geometry.bindHitJSON(part.visualPartId)).pieceId).toBe(part.pieceId);
    } finally { geometry.delete(); }
  });
  it('matches Helicopter jumbling, blocking, inverse and replay records', () => {
    const path = 'packages/helicopter/definition.json';
    const wasm = new runtime.Session(read(path));
    const restored = new runtime.Session(read(path));
    try {
      const algorithm = 'UF_ab DR_ab FR_ad DR_ba UF_ba';
      const result = JSON.parse(wasm.runJSON(algorithm, 'transactional', '0'));
      const reference = native(['run', '--definition', path, '--algorithm', algorithm, '--policy', 'transactional']);
      expect(result.status).toBe('Committed');
      expect(result.snapshot).toEqual(reference.snapshot);
      expect(result.transitions).toEqual(reference.transitions);
      expect(JSON.parse(restored.loadJSON(wasm.saveJSON())).status).toBe('Loaded');
      expect(snapshot(restored).stateDigest).toBe(snapshot(wasm).stateDigest);
      expect(JSON.parse(wasm.runJSON(`(${algorithm})'`, 'transactional', snapshot(wasm).revision)).status).toBe('Committed');
      expect(snapshot(wasm).solved).toBe(true);
      expect(JSON.parse(wasm.runJSON('UF_ab UR_ad', 'transactional', snapshot(wasm).revision)).reasonCode).toBe('placement.blocked');
      expect(snapshot(wasm).solved).toBe(true);
    } finally { wasm.delete(); restored.delete(); }
  });
  it.each(['cube3', 'bandaged'])('matches compilation, state, scramble, and witnesses for %s', (name) => {
    const path = `packages/${name}/source.json`;
    const wasm = session(path);
    const compiled = JSON.parse(runtime.compileJSON(read(path)));
    expect(compiled).toEqual(native(['compile', '--definition', path]));
    expect(snapshot(wasm)).toEqual(native(['inspect', '--definition', path]).snapshot);
    const result = JSON.parse(wasm.scrambleJSON(42, 30, '0'));
    const reference = native(['scramble', '--definition', path, '--seed', '42', '--length', '30']);
    expect(result.snapshot).toEqual(reference.snapshot);
    expect(result.transitions).toEqual(reference.transitions);
    expect(result.scramble).toEqual(reference.scramble);
    const restored = session(path);
    expect(JSON.parse(restored.loadJSON(wasm.saveJSON())).status).toBe('Loaded');
    expect(snapshot(restored).stateDigest).toEqual(snapshot(wasm).stateDigest);
  });

  it('matches Bagua published sequences, cut blocking and replay', () => {
    const path = 'packages/bagua/definition.json';
    const wasm = new runtime.Session(read(path));
    const restored = new runtime.Session(read(path));
    try {
      const origin = snapshot(wasm);
      const algorithm = '[[U+ R2:U+],[R R+ L-:D2]]';
      const result = JSON.parse(wasm.runJSON(algorithm, 'transactional', origin.revision));
      const reference = native(['run', '--definition', path, '--algorithm', algorithm, '--policy', 'transactional']);
      expect(result.status).toBe('Committed');
      expect(result.snapshot).toEqual(reference.snapshot);
      expect(result.transitions).toEqual(reference.transitions);
      expect(JSON.parse(restored.loadJSON(wasm.saveJSON())).status).toBe('Loaded');
      expect(snapshot(restored).stateDigest).toBe(snapshot(wasm).stateDigest);
      expect(JSON.parse(wasm.runJSON(`(${algorithm})'`, 'transactional', snapshot(wasm).revision)).status).toBe('Committed');
      expect(snapshot(wasm).stateDigest).toBe(origin.stateDigest);
      const blocked = JSON.parse(wasm.runJSON('U+ R F- U+', 'transactional', snapshot(wasm).revision));
      expect(blocked.reasonCode).toBe('placement.blocked');
      expect(blocked.implicatedPieces).toContain('corner/07');
      expect(snapshot(wasm).stateDigest).toBe(origin.stateDigest);
    } finally { wasm.delete(); restored.delete(); }
  });

  it.each(JSON.parse(read('packages/bagua/review.json')).pureKiteCycles)(
    'matches Bagua $name and its published pure three-cycle', ({ notation }: { notation: string }) => {
      const path = 'packages/bagua/definition.json';
      const wasm = session(path);
      const definition = JSON.parse(wasm.definitionJSON());
      const result = JSON.parse(wasm.runJSON(notation, 'transactional', '0'));
      const reference = native(['run', '--definition', path, '--algorithm', notation, '--policy', 'transactional']);
      expect(result.status).toBe('Committed');
      expect(result.snapshot).toEqual(reference.snapshot);
      expect(result.transitions).toEqual(reference.transitions);
      const moved = definition.pieces.filter((piece: any) => result.snapshot.state.placementOf[piece.id] !== piece.homePlacement);
      expect(moved).toHaveLength(3);
      expect(moved.every((piece: any) => piece.type.startsWith('kite-'))).toBe(true);
      const destinations = new Map(moved.map((piece: any) => [piece.homePlacement, piece.id]));
      let current = moved[0].id;
      const cycle = new Set<string>();
      for (let i = 0; i < 3; i++) {
        expect(cycle.has(current)).toBe(false);
        cycle.add(current);
        current = destinations.get(result.snapshot.state.placementOf[current]) as string;
        expect(current).toBeDefined();
      }
      expect(current).toBe(moved[0].id);
    },
  );

  it('runs the same version-pinned portable fixtures', () => {
    const fixtures = JSON.parse(read('tests/fixtures/core.json'));
    for (const fixture of fixtures.cases) {
      const value = new runtime.Session(read(fixture.source));
      try {
        expect(snapshot(value).definitionDigest).toBe(fixture.definitionDigest);
        expect(JSON.parse(value.runJSON(fixture.prefix, 'transactional', snapshot(value).revision)).status).toBe('Committed');
        const result = JSON.parse(value.executeJSON(JSON.stringify({ operation: fixture.operation, parameters: {} }), snapshot(value).revision));
        expect(result.status, fixture.name).toBe(fixture.expectedStatus);
        if (fixture.reasonCode) expect(result.reasonCode).toBe(fixture.reasonCode);
        if (fixture.implicatedPiece) expect(result.implicatedPieces).toContain(fixture.implicatedPiece);
        if (fixture.unchangedParticipant) expect(result.transitions[0].pieceActions).toContainEqual(expect.objectContaining({ pieceId: fixture.unchangedParticipant, from: 'center:U', to: 'center:U' }));
      } finally { value.delete(); }
    }
  });

  it('preserves wide exact integers through native serialization and animation records', () => {
    const compiled = JSON.parse(runtime.compileJSON(read('packages/cube3/source.json'))).definition;
    delete compiled.definitionDigest;
    compiled.initialState.mechanism.counter = 'wide-integer-marker';
    const source = JSON.stringify(compiled).replace('"wide-integer-marker"', '9223372036854775807');
    const value = new runtime.Session(source); handles.push(value);
    const result = JSON.parse(value.runJSON('U', 'interactive', '0'));
    expect(result.transitionRecords[0]).toContain('9223372036854775807');
    const text = value.saveWithPresentationJSON('{"cameraZoom":0.5}');
    expect(text).toContain('"counter":9223372036854775807');
    const restored = new runtime.Session(source); handles.push(restored);
    expect(JSON.parse(restored.loadJSON(text)).status).toBe('Loaded');
    expect(snapshot(restored).stateDigest).toBe(snapshot(value).stateDigest);
    const realization = { ...JSON.parse(read('packages/cube3/cube-euclidean.json')), compatibleDefinitionDigest: snapshot(value).definitionDigest };
    const view = new runtime.Geometry(value.definitionJSON(), JSON.stringify(realization));
    try {
      expect(JSON.parse(view.prepareAnimationJSON(result.transitionRecords[0])).status).toBe('Prepared');
      view.sample(1);
      expect(Array.from(view.transforms() as Float32Array).every(Number.isFinite)).toBe(true);
    } finally { view.delete(); }
  });
});
