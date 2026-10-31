import {runtimeReference} from './runtime-reference';
import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';

async function resting(page:Page):Promise<void>{
  await expect.poll(async()=>{
    if(await page.getByTestId('app').getAttribute('data-animating')!=='false')return false;
    const digest=await page.getByTestId('state-digest').textContent();
    for(const id of ['cube-view','diagram-view'])
      if(await page.getByTestId(id).getAttribute('data-state-digest')!==digest)return false;
    return true;
  },{timeout:15000}).toBe(true);
  await expect(page.getByRole('alert')).toHaveCount(0);
}

async function worldClick(page:Page,id:string,x:number,y:number):Promise<void>{
  const host=page.getByTestId(id);await host.scrollIntoViewIfNeeded();const r=(await host.boundingBox())!;
  const tiled=id==='diagram-view',factor=tiled?3:1;
  const w=(tiled?Math.min:Math.max)(1.5*.57*factor,Math.sqrt(3)/2*.57*factor*r.width/r.height),h=w*r.height/r.width;
  const cx=Number(await host.getAttribute('data-camera-x')),cy=Number(await host.getAttribute('data-camera-y'));
  const zoom=Number(await host.getAttribute('data-camera-zoom'));
  await host.locator('canvas').click({position:{x:(1+(x-cx)*zoom/w)*r.width/2,y:(1-(y-cy)*zoom/h)*r.height/2}});
}

for(const six of [false,true])test(`333 ${six?6:3} disks: repeated images, turn orders, animation and saved sessions`,async({page})=>{
  test.setTimeout(180_000);
  const puzzle=`flat-333${six?'-6':''}`,errors:string[]=[];
  const bundle=JSON.parse(readFileSync(`packages/${puzzle}/package.json`,'utf8'));
  const centerPiece=(color:string)=>Object.entries(bundle.catalog.pieces).find(([,p]:[string,any])=>
    ['cone','face'].includes(p.domain)&&Object.values(p.portLabels).every(label=>String(label).split('-')[0]===color))![0];
  page.on('pageerror',error=>errors.push(error.message));
  await page.goto('./');await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/,{timeout:15000});
  await page.getByLabel('Puzzle',{exact:true}).selectOption(puzzle);await resting(page);
  await page.getByLabel('Animation speed').selectOption('0');
  for(const id of ['cube-view','diagram-view'])await expect(page.getByTestId(id)).toHaveAttribute('data-topology','orbifold-333');
  await page.screenshot({path:`test-results/${puzzle}-solved.png`,fullPage:true});
  const origin=await page.getByTestId('state-digest').textContent();
  await worldClick(page,'cube-view',.03,.03);
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-selected-piece',centerPiece('A'));
  await page.keyboard.press('Escape');
  await worldClick(page,'diagram-view',1.53,Math.sqrt(3)/2+.03);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece',centerPiece('A'));
  await page.keyboard.press('Escape');
  await worldClick(page,'cube-view',.1,.75);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece','');
  await page.getByRole('button',{name:'Move A',exact:true}).click();await resting(page);
  await expect(page.getByTestId('state-digest')).not.toHaveText(origin!);
  await page.getByRole('button',{name:'Move A',exact:true}).click();await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(origin!);
  if(six){
    await worldClick(page,'cube-view',.75,Math.sqrt(3)/4);
    await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-selected-piece',centerPiece('E'));
    await page.keyboard.press('Escape');
    await page.keyboard.press('d');await resting(page);
    await expect(page.getByTestId('state-digest')).not.toHaveText(origin!);
    await page.keyboard.press('Shift+D');await resting(page);
    await expect(page.getByTestId('state-digest')).toHaveText(origin!);
    await page.getByLabel('Algorithm',{exact:true}).fill('(D)6 (E3)2 F2 F2\'');
    await page.getByRole('button',{name:'Play algorithm →'}).click();await resting(page);
    await expect(page.getByTestId('state-digest')).toHaveText(origin!);
  }
  await page.getByLabel('Animation speed').selectOption('450');
  const word=six?'A D E F2':'A2 B C';
  await page.getByLabel('Algorithm',{exact:true}).fill(word);
  await page.getByRole('button',{name:'Play algorithm →'}).click();
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating','true');
  await page.waitForTimeout(150);
  await page.screenshot({path:`test-results/${puzzle}-motion.png`,fullPage:true});
  await resting(page);
  const native=runtimeReference(['run','--definition',`packages/${puzzle}/package.json`,'--algorithm',word,'--json']);
  await expect(page.getByTestId('state-digest')).toHaveText(native.snapshot.stateDigest);
  await page.getByLabel('Animation speed').selectOption('0');
  const download=page.waitForEvent('download');await page.getByRole('button',{name:'Save session ↗'}).click();
  const saved=(await(await download).path())!;
  await page.getByRole('button',{name:'↶ Undo',exact:true}).click();await resting(page);
  await page.getByRole('button',{name:'↷ Redo',exact:true}).click();await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(native.snapshot.stateDigest);
  await page.getByLabel('Puzzle',{exact:true}).selectOption('cube3');await resting(page);
  await page.getByLabel('Puzzle',{exact:true}).selectOption(puzzle);await resting(page);
  await page.getByLabel('Open session file').setInputFiles(saved);await resting(page);
  await expect(page.getByLabel('Puzzle',{exact:true})).toHaveValue(puzzle);
  await expect(page.getByTestId('state-digest')).toHaveText(native.snapshot.stateDigest);
  await page.screenshot({path:`test-results/${puzzle}-resting.png`,fullPage:true});
  // Restore a far translated camera so both rotation cosets and the image
  // pool's recycled translation addresses must work outside the original pool.
  const far=JSON.parse(readFileSync(saved,'utf8')),tx=4.5,ty=-13*Math.sqrt(3)/2;
  far.presentation.cameras[1]={position:[.75+tx,Math.sqrt(3)/4+ty,15],target:[.75+tx,Math.sqrt(3)/4+ty,0],zoom:1};
  await page.getByLabel('Open session file').setInputFiles({name:'far-333.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(far))});await resting(page);
  await expect.poll(async()=>Number(await page.getByTestId('diagram-view').getAttribute('data-camera-x'))).toBeCloseTo(.75+tx,8);
  // Center locations stay fixed throughout the saved scramble.
  await worldClick(page,'diagram-view',.03+tx,.03+ty);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece',centerPiece('A'));
  await page.keyboard.press('Escape');
  expect(errors).toEqual([]);
});

test('333 six-disk example uses intersecting disks in both clients',async({page})=>{
  test.setTimeout(90_000);
  await page.goto('./');await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/,{timeout:15000});
  await page.getByLabel('Puzzle',{exact:true}).selectOption('flat-333-6');await resting(page);
  await expect(page.getByLabel('Algorithm',{exact:true})).toHaveValue("A D A' D'");
  await page.getByLabel('Animation speed').selectOption('0');
  const initial=await page.getByTestId('state-digest').textContent();
  await page.getByRole('button',{name:'Play algorithm →'}).click();
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);await resting(page);
  await page.goto('kernel/examples/canvas/');await expect(page.getByRole('status')).toHaveText('Ready');
  await page.getByLabel('Puzzle',{exact:true}).selectOption('flat-333-6');await expect(page.getByRole('status')).toHaveText('Ready');
  await expect(page.locator('#notation')).toHaveValue("A D A' D'");
});
