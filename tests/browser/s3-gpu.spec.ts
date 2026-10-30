import { test, expect, type Page } from '@playwright/test';
import { build } from 'vite';
import { writeFile } from 'node:fs/promises';

async function loadFixture(page: Page, baseURL: string) {
  // Bundle the actual adapter with Three.js; consume the published standalone
  // kernel over HTTP. The fixture never becomes part of the app or deployment.
  const result = await build({ configFile: false, logLevel: 'silent', plugins: [{
    name: 'published-kernel', resolveId(source) {
      if (source.endsWith('/kernel/index')) return { id: new URL('kernel/index.js', baseURL!).href, external: true };
    },
  }], build: { write: false, minify: false, lib: { entry: 'tests/browser/fixtures/s3-gpu.ts', formats: ['es'] } } });
  const output = Array.isArray(result) ? result[0] : result;
  if (!('output' in output)) throw new Error('Expected a browser fixture bundle');
  const chunks = output.output.filter(output => output.type === 'chunk');
  const chunk = chunks.find(output => output.isEntry)!;
  await page.route('**/gpu-fixture/*.js', route => {
    const code = chunks.find(chunk => route.request().url().endsWith(`/${chunk.fileName}`))?.code;
    return code ? route.fulfill({ contentType: 'text/javascript', body: code }) : route.abort();
  });
  await page.route('**/gpu-fixture.html', route => route.fulfill({ contentType: 'text/html', body: '<!doctype html><title>S3 GPU check</title>' }));
  await page.goto(new URL('gpu-fixture.html', baseURL!).href);
  return new URL(`gpu-fixture/${chunk.fileName}`,baseURL).href;
}

test('GPU shader pixels agree with CPU stereographic projection, including cached clipping', async ({ page, baseURL }, testInfo) => {
  test.setTimeout(90_000);
  const errors: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
  const entry=await loadFixture(page,baseURL!);
  const metrics = await page.evaluate(async ({ entry, base }) =>
    (await import(/* @vite-ignore */ entry)).compareS3Projection(base),
    { entry, base: baseURL! });
  const reportPath = testInfo.outputPath('s3-gpu-comparison.json');
  await writeFile(reportPath, JSON.stringify(metrics, null, 2));
  await testInfo.attach('s3-gpu-comparison.json', { path: reportPath, contentType: 'application/json' });
  expect(errors).toEqual([]);
  expect(metrics.reports).toHaveLength(30);
  expect(metrics.materialPassMaxError).toBe(0);
  for (const report of metrics.reports) {
    expect(report.meanError, JSON.stringify(report)).toBeLessThan(.05);
    expect(report.differentPixels, JSON.stringify(report)).toBeLessThan(.001);
  }
  for (const sample of metrics.opacityProbes.samples) {
    const fade=metrics.opacityProbes.fade;
    const radius=Math.sqrt((1+sample.w)/(1-sample.w));
    const t = Math.max(0, Math.min(1, (radius-fade.minRadius)/(fade.endRadius-fade.minRadius)));
    expect(sample.alpha, JSON.stringify(sample)).toBeCloseTo(1 - t * t * (3 - 2 * t), 2);
  }
  expect(metrics.opacityProbes.hiddenPole).toEqual([0, 0]);
  expect(metrics.opacityProbes.outsideNeighbors).toBe(0);
  expect(metrics.opacityProbes.beforeSlider).toBeLessThan(.5);
  expect(metrics.opacityProbes.afterSlider).toBe(1);
  expect(metrics.opacityProbes.afterEndSlider).toEqual([0,0]);
  expect(metrics.opacityProbes.focusedAfterEndSlider).toBe(1);
  expect(metrics.opacityProbes.straddlingCutoff).toEqual([0,0]);
  expect(metrics.opacityProbes.gradient.left).toBeGreaterThan(metrics.opacityProbes.gradient.center + .2);
  expect(metrics.opacityProbes.gradient.center).toBeGreaterThan(metrics.opacityProbes.gradient.top + .1);
});

for (const puzzle of ['tesseract3','cell24','cell24-192'] as const) {
  test(`${puzzle} cached material passes preserve colors, selection and CPU drawings`,async({page,baseURL},testInfo)=>{
    test.setTimeout(90_000);
    const errors:string[]=[];
    page.on('pageerror',error=>errors.push(error.message));
    page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
    // Each test owns a fresh document so its WASM and WebGL allocations can be
    // released before the next puzzle under the workspace's 4 GiB limit.
    const entry=await loadFixture(page,baseURL!);
    const materials=await page.evaluate(async({entry,base,puzzle})=>
      (await import(/* @vite-ignore */ entry)).compareS3MaterialViews(base,puzzle),
      {entry,base:baseURL!,puzzle});
    expect(materials.reports).toHaveLength(11);
    for (const report of materials.reports) expect(report.maxError,JSON.stringify({puzzle,...report})).toBe(0);
    await testInfo.attach(`${puzzle}-material-passes.json`,{body:JSON.stringify(materials,null,2),contentType:'application/json'});
    expect(errors).toEqual([]);
  });
}
