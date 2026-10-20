import { describe, expect, it } from 'vitest';
import { defaultViews, restoreViews, type ViewId } from '../../web/src/viewSelection';

const spatial: ViewId[] = ['cube', 'sphere', 'diagram'];
const planar: ViewId[] = ['fundamental-domain', 'tiled-plane'];

describe('saved view selections', () => {
  it('keeps the existing default pairs', () => {
    expect(defaultViews(spatial).visible).toEqual(['cube', 'sphere']);
    expect(defaultViews(planar).visible).toEqual(planar);
    expect(defaultViews(['cube', 'diagram']).visible).toEqual(['cube', 'diagram']);
  });

  it('restores custom order and hidden views, ignoring unavailable or duplicated entries', () => {
    const restored = restoreViews({ viewOrder: ['diagram', 'sphere', 'sphere', 'invalid', 'tiled-plane'], visibleViews: ['sphere', 'diagram', 'tiled-plane'] }, spatial);
    expect(restored.selection).toEqual({ order: ['diagram', 'sphere', 'cube'], visible: ['sphere', 'diagram'] });
    expect(restored.cameraOrder).toEqual(['diagram', 'sphere', undefined, undefined, undefined]);
    expect(restoreViews({ viewOrder: [], visibleViews: ['invalid'] }, spatial).selection.visible).toEqual(['cube', 'sphere']);
  });

  it('reads old pair/layout sessions with cameras attached to the original pair', () => {
    const restored = restoreViews({ viewPair: 'sphere-diagram', layout: 'diagram' }, spatial);
    expect(restored.selection).toEqual({ order: ['sphere', 'diagram', 'cube'], visible: ['diagram'] });
    expect(restored.cameraOrder).toEqual(['sphere', 'diagram']);
    expect(restoreViews({ viewPair: 'square-tiles', layout: 'cube' }, planar).selection.visible).toEqual(['fundamental-domain']);
    expect(restoreViews({ viewPair: 'sphere-diagram' }, planar).selection.visible).toEqual(planar);
    expect(restoreViews({ viewPair: '__proto__' }, spatial).selection.visible).toEqual(['cube', 'sphere']);
  });
});
