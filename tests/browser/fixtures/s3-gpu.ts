// Bundled only by the browser test. Uses the published SDK and the application's
// real shader adapter to compare rendered pixels against portable CPU drawings.
import * as THREE from 'three';
import { createKernel, copyIntrinsicMeshes, GeometryInstance, KernelSession } from '../../../kernel/index';
import { patchS3Material, S3GpuProjection } from '../../../web/src/s3Gpu';
import { s3FadeRange } from '../../../web/src/s3Fade';
import { S3MaterialPasses, type S3Mesh } from '../../../web/src/s3Materials';
import type { CellAxisLayout } from '../../../kernel/index';
import { RenderView } from '../../../web/src/renderer';

export async function compareS3Projection(base: string) {
  const module = await createKernel({ wasmURL: new URL('kernel/generated/twisty.wasm', base) });
  const data = new URL('kernel/examples/canvas/data/tesseract3/', base);
  const session = new KernelSession(module, await (await fetch(new URL('package.json', data))).text());
  session.lightweightResults();
  const geometry = new GeometryInstance(module, session.native, await (await fetch(new URL('tesseract3-s3.json', data))).text());
  const intrinsic = copyIntrinsicMeshes(geometry.definition).descriptor;
  const fade = s3FadeRange(1 / 3, (geometry.scene.controlLayout as CellAxisLayout).groups.map(g => g.center));
  let startRadius = fade.minRadius;
  const renderer = new THREE.WebGLRenderer({ antialias: false });
  renderer.setSize(400, 400); renderer.outputColorSpace = THREE.SRGBColorSpace;
  const target = new THREE.WebGLRenderTarget(400, 400);
  const cpu = new THREE.Scene(), gpuScene = new THREE.Scene();
  for (const scene of [cpu, gpuScene]) {
    scene.background = new THREE.Color('#17212a');
    scene.add(new THREE.AmbientLight(0xffffff, 2.4));
    const light = new THREE.DirectionalLight(0xffffff, 3.2); light.position.set(4, 7, 6); scene.add(light);
  }
  const camera = new THREE.PerspectiveCamera(35, 1, .1, 100);
  camera.position.set(5, 4.2, 6).multiplyScalar(.62); camera.lookAt(0, 0, 0);
  const assets = new Map(geometry.scene.meshAssets.map(a => [a.id, new THREE.BufferGeometry()]));
  function upload() {
    for (const [partIndex, a] of geometry.scene.meshAssets.entries()) {
      const mesh = assets.get(a.id)!;
      mesh.setAttribute('position', new THREE.BufferAttribute(geometry.positions.slice(a.positionOffset, a.positionOffset + 3 * a.vertexCount), 3));
      mesh.setAttribute('normal', new THREE.BufferAttribute(geometry.normals.slice(a.positionOffset, a.positionOffset + 3 * a.vertexCount), 3));
      mesh.setIndex(new THREE.BufferAttribute(geometry.indices.slice(a.indexOffset, a.indexOffset + a.indexCount), 1));
      // Independent CPU reference: interpolate RGBA vertex colors using the
      // projected radius and active cut directions instead of custom shaders.
      const colors = new Float32Array(a.vertexCount * 4), scale = geometry.scene.projection!.scale;
      const combined = new THREE.Matrix4().multiplyMatrices(new THREE.Matrix4().fromArray(geometry.scene.projection!.viewIsometry), new THREE.Matrix4().fromArray(geometry.isometries, partIndex * 16));
      let alignment = -1;
      for (let j = 0; j < intrinsic.visualParts[partIndex].activeCoordinates!; j++) alignment = Math.max(alignment, -combined.elements[j * 4 + 3]);
      const visibility = THREE.MathUtils.smoothstep(alignment, fade.distantAlignment, fade.adjacentAlignment);
      for (let i = 0; i < a.vertexCount; i++) {
        const offset = a.positionOffset + 3 * i;
        const radius = (geometry.positions[offset] ** 2 + geometry.positions[offset + 1] ** 2 + geometry.positions[offset + 2] ** 2) / (scale * scale);
        colors.set([1, 1, 1, visibility * (1 - THREE.MathUtils.smoothstep(Math.sqrt(radius), startRadius, fade.endRadius))], 4 * i);
      }
      mesh.setAttribute('color', new THREE.BufferAttribute(colors, 4));
      mesh.computeBoundingSphere();
    }
  }
  upload();
  const makeMeshes = (scene: THREE.Scene, vertexColors = false) => geometry.scene.visualParts.map(p => {
    const mesh = new THREE.Mesh(assets.get(p.meshAssetId), new THREE.MeshStandardMaterial({ color: p.materialBindingId, side: THREE.DoubleSide, roughness: .55, metalness: .1, vertexColors }));
    mesh.matrixAutoUpdate = false; mesh.userData = { meshAssetId: p.meshAssetId, pieceId: p.pieceId };
    scene.add(mesh); return mesh;
  });
  const cpuMeshes = makeMeshes(cpu, true), gpuMeshes = makeMeshes(gpuScene);
  const cpuMaterials = cpuMeshes.map(m=>m.material), gpuMaterials = gpuMeshes.map(m=>m.material);
  const passes = new S3MaterialPasses();
  const gpu = new S3GpuProjection(geometry, gpuMeshes, assets);
  function pixels(scene: THREE.Scene, meshes: S3Mesh[], opaqueOnly: boolean, cached = true) {
    for (let i = 0; i < meshes.length; i++) {
      const mesh = meshes[i], range = gpu.opacityRanges[i], faded = range.min < 1;
      const material = (meshes === gpuMeshes ? gpuMaterials : cpuMaterials)[i];
      mesh.visible = range.max > 0 && (!opaqueOnly || !faded);
      material.opacity = 1;
      material.depthWrite = !faded;
      if (material.transparent !== faded) { material.transparent = faded; material.needsUpdate = true; }
      mesh.material = material;
      if (cached && meshes === gpuMeshes) passes.sync(mesh,material);
    }
    renderer.setRenderTarget(target); renderer.render(scene, camera);
    const pixels = new Uint8Array(400 * 400 * 4);
    renderer.readRenderTargetPixels(target, 0, 0, 400, 400, pixels);
    return pixels;
  }
  const reports: { focusCell: string; operation: string; opaqueOnly: boolean; meanError: number; differentPixels: number }[] = [];
  let materialPassMaxError = 0;
  try {
    for (const [index, focusCell] of ['Wm', 'Xp', 'Ym', 'Zp', 'Wp'].entries()) {
      startRadius = index % 2 ? fade.maxStartRadius : fade.minRadius;
      gpu.setFadeStartRadius(startRadius);
      geometry.setProjection({ focusCell }); upload(); gpu.captureDrawing();
      for (const operation of ['Xp_fY', 'Wp_fX', session.definition.operations.find(o => o.family === 'Zm' && o.id.includes('_v'))!.id]) {
        const plan = session.plan({ operation, parameters: {} });
        geometry.prepare(plan.transitionRecord ?? JSON.stringify(plan.transition), false);
        geometry.evaluatePose(.37);
        if (!gpu.update()) throw new Error(`Unexpected clipping fallback: ${focusCell}/${operation}`);
        // Draw GPU before updating its cached CPU clipping buffers.
        const all = pixels(gpuScene, gpuMeshes, false), opaque = pixels(gpuScene, gpuMeshes, true);
        const uncached = pixels(gpuScene,gpuMeshes,false,false);
        for (let i=0;i<all.length;i++) materialPassMaxError=Math.max(materialPassMaxError,Math.abs(all[i]-uncached[i]));
        geometry.refreshProjection(); upload();
        for (const opaqueOnly of [true, false]) {
          const reference = pixels(cpu, cpuMeshes, opaqueOnly), actual = opaqueOnly ? opaque : all;
          let error = 0, different = 0;
          for (let i = 0; i < actual.length; i += 4) {
            let largest = 0;
            for (let j = 0; j < 3; j++) { const d = Math.abs(actual[i + j] - reference[i + j]); error += d; largest = Math.max(largest, d); }
            if (largest > 10) different++;
          }
          reports.push({ focusCell, operation, opaqueOnly, meanError: error / (400 * 400 * 3), differentPixels: different / (400 * 400) });
        }
        gpu.captureDrawing();
      }
    }
    // Comparable warmed main-thread work, excluding the renderer/GPU draw.
    for (let i = 0; i < 20; i++) { geometry.evaluatePose(i / 20); gpu.update(); }
    let start = performance.now();
    for (let i = 0; i < 120; i++) { geometry.evaluatePose((i % 120) / 120); gpu.update(); }
    const gpuFrameMs = (performance.now() - start) / 120;
    start = performance.now();
    for (let i = 0; i < 12; i++) geometry.evaluate(i / 12);
    const cpuFrameMs = (performance.now() - start) / 12;
    const opacityProbes = probeOpacity(renderer, target);
    return { reports, gpuFrameMs, cpuFrameMs, opacityProbes, materialPassMaxError };
  } finally {
    gpu.dispose(); geometry.dispose(); session.dispose();
    passes.dispose();
    for (const material of [...cpuMaterials, ...gpuMaterials]) material.dispose();
    for (const a of assets.values()) a.dispose();
    target.dispose(); renderer.dispose(); renderer.forceContextLoss();
  }
}

/** Exercise the production pass cache with both puzzle families, including
 * selected opaque surfaces and CPU meshes whose clipping topology changes. */
export async function compareS3MaterialViews(base: string, puzzle: 'cell24' | 'cell24-192' | 'tesseract3') {
  const module=await createKernel({wasmURL:new URL('kernel/generated/twisty.wasm',base)});
  const data=new URL(`kernel/examples/canvas/data/${puzzle}/`,base);
  const session=new KernelSession(module,await (await fetch(new URL('package.json',data))).text());
  session.lightweightResults();
  // Exercise transported colors without enumerating all 552 requests at each
  // step of the 24-cell's general-purpose random scrambler.
  if(puzzle.startsWith('cell24')){
    if(session.run('Xp_fXYZ Hpppp_eXY Yp_vX Hpmmp_vZ','transactional').status!=='Committed')throw new Error('Cannot prepare mixed 24-cell state.');
  }
  else session.scramble(41,7);
  const realization=await (await fetch(new URL(`${puzzle}-s3.json`,data))).json();
  const host=document.createElement('div');host.style.cssText='width:400px;height:300px';document.body.append(host);
  const view=new RenderView(module,host,session.native,realization,()=>{},()=>false,()=>{});
  // Private access is confined to this browser fixture: compare the same
  // production objects with the previous automatic DoubleSide draw path.
  const internals=view as unknown as {
    frame:number;resizeObserver:ResizeObserver;renderer:THREE.WebGLRenderer;meshes:S3Mesh[];
    materials:THREE.MeshStandardMaterial[];materialPasses:S3MaterialPasses;gpu:S3GpuProjection;
    assets:Map<string,THREE.BufferGeometry>;updateTransforms:()=>void;
  };
  cancelAnimationFrame(internals.frame);internals.resizeObserver.disconnect();
  const target=new THREE.WebGLRenderTarget(400,300);
  const renderer=internals.renderer;
  const reports:{focus:string;operation:string;style:string;maxError:number;calls:number}[]=[];
  const pixels=(cached:boolean)=>{
    internals.meshes.forEach((mesh,i)=>{
      const material=internals.materials[i];mesh.material=material;
      if(cached)internals.materialPasses.sync(mesh,material);
    });
    renderer.setRenderTarget(target);renderer.render(view.scene,view.camera);
    const pixels=new Uint8Array(400*300*4);renderer.readRenderTargetPixels(target,0,0,400,300,pixels);
    return pixels;
  };
  const compare=(focus:string,operation:string,style:string)=>{
    const actual=pixels(true),calls=renderer.info.render.calls,expected=pixels(false);
    let maxError=0;for(let i=0;i<actual.length;i++)maxError=Math.max(maxError,Math.abs(actual[i]-expected[i]));
    reports.push({focus,operation,style,maxError,calls});
  };
  try {
    view.setState(session.stateText());
    const layout=realization.controlLayout as CellAxisLayout;
    const operations=['Xp','Wp','Ym'].map((id,i)=>layout.groups.find(g=>g.id===id)!.categories[i].axes[0].turns[0].operation);
    for(const [index,focus] of (puzzle.startsWith('cell24')?['Wm','Xp','Hpppp']:['Wm','Xp','Ym']).entries()){
      view.focusCell(focus);
      const operation=operations[index], plan=session.plan({operation,parameters:{}});
      view.prepare(plan.transitionRecord??JSON.stringify(plan.transition));view.sample(.37);
      const piece=view.geometry.scene.visualParts.find((_,i)=>internals.gpu.opacityRanges[i].min===1)!.pieceId;
      view.highlight([],[]);compare(focus,operation,'ordinary');
      view.highlight([piece],[]);compare(focus,operation,'selected');
      view.highlight([],[piece]);compare(focus,operation,'blocked');
    }
    view.highlight([],[]);
    if(puzzle==='tesseract3'){
      view.setProjection({...view.geometry.projectionOptions,clipRadius:1.1});
      view.sample(.61);compare('clipped',operations.at(-1)!,'changed topology');
      view.sample(.23);compare('clipped',operations.at(-1)!,'changed topology again');
    }else{
      // Exercise the same cached-CPU shader branch with the portable 24-cell
      // drawing. Deep geometric clipping is an expensive separate kernel task;
      // its changing index counts are already covered above and in unit tests.
      view.geometry.refreshProjection();internals.updateTransforms();
      const uniforms=(internals.gpu as unknown as {uniforms:{s3Enabled:{value:boolean}}[]}).uniforms;
      internals.meshes.forEach((mesh,i)=>{
        mesh.geometry=internals.assets.get(mesh.userData.meshAssetId)!;
        mesh.matrix.identity();mesh.matrixWorldNeedsUpdate=true;uniforms[i].s3Enabled.value=false;
      });
      compare('portable CPU drawing',operations.at(-1)!,'ordinary');
      const piece=view.geometry.scene.visualParts.find((_,i)=>internals.gpu.opacityRanges[i].min===1)!.pieceId;
      view.highlight([piece],[]);compare('portable CPU drawing',operations.at(-1)!,'selected');
    }
    return {puzzle,reports};
  } finally {target.dispose();view.dispose();session.dispose();host.remove();}
}

function probeOpacity(renderer: THREE.WebGLRenderer, target: THREE.WebGLRenderTarget) {
  const scene = new THREE.Scene();
  scene.add(new THREE.AmbientLight(0xffffff, 2));
  const camera = new THREE.OrthographicCamera(-1, 1, 1, -1, .1, 100);
  camera.position.set(0, 0, 20); camera.lookAt(0, 0, 0);
  const material = new THREE.MeshStandardMaterial({ transparent: true, depthWrite: false });
  const fade = s3FadeRange(2 / 3, [[1,0,0,0],[-1,0,0,0],[.5,.5,.5,.5],[0,1,0,0]]);
  const uniforms = patchS3Material(material, fade.minRadius, fade.endRadius);
  const mesh = new THREE.Mesh(new THREE.BufferGeometry(), material);
  mesh.frustumCulled = false; scene.add(mesh);
  renderer.setClearColor(0x000000, 0);
  function render(ws: number[], enabled: boolean, size: number) {
    const positions: number[] = [], intrinsic: number[] = [], normals: number[] = [];
    for (let i = 0; i < 3; i++) {
      const w = ws[i], radius = (1 + w) / (1 - w);
      const x = [-size, size, 0][i], y = [-size, -size, size][i], z = Math.sqrt(radius - x * x - y * y);
      positions.push(x, y, z);
      const p = [x * (1 - w), y * (1 - w), z * (1 - w), w]; intrinsic.push(...p);
      const length = Math.sqrt(1 - w * w);
      normals.push(...p.map((v, j) => ((j === 3 ? 1 : 0) - w * v) / length));
    }
    mesh.geometry.dispose(); mesh.geometry = new THREE.BufferGeometry();
    mesh.geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    mesh.geometry.setAttribute('normal', new THREE.Float32BufferAttribute([0, 0, 1, 0, 0, 1, 0, 0, 1], 3));
    mesh.geometry.setAttribute('intrinsicPosition', new THREE.Float32BufferAttribute(intrinsic, 4));
    mesh.geometry.setAttribute('intrinsicNormal', new THREE.Float32BufferAttribute(normals, 4));
    uniforms.s3Enabled.value = enabled;
    renderer.setRenderTarget(target); renderer.render(scene, camera);
    const sample = (x: number, y: number) => {
      const pixel = new Uint8Array(4);
      renderer.readRenderTargetPixels(target, Math.round((x + 1) * 200), Math.round((y + 1) * 200), 1, 1, pixel);
      return pixel[3] / 255;
    };
    return { center: sample(0, 0), left: sample(-size / 2, -size / 2), top: sample(0, size / 2) };
  }
  try {
    const samples = [];
    for (const enabled of [true, false]) for (const w of [-.95, -2 / 3, -.15, 0, .15, 1 / 3, .95])
      samples.push({ enabled, w, alpha: render([w, w, w], enabled, .05).center });
    const gradient = render([-.9, -.2, .4], true, .1);
    const beforeSlider = render([0,0,0], true, .05).center;
    uniforms.s3FadeStart.value = fade.maxStartRadius;
    const afterSlider = render([0,0,0], true, .05).center;
    uniforms.s3FadeStart.value = fade.minRadius;
    uniforms.s3FadeEnd.value = .46;
    const afterEndSlider = [render([0,0,0], true, .05).center, render([0,0,0], false, .05).center];
    const focusedAfterEndSlider = render([-.9,-.9,-.9], true, .05).center;
    uniforms.s3FadeEnd.value = fade.endRadius;
    // An opaque vertex must not leak alpha across a triangle past the cutoff.
    const straddlingCutoff = [render([-.9,.95,.95], true, .1).center, render([-.9,.95,.95], false, .1).center];
    uniforms.s3CellVisibility.value = 0;
    const outsideNeighbors = render([-.9,-.9,-.9], true, .05).center;
    uniforms.s3CellVisibility.value = 1;
    uniforms.s3PolePiece.value = true;
    const hiddenPole = [render([0, 0, 0], true, .05).center, render([0, 0, 0], false, .05).center];
    return { samples, gradient, hiddenPole, beforeSlider, afterSlider, afterEndSlider, focusedAfterEndSlider, straddlingCutoff, outsideNeighbors, fade };
  } finally { mesh.geometry.dispose(); material.dispose(); }
}
