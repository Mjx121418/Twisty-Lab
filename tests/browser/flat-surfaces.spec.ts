import { expectViews } from './view-controls';
import { expect, test, type Page } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

async function resting(page:Page):Promise<void>{
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating','false');
  const digest=await page.getByTestId('state-digest').textContent();
  for(const id of ['cube-view','diagram-view'])await expect(page.getByTestId(id)).toHaveAttribute('data-state-digest',digest!);
}

async function worldClick(page:Page,id:string,x:number,y:number,tiled:boolean,hex=false):Promise<void>{
  const host=page.getByTestId(id);await host.scrollIntoViewIfNeeded();const r=(await host.boundingBox())!;
  const factor=tiled?3:1,horizontal=(hex?3:2)*.57*factor,vertical=(hex?Math.sqrt(3):2)*.57*factor,
    w=(tiled?Math.min:Math.max)(horizontal,vertical*r.width/r.height),h=w*r.height/r.width,cx=hex?1.5:1,cy=hex?Math.sqrt(3)/2:1;
  await host.locator('canvas').click({position:{x:(1+(x-cx)/w)*r.width/2,y:(1-(y-cy)/h)*r.height/2}});
}

for(const name of ['torus','klein-bottle','hex-torus','hex-klein-bottle'])test(`${name}: picks copies and plays exact moves in both views`,async({page})=>{
  test.setTimeout(90_000);
  const hex=name.startsWith('hex-'),topology=hex?name.slice(4):name;
  const errors:string[]=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto('./');await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/,{timeout:15000});
  await page.getByLabel('Puzzle',{exact:true}).selectOption('flat-'+name);await resting(page);
  await expect(page.getByLabel('Algorithm',{exact:true})).toHaveValue("A B A' B'");
  await expect(page.locator('canvas')).toHaveCount(2);
  await expectViews(page, 'square-tiles');
  for(const [id,mode,images] of [['cube-view','square','25'],['diagram-view','tiles',hex?'117':'81']]){
    const host=page.getByTestId(id);await expect(host).toHaveAttribute('data-topology',topology);
    await expect(host).toHaveAttribute('data-surface-mode',mode);await expect(host).toHaveAttribute('data-image-count',images);
  }
  const initial=await page.getByTestId('state-digest').textContent();
  await worldClick(page,'diagram-view',hex?1:topology==='torus'?.5:1.5,hex?Math.sqrt(3):2.5,true,hex);
  for(const id of ['cube-view','diagram-view']){
    await expect(page.getByTestId(id)).toHaveAttribute('data-selected-piece','face-00');
    await expect(page.getByTestId(id)).toHaveAttribute('data-destination-operations',hex?"A,A',A2,A2',A3":"A,A',A2");
  }
  // There is geometry in the covering plane behind this margin, but the
  // displayed fundamental polygon clips it and must not allow it to be picked.
  await worldClick(page,'cube-view',hex?.1:-.08,hex?1.5:.5,false,hex);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece','');
  await worldClick(page,'diagram-view',hex?1:topology==='torus'?.5:1.5,hex?Math.sqrt(3):2.5,true,hex);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece','face-00');
  await page.screenshot({path:`test-results/flat-${name}-selected.png`,fullPage:true});
  const host=page.getByTestId('cube-view');
  await host.getByText(hex?'5 twists':'3 twists',{exact:true}).click();await host.getByRole('button',{name:'Twist A',exact:true}).click();await resting(page);
  const reference=JSON.parse(execFileSync('build/native/twisty',['run','--definition',`packages/flat-${name}/package.json`,'--algorithm','A','--json'],{encoding:'utf8'}));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await page.getByRole('button',{name:'↶ Undo'}).click();await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.keyboard.press('Escape');
  await page.getByLabel('Animation speed').selectOption('450');
  await page.getByLabel('Algorithm',{exact:true}).fill("A B C D (A B C D)'");
  await page.getByRole('button',{name:'Play algorithm →'}).click();
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating','true');
  await page.waitForTimeout(170);await page.screenshot({path:`test-results/flat-${name}-motion.png`,fullPage:true});
  await resting(page);await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByLabel('Puzzle',{exact:true}).selectOption('cube3');await resting(page);
  await expectViews(page, 'cube-sphere');
  await expect(page.getByRole('alert')).toHaveCount(0);expect(errors).toEqual([]);
});

for(const name of ['torus','klein-bottle','hex-torus','hex-klein-bottle'])test(`${name}: pans the camera, picks recycled destinations, and restores saved views`,async({page})=>{
  test.setTimeout(90_000);
  const hex=name.startsWith('hex-');
  const errors:string[]=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto('./');await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/,{timeout:15000});
  await page.getByLabel('Puzzle',{exact:true}).selectOption('flat-'+name);await resting(page);
  const initial=await page.getByTestId('state-digest').textContent();
  await page.getByLabel('Animation speed').selectOption('0');
  const window=page.getByTestId('cube-view').locator('.surface-viewport polygon'),windowPoints=await window.getAttribute('points');
  const canvas=page.getByTestId('cube-view').locator('canvas'),bounds=(await canvas.boundingBox())!;
  await page.mouse.move(bounds.x+bounds.width*.5,bounds.y+bounds.height*.5);await page.mouse.down();
  await page.mouse.move(bounds.x+bounds.width*.5+35,bounds.y+bounds.height*.5+20,{steps:5});await page.mouse.up();
  await expect(window).toHaveAttribute('points',windowPoints!);
  // This fixed screen-space margin remains unpickable after the camera moves.
  await worldClick(page,'cube-view',hex?.1:-.08,hex?1.5:.5,false,hex);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece','');
  // Wheel zoom is disabled for the fixed fundamental-domain window.
  await canvas.hover();await page.mouse.wheel(0,-400);
  await expect(window).toHaveAttribute('points',windowPoints!);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  const downloading=page.waitForEvent('download');await page.getByRole('button',{name:'Save session ↗'}).click();
  const saved=await(await downloading).path();
  const presentation=JSON.parse(readFileSync(saved!,'utf8')).presentation;
  expect(presentation.visibleViews).toEqual(['fundamental-domain', 'tiled-plane']);
  expect(presentation.cameras[0].position[0]).toBeLessThan(hex?1.5:1);
  expect(presentation.cameras[0].position[1]).toBeGreaterThan(hex?Math.sqrt(3)/2:1);
  expect(presentation.cameras[0].zoom).toBe(1);
  const far=JSON.parse(readFileSync(saved!,'utf8')),cx=hex?1.5:1,cy=hex?Math.sqrt(3)/2:1;
  far.presentation.cameras[0]={position:[cx+76,cy,15],target:[cx+76,cy,0],zoom:.7};
  await page.getByLabel('Open session file').setInputFiles({name:'far-camera.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(far))});
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-camera-x',String(cx+76));
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-camera-zoom','1');
  await expect(window).toHaveAttribute('points',windowPoints!);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-image-count','25');
  // An even deck translation preserves the same copy, far outside the original
  // image pool. Picking must use the relocated images and camera-space mask.
  await worldClick(page,'cube-view',hex?.12:.5,hex?.05:.5,false,hex);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece','face-00');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-destination-count',hex?'5':'3');
  if(name==='hex-klein-bottle'){
    const r=(await canvas.boundingBox())!,halfWidth=Math.max(3*.57,Math.sqrt(3)*.57*r.width/r.height);
    const dragPixels=2*r.width/(2*halfWidth);
    for(let i=0;i<4;i++){
      await page.mouse.move(r.x+r.width*.35,r.y+r.height*.5);await page.mouse.down();
      await page.mouse.move(r.x+r.width*.35+dragPixels,r.y+r.height*.5,{steps:5});await page.mouse.up();
    }
    await expect.poll(async()=>Number(await page.getByTestId('cube-view').getAttribute('data-camera-x'))).toBeCloseTo(cx+68,1);
    await expect(window).toHaveAttribute('points',windowPoints!);
    // Destination hit geometry must follow the recycled copies too.
    await worldClick(page,'cube-view',.12,.05,false,hex);
    const chooser=page.getByTestId('cube-view').getByRole('group',{name:'Choose a twist for this destination'});
    await expect(chooser).toBeVisible();
    await chooser.getByRole('button',{name:'Twist A',exact:true}).click();await resting(page);
    await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
    await page.getByRole('button',{name:'↶ Undo'}).click();await resting(page);
    await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  }
  await page.screenshot({path:`test-results/flat-${name}-viewport.png`,fullPage:true});
  await page.keyboard.press('Escape');
  await page.getByLabel('Open session file').setInputFiles(saved!);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-camera-x',String(presentation.cameras[0].position[0]));
  await page.keyboard.press('a');await resting(page);await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
  await page.getByLabel('Open session file').setInputFiles(saved!);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);await resting(page);
  await page.getByLabel('Puzzle',{exact:true}).selectOption('cube3');await resting(page);
  await expectViews(page, 'cube-sphere');
  await expect(page.getByRole('alert')).toHaveCount(0);expect(errors).toEqual([]);
});

test('independent Canvas renderer displays square and rhombus quotients',async({page})=>{
  await page.goto('kernel/examples/canvas/');await expect(page.getByRole('status')).toHaveText('Ready');
  for(const topology of ['torus','klein-bottle','hex-torus','hex-klein-bottle']){
    await page.getByLabel('Puzzle',{exact:true}).selectOption('flat-'+topology);
    await expect(page.getByRole('status')).toHaveText('Ready');const origin=await page.locator('#digest').textContent();
    await page.getByRole('button',{name:'A',exact:true}).click();await expect(page.locator('#digest')).not.toHaveText(origin!);
    await page.getByRole('button',{name:'Undo',exact:true}).click();await expect(page.locator('#digest')).toHaveText(origin!);
    await page.screenshot({path:`test-results/flat-${topology}-canvas.png`,fullPage:true});
  }
});
