import * as THREE from 'three';
import { expect, it } from 'vitest';
import { S3MaterialPasses, type S3Mesh } from '../../web/src/s3Materials';
import { patchS3Material } from '../../web/src/s3Gpu';

it('keeps cached back/front materials stable while selection, depth and poses change', () => {
  const geometry = new THREE.BufferGeometry();
  geometry.setIndex([0,1,2,0,2,3]);
  const source = new THREE.MeshStandardMaterial({side:THREE.DoubleSide,transparent:true,depthWrite:false});
  const uniforms = patchS3Material(source,.45,1.39);
  const mesh: S3Mesh = new THREE.Mesh(geometry,source);
  const passes = new S3MaterialPasses();
  try {
    passes.sync(mesh,source);
    const pair = mesh.material as THREE.MeshStandardMaterial[];
    expect(pair.map(m=>m.side)).toEqual([THREE.BackSide,THREE.FrontSide]);
    expect(geometry.groups).toEqual([{start:0,count:6,materialIndex:0},{start:0,count:6,materialIndex:1}]);
    for (const material of pair) {
      expect(material.onBeforeCompile).toBe(source.onBeforeCompile);
      expect(material.customProgramCacheKey()).toBe(source.customProgramCacheKey());
    }
    const versions = pair.map(m=>m.version);
    source.color.set('#ffcc55');source.emissive.set('#aa2200');source.emissiveIntensity=.45;source.depthWrite=true;
    uniforms.s3Pose.value.makeRotationX(.4);
    passes.sync(mesh,source);
    expect(mesh.material).toBe(pair);
    expect(pair.map(m=>m.version)).toEqual(versions);
    for (const material of pair) {
      expect(material.color.equals(source.color)).toBe(true);
      expect(material.emissive.equals(source.emissive)).toBe(true);
      expect(material.emissiveIntensity).toBe(.45);
      expect(material.depthWrite).toBe(true);
      const shader = {uniforms:{},vertexShader:'#include <beginnormal_vertex>\n#include <begin_vertex>',fragmentShader:'#include <color_fragment>'};
      material.onBeforeCompile(shader as THREE.WebGLProgramParametersWithUniforms, {} as THREE.WebGLRenderer);
      expect((shader.uniforms as Record<string,unknown>).s3Pose).toBe(uniforms.s3Pose);
    }
    source.transparent=false;passes.sync(mesh,source);expect(mesh.material).toBe(source);
    source.transparent=true;passes.sync(mesh,source);expect(mesh.material).toBe(pair);
    // Clipped CPU drawings may change their index count without changing identity.
    geometry.setIndex([0,1,2]);passes.sync(mesh,source);
    expect(geometry.groups.map(g=>g.count)).toEqual([3,3]);
    mesh.visible=false;passes.sync(mesh,source);expect(mesh.material).toBe(source);
    mesh.visible=true;passes.sync(mesh,source);expect(mesh.material).toBe(pair);
    let disposed=0;pair.forEach(m=>m.addEventListener('dispose',()=>disposed++));
    passes.dispose();expect(disposed).toBe(2);
    expect(source.version).toBe(0);
  } finally {passes.dispose();source.dispose();geometry.dispose();}
});
