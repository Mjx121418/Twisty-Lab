import { readFileSync } from 'node:fs';
import { beforeAll, describe, expect, it } from 'vitest';
import { createKernel, KernelSession, type MainModule } from '../../kernel/index';
import { pieceDestinations } from '../../web/src/destinations';

const read = (path: string): string => readFileSync(path, 'utf8');
let runtime: MainModule;
beforeAll(async () => { runtime = await createKernel(); });

describe('selected-piece destinations', () => {
  it.each([
    ['cube3', 'source.json', 'corner/UFR', ['F', "F'", 'R', "R'", 'U', "U'"]],
    ['bandaged', 'source.json', 'bandage/UF-UFR', ['F', "F'", 'U', "U'"]],
    ['helicopter', 'definition.json', 'corner/BDL', ['BL_ab', 'BL_ac', 'BL_ad', 'BL_ae', 'BL_af', 'DB_ab', 'DB_ac', 'DB_ad', 'DB_ae', 'DB_af', 'DL_ab', 'DL_ac', 'DL_ad', 'DL_ae', 'DL_af']],
  ])('plans only legal participating moves for %s', (puzzle, source, pieceId, expected) => {
    const session = new KernelSession(runtime, read(`packages/${puzzle}/${source}`));
    try {
      const before = session.save();
      const snapshot = session.snapshot();
      const options = pieceDestinations(session, snapshot, pieceId as string);
      expect(options.map((option) => option.request.operation).sort()).toEqual(expected);
      expect(options.every((option) => option.revision === snapshot.revision && option.definitionDigest === snapshot.definitionDigest)).toBe(true);
      expect(session.save()).toBe(before);
      expect(pieceDestinations(session, snapshot, 'missing-piece')).toEqual([]);
    } finally { session.dispose(); }
  });

  it('includes centers that participate without changing placement and rejects old source revisions', () => {
    const session = new KernelSession(runtime, read('packages/cube3/source.json'));
    try {
      const snapshot = session.snapshot();
      const options = pieceDestinations(session, snapshot, 'center/U');
      expect(options.map((option) => option.request.operation)).toEqual(['U', "U'"]);
      expect(JSON.parse(options[0].transitionRecord).pieceActions).toContainEqual(expect.objectContaining({ pieceId: 'center/U', from: 'center:U', to: 'center:U' }));
      expect(session.move('F').status).toBe('Committed');
      const newer = session.save();
      expect(pieceDestinations(session, snapshot, 'center/U')).toEqual([]);
      expect(session.execute(options[0].request, options[0].revision).status).toBe('StaleRevision');
      expect(session.save()).toBe(newer);
    } finally { session.dispose(); }
  });

  it('preserves exact records and previews destinations without changing the session or live geometry', () => {
    const compiled = JSON.parse(runtime.compileJSON(read('packages/cube3/source.json'))).definition;
    delete compiled.definitionDigest;
    compiled.initialState.mechanism.counter = 'wide-integer-marker';
    const source = JSON.stringify(compiled).replace('"wide-integer-marker"', '9223372036854775807');
    const session = new KernelSession(runtime, source);
    const realization = { ...JSON.parse(read('packages/cube3/cube-spherical.json')), compatibleDefinitionDigest: session.definition.definitionDigest };
    const live = runtime.createGeometry(session.native, JSON.stringify(realization))!;
    const preview = runtime.createGeometry(session.native, JSON.stringify(realization))!;
    try {
      const before = session.save();
      const liveFrame = Array.from(live.transforms() as Float32Array);
      const options = pieceDestinations(session, session.snapshot(), 'corner/UFR');
      for (const option of options) {
        expect(option.transitionRecord).toContain('9223372036854775807');
        expect(JSON.parse(preview.prepareAnimationJSON(option.transitionRecord)).status).toBe('Prepared');
        preview.sample(1);
        expect(Array.from(preview.transforms() as Float32Array).every(Number.isFinite)).toBe(true);
      }
      expect(Array.from(live.transforms() as Float32Array)).toEqual(liveFrame);
      expect(session.save()).toBe(before);
    } finally { preview.delete(); live.delete(); session.dispose(); }
  });
});
