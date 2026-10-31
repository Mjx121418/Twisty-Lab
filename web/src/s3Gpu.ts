import * as THREE from 'three';
import { copyIntrinsicMeshes, type GeometryInstance, type GeometryScene } from '../../kernel/shared';
import { s3CellVisibility, s3FadeRange, s3Opacity, type S3FadeSettings } from './s3Fade';
import type { S3Mesh } from './s3Materials';

// Keep Three.js's lighting and color management; compute projection, normals
// and opacity in the vertex shader, then interpolate alpha across the surface.
export function patchS3Material(material: THREE.MeshStandardMaterial, startRadius: number, endRadius: number) {
  const uniforms = { s3Enabled: { value: false }, s3Pose: { value: new THREE.Matrix4() }, s3Scale: { value: 1 }, s3FadeStart: { value: startRadius }, s3FadeEnd: { value: endRadius }, s3CellVisibility: { value: 1 }, s3PolePiece: { value: false } };
  material.onBeforeCompile = shader => {
    Object.assign(shader.uniforms, uniforms);
    shader.vertexShader = `
      attribute vec4 intrinsicPosition;
      attribute vec4 intrinsicNormal;
      uniform bool s3Enabled;
      uniform mat4 s3Pose;
      uniform float s3Scale;
      uniform float s3FadeStart;
      uniform float s3FadeEnd;
      uniform float s3CellVisibility;
      uniform bool s3PolePiece;
      varying float vS3Opacity;
      varying vec3 vS3ProjectedPosition;
    ` + shader.vertexShader;
    shader.vertexShader = shader.vertexShader.replace('#include <beginnormal_vertex>', `
      #include <beginnormal_vertex>
      vec4 s3Point = s3Pose * intrinsicPosition;
      if (s3Enabled) {
        vec4 tangent = s3Pose * intrinsicNormal;
        objectNormal = normalize((1.0 - s3Point.w) * tangent.xyz + s3Point.xyz * tangent.w);
      }
    `).replace('#include <begin_vertex>', `
      #include <begin_vertex>
      if (s3Enabled) transformed = s3Scale * s3Point.xyz / (1.0 - s3Point.w);
      vS3ProjectedPosition = transformed / s3Scale;
      // Cached CPU meshes use the inverse stereographic radius. Their model
      // matrices are rotations about the origin, so this W is unchanged.
      float radiusSquared = dot(position, position);
      float scaleSquared = s3Scale * s3Scale;
      float poleDepth = s3Enabled ? s3Point.w : (radiusSquared - scaleSquared) / (radiusSquared + scaleSquared);
      float radius = sqrt(max(0.0, (1.0 + poleDepth) / max(1e-7, 1.0 - poleDepth)));
      vS3Opacity = s3PolePiece ? 0.0 : s3CellVisibility * (1.0 - smoothstep(s3FadeStart, s3FadeEnd, radius));
    `);
    shader.fragmentShader = 'varying float vS3Opacity;\nvarying vec3 vS3ProjectedPosition;\nuniform float s3FadeEnd;\n' + shader.fragmentShader;
    shader.fragmentShader = shader.fragmentShader.replace('#include <color_fragment>', `
      #include <color_fragment>
      // Enforce the radius on the drawn surface too, including triangles
      // straddling the cutoff whose vertex alphas would interpolate past it.
      if (dot(vS3ProjectedPosition, vS3ProjectedPosition) >= s3FadeEnd * s3FadeEnd) discard;
      diffuseColor.a *= vS3Opacity;
      if (diffuseColor.a <= 0.0) discard;
    `);
  };
  material.customProgramCacheKey = () => 's3-stereographic-v5-radius-cutoff';
  return uniforms;
}

/** Maximum W over a spherical cap with center W=depth and angular cosine. */
export function capUpperW(depth: number, cosine: number): number {
  if (depth >= cosine) return 1;
  return depth * cosine + Math.sqrt(Math.max(0, (1 - depth * depth) * (1 - cosine * cosine)));
}

/** Returns an R³ rotation when two SO(4) poses have the same pole covector. */
export function clippedMeshRotation(current: ArrayLike<number>, base: ArrayLike<number>, out: THREE.Matrix4): boolean {
  for (let j = 0; j < 4; j++) if (Math.abs(current[j * 4 + 3] - base[j * 4 + 3]) > 1e-8) return false;
  out.identity();
  for (let j = 0; j < 3; j++) for (let i = 0; i < 3; i++) {
    let value = 0;
    for (let k = 0; k < 4; k++) value += current[k * 4 + i] * base[k * 4 + j];
    out.elements[j * 4 + i] = value;
  }
  return true;
}

/** Static prototype buffers and small per-part uniforms. CPU-clipped meshes are
 * reusable under rotations fixing the pole; other clipping changes ask the
 * caller for a fresh portable drawing before the frame can be displayed. */
export class S3GpuProjection {
  readonly pieceDepths: Record<string, number> = {};
  readonly guidePieces = new Set<string>();
  readonly opacityRanges: { min: number; max: number }[];
  readonly fadeRange;
  private startRadius: number;
  private endRadius: number;
  private readonly intrinsic;
  private readonly assets = new Map<string, THREE.BufferGeometry>();
  private readonly positiveCuts: { normal: number[]; minimum: number; meshMinimum: number }[][];
  private readonly coordinateBounds = new Map<string, { min: number[]; max: number[] }>();
  private readonly uniforms;
  private readonly combined: THREE.Matrix4[];
  private readonly bases: THREE.Matrix4[];
  private readonly rotations: THREE.Matrix4[];
  private readonly modes: Uint8Array;
  private readonly bounds: THREE.Sphere[];
  private readonly sortAnchors: THREE.Vector4[];
  private readonly clippedDepths: { min: number; max: number }[];
  private readonly view = new THREE.Matrix4();
  private readonly pose = new THREE.Matrix4();
  private readonly center = new THREE.Vector4();

  constructor(private readonly geometry: GeometryInstance<GeometryScene>, private readonly meshes: S3Mesh[], private readonly cpuAssets: Map<string, THREE.BufferGeometry>, readonly cutDepth = 1 / 3) {
    const layout = geometry.scene.controlLayout;
    if (layout?.version !== 2) throw new Error('S³ projection requires cell centers.');
    this.fadeRange = s3FadeRange(cutDepth, layout.groups.map(g => g.center));
    this.startRadius = this.fadeRange.minRadius;
    this.endRadius = this.fadeRange.endRadius;
    this.intrinsic = copyIntrinsicMeshes(geometry.definition);
    const { descriptor, positions, normals, indices } = this.intrinsic;
    const assetDescriptors = new Map(descriptor.meshAssets.map(a => [a.id, a]));
    this.positiveCuts = descriptor.visualParts.map(part => {
      const asset = assetDescriptors.get(part.meshAssetId)!;
      const cuts = part.cutHalfspaces?.filter(c => c.minimum > 0)
        ?? Array.from({ length: part.activeCoordinates! }, (_, j) => ({
          normal: [0, 1, 2, 3].map(k => +(j === k)), minimum: cutDepth }));
      return cuts.map(cut => {
        let meshMinimum = 1;
        for (let i = 0; i < asset.vertexCount; i++) {
          let value = 0;
          for (let j = 0; j < 4; j++) value += positions[asset.positionOffset + 4 * i + j] * cut.normal[j];
          meshMinimum = Math.min(meshMinimum, value);
        }
        return { ...cut, meshMinimum };
      });
    });
    if (descriptor.version !== 1 || descriptor.coordinateSize !== 4 || descriptor.space.id !== 'S3') throw new Error('Unsupported intrinsic mesh buffer.');
    for (const asset of descriptor.meshAssets) {
      const mesh = new THREE.BufferGeometry();
      const count = asset.vertexCount * 4;
      const points = positions.slice(asset.positionOffset, asset.positionOffset + count);
      const tangents = normals.slice(asset.normalOffset, asset.normalOffset + count);
      const min = [Infinity, Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity, -Infinity];
      for (let i = 0; i < asset.vertexCount; i++) for (let j = 0; j < 4; j++) {
        min[j] = Math.min(min[j], points[4 * i + j]); max[j] = Math.max(max[j], points[4 * i + j]);
      }
      this.coordinateBounds.set(asset.id, { min, max });
      // Standard attributes remain present for Three.js; the shader uses vec4.
      const xyz = new Float32Array(asset.vertexCount * 3);
      const nxyz = new Float32Array(xyz.length);
      for (let i = 0; i < asset.vertexCount; i++) for (let j = 0; j < 3; j++) {
        xyz[i * 3 + j] = points[i * 4 + j]; nxyz[i * 3 + j] = tangents[i * 4 + j];
      }
      mesh.setAttribute('position', new THREE.BufferAttribute(xyz, 3));
      mesh.setAttribute('normal', new THREE.BufferAttribute(nxyz, 3));
      mesh.setAttribute('intrinsicPosition', new THREE.BufferAttribute(points, 4));
      mesh.setAttribute('intrinsicNormal', new THREE.BufferAttribute(tangents, 4));
      mesh.setIndex(new THREE.BufferAttribute(indices.slice(asset.indexOffset, asset.indexOffset + asset.indexCount), 1));
      this.assets.set(asset.id, mesh);
    }
    this.uniforms = meshes.map(mesh => {
      mesh.frustumCulled = false;
      if (Array.isArray(mesh.material)) throw new Error('S³ projection requires one source material per part.');
      return patchS3Material(mesh.material, this.startRadius, this.endRadius);
    });
    this.combined = meshes.map(() => new THREE.Matrix4());
    this.bases = meshes.map(() => new THREE.Matrix4());
    this.rotations = meshes.map(() => new THREE.Matrix4());
    this.modes = new Uint8Array(meshes.length);
    // Three.js uses object-level spheres for transparent sorting. Prototype
    // xyz bounds cannot describe a nonlinearly projected instance.
    this.bounds = meshes.map(mesh => {
      const sphere = new THREE.Sphere();
      Object.assign(mesh, { boundingSphere: sphere });
      return sphere;
    });
    this.sortAnchors = meshes.map(() => new THREE.Vector4());
    this.opacityRanges = meshes.map(() => ({ min: 1, max: 1 }));
    this.clippedDepths = meshes.map(() => ({ min: 1, max: 1 }));
    this.captureDrawing();
  }

  get fadeSettings(): S3FadeSettings {
    const { minRadius, maxStartRadius, endRadius: maxEndRadius } = this.fadeRange;
    return { minRadius, maxStartRadius: Math.min(maxStartRadius, Math.round((this.endRadius - .01) * 100) / 100), maxEndRadius, endRadius: this.endRadius, startRadius: this.startRadius };
  }
  setFadeStartRadius(radius: number): void {
    if (!Number.isFinite(radius)) return;
    this.startRadius = Math.max(this.fadeRange.minRadius, Math.min(this.fadeSettings.maxStartRadius, Math.round(radius * 100) / 100));
  }
  setFadeEndRadius(radius: number): void {
    if (!Number.isFinite(radius)) return;
    this.endRadius = Math.max(this.fadeRange.minRadius + .01, Math.min(this.fadeRange.endRadius, Math.round(radius * 100) / 100));
    this.setFadeStartRadius(this.startRadius);
  }

  /** CPU assets now describe the current view and intrinsic poses. */
  captureDrawing(): void {
    this.combine();
    const scale = this.geometry.scene.projection!.scale;
    for (let i = 0; i < this.bases.length; i++) {
      this.bases[i].copy(this.combined[i]);
      const cpuMesh = this.cpuAssets.get(this.meshes[i].userData.meshAssetId)!;
      const center = cpuMesh.boundingSphere?.center;
      const positions = cpuMesh.getAttribute('position');
      let minRadius = Infinity, maxRadius = -Infinity;
      for (let vertex = 0; vertex < (positions?.count ?? 0); vertex++) {
        const radius = positions.getX(vertex) ** 2 + positions.getY(vertex) ** 2 + positions.getZ(vertex) ** 2;
        minRadius = Math.min(minRadius, radius); maxRadius = Math.max(maxRadius, radius);
      }
      const scaleSquared = scale * scale;
      this.clippedDepths[i].min = positions?.count ? (minRadius - scaleSquared) / (minRadius + scaleSquared) - 1e-6 : 1;
      this.clippedDepths[i].max = positions?.count ? (maxRadius - scaleSquared) / (maxRadius + scaleSquared) + 1e-6 : 1;
      if (center) {
        // Lift the CPU drawing's sorting anchor back to S³. Static pieces keep
        // exactly the previous sorting position; moving anchors follow SO(4).
        const x = center.x / scale, y = center.y / scale, z = center.z / scale;
        const squared = x * x + y * y + z * z;
        this.sortAnchors[i].set(2 * x, 2 * y, 2 * z, squared - 1).multiplyScalar(1 / (squared + 1));
        this.sortAnchors[i].applyMatrix4(this.pose.copy(this.bases[i]).transpose());
      } else this.sortAnchors[i].fromArray(this.intrinsic.descriptor.visualParts[i].boundingCap.center);
    }
  }
  private combine(): void {
    this.view.fromArray(this.geometry.scene.projection!.viewIsometry);
    for (let i = 0; i < this.combined.length; i++) {
      this.pose.fromArray(this.geometry.isometries, i * 16);
      this.combined[i].multiplyMatrices(this.view, this.pose);
    }
  }
  update(): boolean {
    this.combine();
    const { scale, clipRadius } = this.geometry.scene.projection!;
    const clip = (clipRadius * clipRadius - 1) / (clipRadius * clipRadius + 1);
    // Certify the whole curved triangle, not just the sampled vertices. The
    // margin also covers Float32 attributes/uniforms near the clipping plane.
    for (let i = 0; i < this.meshes.length; i++) {
      const { boundingCap } = this.intrinsic.descriptor.visualParts[i];
      const matrix = this.combined[i].elements;
      let depth = 0;
      for (let j = 0; j < 4; j++) depth += matrix[j * 4 + 3] * boundingCap.center[j];
      let upper = capUpperW(depth, boundingCap.minDot);
      // Every positive cut is a convex spherical cap. Its bound can certify a
      // moving piece even when the looser center/radius cap reaches the pole.
      for (const cut of this.positiveCuts[i]) {
        let w = 0;
        for (let j = 0; j < 4; j++) w += matrix[j * 4 + 3] * cut.normal[j];
        upper = Math.min(upper, capUpperW(w, cut.minimum));
      }
      if (upper < clip - 1e-5) this.modes[i] = 1;
      else if (clippedMeshRotation(matrix, this.bases[i].elements, this.rotations[i])) this.modes[i] = 0;
      else return false;
    }
    const layout = this.geometry.scene.controlLayout;
    const guide = this.geometry.projectionOptions.guide;
    const group = layout?.version === 2 ? layout.groups.find(g => g.id === guide?.group) : undefined;
    this.guidePieces.clear();
    for (let i = 0; i < this.meshes.length; i++) {
      const mesh = this.meshes[i], part = this.intrinsic.descriptor.visualParts[i];
      const uniform = this.uniforms[i];
      uniform.s3Enabled.value = this.modes[i] === 1;
      uniform.s3Pose.value.copy(this.combined[i]); uniform.s3Scale.value = scale;
      uniform.s3FadeStart.value = this.startRadius;
      uniform.s3FadeEnd.value = this.endRadius;
      mesh.geometry = this.modes[i] ? this.assets.get(part.meshAssetId)! : this.cpuAssets.get(mesh.userData.meshAssetId)!;
      if (this.modes[i]) mesh.matrix.identity(); else mesh.matrix.copy(this.rotations[i]);
      mesh.matrixWorldNeedsUpdate = true;
      let depth = 0;
      for (let j = 0; j < 4; j++) depth += this.combined[i].elements[j * 4 + 3] * part.boundingCap.center[j];
      this.pieceDepths[part.pieceId] = depth;
      // Membership follows the full active cut set, including uncolored cuts.
      // In the focused chart, -W is the focused center. The next alignment ring
      // consists exactly of its face neighbors; later rings are hidden.
      let alignment = -1;
      for (const cut of this.positiveCuts[i]) {
        let w = 0;
        for (let j = 0; j < 4; j++) w += this.combined[i].elements[j * 4 + 3] * cut.normal[j];
        alignment = Math.max(alignment, -w);
      }
      const cellVisibility = s3CellVisibility(alignment, this.fadeRange.distantAlignment, this.fadeRange.adjacentAlignment);
      uniform.s3CellVisibility.value = cellVisibility;
      // The pole-containing piece has an unbounded image and remains hidden.
      let polePiece = true;
      if (part.cutHalfspaces) {
        for (const cut of part.cutHalfspaces) {
          let w = 0;
          for (let j = 0; j < 4; j++) w += this.combined[i].elements[j * 4 + 3] * cut.normal[j];
          polePiece &&= w > cut.minimum;
        }
      } else for (let j = 0; j < 4; j++) {
        const w = this.combined[i].elements[j * 4 + 3];
        polePiece &&= j < part.activeCoordinates! ? w > this.cutDepth : Math.abs(w) < this.cutDepth;
      }
      uniform.s3PolePiece.value = polePiece;
      const opacity = this.opacityRanges[i];
      if (polePiece || cellVisibility === 0) opacity.min = opacity.max = 0;
      else if (this.modes[i]) {
        // A box bounds the linear W of every GPU vertex. Interpolated opacity
        // stays within its vertex range, so opaque meshes can write depth.
        const bounds = this.coordinateBounds.get(part.meshAssetId)!;
        let minW = 0, maxW = 0;
        for (let j = 0; j < 4; j++) {
          const coefficient = this.combined[i].elements[j * 4 + 3];
          minW += coefficient * (coefficient >= 0 ? bounds.min[j] : bounds.max[j]);
          maxW += coefficient * (coefficient >= 0 ? bounds.max[j] : bounds.min[j]);
        }
        // Tight intrinsic caps also certify opacity for half-integral frames;
        // a coordinate box alone can put an opaque inner piece in the fade pass.
        for (const cut of this.positiveCuts[i]) {
          let w = 0;
          for (let j = 0; j < 4; j++) w += this.combined[i].elements[j * 4 + 3] * cut.normal[j];
          maxW = Math.min(maxW, capUpperW(w, cut.meshMinimum));
          minW = Math.max(minW, -capUpperW(-w, cut.meshMinimum));
        }
        opacity.min = cellVisibility * s3Opacity(maxW + 1e-6, this.startRadius, this.endRadius);
        opacity.max = cellVisibility * s3Opacity(minW - 1e-6, this.startRadius, this.endRadius);
      } else {
        opacity.min = cellVisibility * s3Opacity(this.clippedDepths[i].max, this.startRadius, this.endRadius);
        opacity.max = cellVisibility * s3Opacity(this.clippedDepths[i].min, this.startRadius, this.endRadius);
      }
      if (this.modes[i]) {
        const center = this.center.copy(this.sortAnchors[i]).applyMatrix4(this.combined[i]);
        if (1 - center.w < 1e-6) center.fromArray(part.boundingCap.center).applyMatrix4(this.combined[i]);
        this.bounds[i].center.set(center.x, center.y, center.z).multiplyScalar(scale / (1 - center.w));
        this.bounds[i].radius = scale * clipRadius;
      } else {
        const sphere = mesh.geometry.boundingSphere;
        if (sphere) this.bounds[i].copy(sphere);
        else { this.bounds[i].center.set(0, 0, 0); this.bounds[i].radius = scale * clipRadius; }
      }
      if (group) {
        for (const { normal } of this.positiveCuts[i]) {
          let alignment = 0;
          for (let j = 0; j < 4; j++) for (let k = 0; k < 4; k++)
            alignment += this.geometry.isometries[i * 16 + j * 4 + k] * normal[j] * group.center[k];
          if (alignment > 1 - 1e-8) this.guidePieces.add(part.pieceId);
        }
      }
    }
    return true;
  }
  /** Faded surfaces let selection through to the opaque inner cell. */
  opaqueAt(point: THREE.Vector3): boolean {
    const scale = this.geometry.scene.projection!.scale, radius = point.lengthSq();
    return radius <= (scale * this.startRadius) ** 2 + 1e-6;
  }
  dispose(): void { for (const asset of this.assets.values()) asset.dispose(); }
}
