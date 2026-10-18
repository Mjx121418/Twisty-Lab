import { expect, test, type Page } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

async function resting(page:Page):Promise<void>{
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating','false');
  const digest=await page.getByTestId('state-digest').textContent();
  for(const id of ['cube-view','diagram-view'])await expect(page.getByTestId(id)).toHaveAttribute('data-state-digest',digest!);
}

async function worldClick(page:Page,id:string,x:number,y:number,tiled:boolean):Promise<void>{
  const host=page.getByTestId(id);await host.scrollIntoViewIfNeeded();const r=(await host.boundingBox())!;
  const base=tiled?3.42:1.14,w=Math.max(base,base*r.width/r.height),h=w*r.height/r.width;
  await host.locator('canvas').click({position:{x:(1+(x-1)/w)*r.width/2,y:(1-(y-1)/h)*r.height/2}});
}

for(const topology of ['torus','klein-bottle'])test(`${topology}: picks copies, plays moves, pans, and restores a saved session`,async({page})=>{
  const errors:string[]=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto('./');await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/,{timeout:15000});
  await page.getByLabel('Puzzle',{exact:true}).selectOption('flat-'+topology);await resting(page);
  await expect(page.getByLabel('Algorithm',{exact:true})).toHaveValue("A B A' B'");
  await expect(page.locator('canvas')).toHaveCount(2);
  await expect(page.getByRole('button',{name:'Square',exact:true})).toBeVisible();
  for(const [id,mode,images] of [['cube-view','square','9'],['diagram-view','tiles','25']]){
    const host=page.getByTestId(id);await expect(host).toHaveAttribute('data-topology',topology);
    await expect(host).toHaveAttribute('data-surface-mode',mode);await expect(host).toHaveAttribute('data-image-count',images);
  }
  const initial=await page.getByTestId('state-digest').textContent();
  await worldClick(page,'diagram-view',topology==='torus'?.5:1.5,-1.5,true);
  for(const id of ['cube-view','diagram-view']){
    await expect(page.getByTestId(id)).toHaveAttribute('data-selected-piece','face-00');
    await expect(page.getByTestId(id)).toHaveAttribute('data-destination-operations',"A,A',A2");
  }
  // There is geometry in the covering plane behind this margin, but the
  // displayed fundamental square clips it and must not allow it to be picked.
  await worldClick(page,'cube-view',-.08,.5,false);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece','');
  await worldClick(page,'diagram-view',topology==='torus'?.5:1.5,-1.5,true);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece','face-00');
  await page.screenshot({path:`test-results/flat-${topology}-selected.png`,fullPage:true});
  const host=page.getByTestId('cube-view');
  await host.getByText('3 twists',{exact:true}).click();await host.getByRole('button',{name:'Twist A',exact:true}).click();await resting(page);
  const reference=JSON.parse(execFileSync('build/native/twisty',['run','--definition',`packages/flat-${topology}/relational-package.json`,'--algorithm','A','--json'],{encoding:'utf8'}));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await page.getByRole('button',{name:'↶ Undo'}).click();await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.keyboard.press('Escape');
  await page.getByLabel('Animation speed').selectOption('450');
  await page.getByLabel('Algorithm',{exact:true}).fill("A B C D (A B C D)'");
  await page.getByRole('button',{name:'Play algorithm →'}).click();
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating','true');
  await page.waitForTimeout(170);await page.screenshot({path:`test-results/flat-${topology}-motion.png`,fullPage:true});
  await resting(page);await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  const canvas=page.getByTestId('cube-view').locator('canvas'),bounds=(await canvas.boundingBox())!;
  await page.mouse.move(bounds.x+bounds.width*.5,bounds.y+bounds.height*.5);await page.mouse.down();
  await page.mouse.move(bounds.x+bounds.width*.5+35,bounds.y+bounds.height*.5+20,{steps:5});await page.mouse.up();
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  const downloading=page.waitForEvent('download');await page.getByRole('button',{name:'Save session ↗'}).click();
  const saved=await(await downloading).path();
  const presentation=JSON.parse(readFileSync(saved!,'utf8')).presentation;
  expect(presentation.viewPair).toBe('square-tiles');
  expect(presentation.cameras[0].position[0]).toBeLessThan(1);
  expect(presentation.cameras[0].position[1]).toBeGreaterThan(1);
  await page.keyboard.press('a');await resting(page);await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
  await page.getByLabel('Open session file').setInputFiles(saved!);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);await resting(page);
  await page.getByLabel('Puzzle',{exact:true}).selectOption('cube3');await resting(page);
  await expect(page.getByLabel('Geometry views')).toHaveValue('cube-sphere');
  await expect(page.getByRole('alert')).toHaveCount(0);expect(errors).toEqual([]);
});

test('independent Canvas renderer displays the two square quotients',async({page})=>{
  await page.goto('kernel/examples/canvas/');await expect(page.getByRole('status')).toHaveText('Ready');
  for(const topology of ['torus','klein-bottle']){
    await page.getByLabel('Puzzle',{exact:true}).selectOption('flat-'+topology);
    await expect(page.getByRole('status')).toHaveText('Ready');const origin=await page.locator('#digest').textContent();
    await page.getByRole('button',{name:'A',exact:true}).click();await expect(page.locator('#digest')).not.toHaveText(origin!);
    await page.getByRole('button',{name:'Undo',exact:true}).click();await expect(page.locator('#digest')).toHaveText(origin!);
    await page.screenshot({path:`test-results/flat-${topology}-canvas.png`,fullPage:true});
  }
});
