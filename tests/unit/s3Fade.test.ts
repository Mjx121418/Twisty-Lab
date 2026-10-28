import { expect, it } from 'vitest';
import { s3CellVisibility, s3FadeRange, s3Opacity } from '../../web/src/s3Fade';

const axes = Array.from({ length: 8 }, (_, i) => [0, 1, 2, 3].map(j => j === Math.floor(i / 2) ? (i % 2 ? -1 : 1) : 0));
const centers = [...axes, ...Array.from({ length: 16 }, (_, i) => [0, 1, 2, 3].map(j => i & (1 << j) ? -.5 : .5))];

it('bounds the focused cutting ball and face-neighbor range in both S³ puzzles', () => {
  for (const [depth, cells, adjacent, distant] of [[1 / 3, axes, 0, -1], [2 / 3, centers, .5, 0]] as const) {
    const range = s3FadeRange(depth, cells);
    expect(range.adjacentAlignment).toBe(adjacent);
    expect(range.distantAlignment).toBe(distant);
    const outerW = -adjacent * depth + Math.sqrt((1 - adjacent ** 2) * (1 - depth ** 2));
    const outerRadius = Math.sqrt((1 + outerW) / (1 - outerW));
    expect(range.endRadius).toBeGreaterThanOrEqual(outerRadius);
    expect(range.endRadius - outerRadius).toBeLessThan(.01);
    const cutoffW = (range.endRadius ** 2 - 1) / (range.endRadius ** 2 + 1);
    for (const start of [range.minRadius, (range.minRadius + range.maxStartRadius) / 2, range.maxStartRadius]) {
      expect(s3Opacity(-depth, start, range.endRadius)).toBe(1);
      expect(s3Opacity(cutoffW + 1e-8, start, range.endRadius)).toBe(0);
      expect(s3Opacity(1, start, range.endRadius)).toBe(0);
      let previous = 1;
      for (let i = 0; i <= 200; i++) {
        const opacity = s3Opacity(-1 + i / 100, start, range.endRadius);
        expect(opacity).toBeLessThanOrEqual(previous); previous = opacity;
      }
    }
    expect(s3CellVisibility(1, distant, adjacent)).toBe(1);
    expect(s3CellVisibility(adjacent - 1e-12, distant, adjacent)).toBe(1);
    expect(s3CellVisibility(distant + 1e-12, distant, adjacent)).toBe(0);
    expect(s3CellVisibility((distant + adjacent) / 2, distant, adjacent)).toBeCloseTo(.5);
    // Increasing the start radius reveals neighbors without changing the cutoff.
    const w = (range.maxStartRadius ** 2 - 1) / (range.maxStartRadius ** 2 + 1);
    expect(s3Opacity(w, range.maxStartRadius, range.endRadius)).toBeCloseTo(1);
    expect(s3Opacity(w, range.minRadius, range.endRadius)).toBeLessThan(.1);
  }
});

it('keeps the focused ball opaque while shorter end radii hide the neighbors', () => {
  for (const [depth, cells] of [[1 / 3, axes], [2 / 3, centers]] as const) {
    const range = s3FadeRange(depth, cells);
    for (const end of [range.minRadius + .01, (range.minRadius + range.endRadius) / 2, range.endRadius]) {
      const cutoffW = (end ** 2 - 1) / (end ** 2 + 1);
      expect(s3Opacity(-depth, range.minRadius, end)).toBe(1);
      expect(s3Opacity(cutoffW + 1e-8, range.minRadius, end)).toBe(0);
    }
    const radius = (range.minRadius + range.endRadius) / 2;
    const w = (radius ** 2 - 1) / (radius ** 2 + 1);
    expect(s3Opacity(w, range.minRadius, range.minRadius + .01)).toBe(0);
    expect(s3Opacity(w, range.minRadius, range.endRadius)).toBeGreaterThan(0);
  }
});
