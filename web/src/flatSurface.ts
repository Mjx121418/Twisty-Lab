import { Matrix4 } from 'three';
import type { FlatSurface } from '../../kernel/index';

export type SurfaceMode = 'square' | 'tiles';
export type Bounds = [number, number, number, number];
export type SurfaceImage = { matrix: Matrix4; x: number; y: number; reflected: boolean };
export type Polygon = [number, number][];

export function surfacePolygon(surface: FlatSurface, mode: SurfaceMode): Polygon {
  if(mode==='square' && surface.fundamentalPolygon)return surface.fundamentalPolygon;
  if (surface.latticeBasis) {
    const [a,b]=surface.latticeBasis,lo=mode==='square'?0:-1,hi=mode==='square'?1:2;
    const [x,y]=surface.chartOrigin??[0,0];
    return [[lo,lo],[hi,lo],[hi,hi],[lo,hi]].map(([i,j])=>[x+i*a[0]+j*b[0],y+i*a[1]+j*b[1]]);
  }
  const [x,y,r,t]=surface.fundamentalBounds,dx=mode==='square'?0:r-x,dy=mode==='square'?0:t-y;
  return [[x-dx,y-dy],[r+dx,y-dy],[r+dx,t+dy],[x-dx,t+dy]];
}

export function surfaceBounds(surface: FlatSurface, mode: SurfaceMode): Bounds {
  const polygon=surfacePolygon(surface,mode),xs=polygon.map(p=>p[0]),ys=polygon.map(p=>p[1]);
  return [Math.min(...xs),Math.min(...ys),Math.max(...xs),Math.max(...ys)];
}

function power(generator: Matrix4, n: number): Matrix4 {
  const result = new Matrix4(), factor = generator.clone();
  if (n < 0) factor.invert();
  for (let remaining=Math.abs(n);remaining>0;remaining=Math.floor(remaining/2)) {
    if(remaining%2!==0)result.multiply(factor);
    factor.multiply(factor);
  }
  return result;
}

export function surfaceImageOrigin(surface: FlatSurface, x: number, y: number): [number, number] {
  if(surface.latticeBasis){
    x-=surface.chartOrigin?.[0]??0;y-=surface.chartOrigin?.[1]??0;
    const [a,b]=surface.topology==='orbifold-333'
      ?surface.deckGenerators.slice(0,2).map(m=>[m[12],m[13]]) :surface.latticeBasis;
    const det=a[0]*b[1]-a[1]*b[0];
    return [Math.floor((x*b[1]-y*b[0])/det),Math.floor((a[0]*y-a[1]*x)/det)];
  }
  const [left,bottom,right,top]=surface.fundamentalBounds;
  return [Math.floor((x-left)/(right-left)),Math.floor((y-bottom)/(top-bottom))];
}

// Padding includes images of a piece whose continuous animation lift crosses
// a boundary. This is presentation, independent of intrinsic state/transport.
export function surfaceImages(surface: FlatSurface, mode: SurfaceMode, origin: [number,number]=[0,0],
  radius:number|[number,number]=mode==='square'?1:2): SurfaceImage[] {
  const [a,b] = surface.deckGenerators.map((m) => new Matrix4().fromArray(m));
  const [rx,ry]=typeof radius==='number'?[radius,radius]:radius;
  const images: SurfaceImage[] = [];
  for (let dy=-ry; dy<=ry; dy++) for (let dx=-rx; dx<=rx; dx++) {
    const x=origin[0]+dx,y=origin[1]+dy;
    if(surface.topology==='orbifold-333'){
      const translation=power(a,x).multiply(power(b,y));
      const rotation=new Matrix4().fromArray(surface.deckGenerators[2]!);
      for(let turn=0;turn<3;turn++)images.push({matrix:translation.clone().multiply(power(rotation,turn)),x,y,reflected:false});
      continue;
    }
    const matrix = surface.latticeBasis ? new Matrix4().makeTranslation(
      x*surface.latticeBasis[0][0]+y*surface.latticeBasis[1][0],
      x*surface.latticeBasis[0][1]+y*surface.latticeBasis[1][1],0) : power(a,x).multiply(power(b,y));
    const holonomy=surface.tileHolonomy??surface.tileReflection;
    if(holonomy && (x+y)%2!==0) matrix.multiply(new Matrix4().fromArray(holonomy));
    const e = matrix.elements;
    images.push({matrix,x,y,reflected:e[0]*e[5]-e[1]*e[4]<0});
  }
  return images;
}

/** World-space window whose projection stays fixed while the camera pans/zooms. */
export function cameraWindowPolygon(polygon: Polygon, center: [number,number], target: [number,number], zoom: number): Polygon {
  return polygon.map(([x,y])=>[target[0]+(x-center[0])/zoom,target[1]+(y-center[1])/zoom]);
}

export function insidePolygon(polygon: Polygon, x: number, y: number): boolean {
  return polygon.every((a,i)=>{const b=polygon[(i+1)%polygon.length];return (b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0])>=-1e-7;});
}

export function insideBounds(bounds: Bounds, x: number, y: number): boolean {
  return x>=bounds[0]-1e-7 && x<=bounds[2]+1e-7 && y>=bounds[1]-1e-7 && y<=bounds[3]+1e-7;
}
