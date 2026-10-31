import {runtimeReference} from './runtime-reference';
import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';

async function resting(page:Page):Promise<void>{
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating','false',{timeout:15000});
  const digest=await page.getByTestId('state-digest').textContent();
  for(const id of ['cube-view','diagram-view'])await expect(page.getByTestId(id)).toHaveAttribute('data-state-digest',digest!);
  await expect(page.getByRole('alert')).toHaveCount(0);
}

async function worldClick(page:Page,id:string,x:number,y:number):Promise<void>{
  const host=page.getByTestId(id);await host.scrollIntoViewIfNeeded();const r=(await host.boundingBox())!;
  const tiled=id==='diagram-view',factor=tiled?3:1;
  const w=(tiled?Math.min:Math.max)(2*.57*factor,2*.57*factor*r.width/r.height),h=w*r.height/r.width;
  const cx=Number(await host.getAttribute('data-camera-x')),cy=Number(await host.getAttribute('data-camera-y'));
  const zoom=Number(await host.getAttribute('data-camera-zoom'));
  await host.locator('canvas').click({position:{x:(1+(x-cx)*zoom/w)*r.width/2,y:(1-(y-cy)*zoom/h)*r.height/2}});
}

for(const six of [false,true])test(`pillowcase ${six?6:4}: shared copies, cone turns and ordinary turns`,async({page})=>{
  test.setTimeout(180_000);
  const puzzle=`flat-pillowcase${six?'-6':''}`,errors:string[]=[];
  const bundle=JSON.parse(readFileSync(`packages/${puzzle}/package.json`,'utf8'));
  const pieceWithColor=(color:string)=>Object.entries(bundle.catalog.pieces).find(([,p]:[string,any])=>
    Object.values(p.portLabels).length===1&&Object.values(p.portLabels)[0]===color)![0];
  page.on('pageerror',error=>errors.push(error.message));
  await page.goto('./');await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/,{timeout:15000});
  await page.getByLabel('Puzzle',{exact:true}).selectOption(puzzle);await resting(page);
  await page.screenshot({path:`test-results/${puzzle}-solved.png`,fullPage:true});
  await page.getByLabel('Animation speed').selectOption('0');
  for(const id of ['cube-view','diagram-view'])await expect(page.getByTestId(id)).toHaveAttribute('data-topology','pillowcase');
  const origin=await page.getByTestId('state-digest').textContent();
  const cone=pieceWithColor('A');
  await worldClick(page,'cube-view',.51,.51);
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-selected-piece',cone);
  await page.keyboard.press('Escape');
  await worldClick(page,'diagram-view',2.51,.51);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece',cone);
  await page.keyboard.press('Escape');
  await worldClick(page,'cube-view',.1,.1);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece','');
  await page.getByRole('button',{name:'Move A',exact:true}).click();await resting(page);
  await expect(page.getByTestId('state-digest')).not.toHaveText(origin!);
  await page.getByRole('button',{name:'Move A',exact:true}).click();await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(origin!);
  if(six){
    const vertex=pieceWithColor('F');
    await worldClick(page,'diagram-view',1,.02);
    await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece',vertex);
    await page.keyboard.press('Escape');
    await worldClick(page,'diagram-view',.02,1);
    await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece',vertex);
    await page.keyboard.press('Escape');
    await worldClick(page,'cube-view',1,1);
    await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece',pieceWithColor('E'));
    await page.keyboard.press('Escape');
    await page.keyboard.press('e');await resting(page);
    await expect(page.getByTestId('state-digest')).not.toHaveText(origin!);
    await page.keyboard.press('Shift+E');await resting(page);
    await expect(page.getByTestId('state-digest')).toHaveText(origin!);
    await page.getByLabel('Algorithm',{exact:true}).fill('(E)2');
    await page.getByRole('button',{name:'Play algorithm →'}).click();await resting(page);
    await expect(page.getByTestId('state-digest')).not.toHaveText(origin!);
    await page.getByRole('button',{name:'Play algorithm →'}).click();await resting(page);
    await expect(page.getByTestId('state-digest')).toHaveText(origin!);
  }
  const tiled=page.getByTestId('diagram-view'),canvas=tiled.locator('canvas');
  const r=(await canvas.boundingBox())!;
  for(let i=0;i<3;i++){
    await page.mouse.move(r.x+r.width*.7,r.y+r.height*.5);await page.mouse.down();
    await page.mouse.move(r.x+r.width*.2,r.y+r.height*.5,{steps:5});await page.mouse.up();
  }
  const cx=Number(await tiled.getAttribute('data-camera-x')),cy=Number(await tiled.getAttribute('data-camera-y'));
  await worldClick(page,'diagram-view',1+2*Math.round((cx-1)/2),1+2*Math.round((cy-1)/2));
  await expect(tiled).not.toHaveAttribute('data-selected-piece','');
  await page.keyboard.press('Escape');
  // A full identity path still animates the cone-centered piece through a
  // stabilizer. Its final canonical half mesh must remain visually continuous.
  await page.getByLabel('Animation speed').selectOption('450');
  const word=six?"A2 A E F B C E2 F' D":"A2 A B C D A' C";
  await page.getByLabel('Algorithm',{exact:true}).fill(word);
  await page.getByRole('button',{name:'Play algorithm →'}).click();
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating','true');
  await page.waitForTimeout(150);
  await page.screenshot({path:`test-results/${puzzle}-motion.png`,fullPage:true});
  await resting(page);
  const reference=runtimeReference(['run','--definition',`packages/${puzzle}/package.json`,'--algorithm',word,'--json']);
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await page.getByLabel('Animation speed').selectOption('0');
  await page.getByRole('button',{name:'↶ Undo',exact:true}).click();await resting(page);
  await page.getByRole('button',{name:'↷ Redo',exact:true}).click();await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await page.screenshot({path:`test-results/${puzzle}-resting.png`,fullPage:true});
  expect(errors).toEqual([]);
});
