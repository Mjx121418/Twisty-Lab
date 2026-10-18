import { Matrix4 } from 'three';
import type { FlatSurface } from '../../kernel/index';

export type SurfaceMode = 'square' | 'tiles';
export type Bounds = [number, number, number, number];
export type SurfaceImage = { matrix: Matrix4; x: number; y: number; reflected: boolean };

export function surfaceBounds(surface: FlatSurface, mode: SurfaceMode): Bounds {
  const [x,y,right,top] = surface.fundamentalBounds;
  return mode === 'square' ? [x,y,right,top] : [x-(right-x),y-(top-y),right+(right-x),top+(top-y)];
}

function power(generator: Matrix4, n: number): Matrix4 {
  const result = new Matrix4(), factor = generator.clone();
  if (n < 0) factor.invert();
  for (let i=0; i<Math.abs(n); i++) result.multiply(factor);
  return result;
}

// Padding includes images of a piece whose continuous animation lift crosses
// a boundary. This is presentation, independent of intrinsic state/transport.
export function surfaceImages(surface: FlatSurface, mode: SurfaceMode): SurfaceImage[] {
  const [a,b] = surface.deckGenerators.map((m) => new Matrix4().fromArray(m));
  const radius = mode === 'square' ? 1 : 2;
  const images: SurfaceImage[] = [];
  for (let y=-radius; y<=radius; y++) for (let x=-radius; x<=radius; x++) {
    const matrix = power(a,x).multiply(power(b,y));
    const e = matrix.elements;
    images.push({matrix,x,y,reflected:e[0]*e[5]-e[1]*e[4]<0});
  }
  return images;
}

export function insideBounds(bounds: Bounds, x: number, y: number): boolean {
  return x>=bounds[0]-1e-7 && x<=bounds[2]+1e-7 && y>=bounds[1]-1e-7 && y<=bounds[3]+1e-7;
}
