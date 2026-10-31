import {expect,test} from '@playwright/test';
import {readFileSync} from 'node:fs';
import {createKernel,KernelSession} from '../../kernel/index';

test('presets load only the runtime module and custom Gear loads authoring on demand',async({page})=>{
  const wasm:string[]=[];page.on('request',request=>{if(new URL(request.url()).pathname.endsWith('.wasm'))wasm.push(request.url());});
  await page.goto('./');await expect(page.getByTestId('state-digest')).toHaveText(/^[0-9a-f]{64}$/,{timeout:15000});
  await page.getByLabel('Puzzle',{exact:true}).selectOption('helicopter');
  await expect(page.getByLabel('Twist family',{exact:true})).toBeVisible();
  expect(wasm).toHaveLength(1);expect(wasm[0]).toContain('twisty-artifact');
  await page.getByLabel('Puzzle',{exact:true}).selectOption('gear');
  await expect(page.getByLabel('Gear edge UF')).toBeChecked();
  expect(wasm).toHaveLength(1);
  await page.getByLabel('Gear edge UF').uncheck();
  await expect(page.getByLabel('Gear edge UF')).not.toBeChecked({timeout:15000});
  await expect(page.getByRole('alert')).toHaveCount(0);expect(wasm).toHaveLength(2);
  expect(wasm.filter(url=>url.includes('twisty-artifact'))).toHaveLength(1);
});

test('schema-2 source saves convert with cursor, redo and view metadata intact',async({page})=>{
  const module=await createKernel(),session=new KernelSession(module,readFileSync('packages/cube3/package.json','utf8'));
  let saved:string;
  try{session.run('R U','transactional');session.undo();saved=session.save({viewOrder:['diagram','cube','sphere'],visibleViews:['diagram','cube']});}
  finally{session.dispose();}
  await page.goto('./');await expect(page.getByTestId('state-digest')).toHaveText(/^[0-9a-f]{64}$/,{timeout:15000});
  const initial=await page.getByTestId('state-digest').textContent();
  await page.getByLabel('Open session file').setInputFiles({name:'legacy.json',mimeType:'application/json',buffer:Buffer.from(saved)});
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!,{timeout:15000});
  await expect(page.getByRole('button',{name:'↷ Redo',exact:true})).toBeEnabled();
  await expect(page.getByRole('alert')).toHaveCount(0);
  const download=page.waitForEvent('download');await page.getByRole('button',{name:'Save session ↗'}).click();
  const result=JSON.parse(readFileSync((await(await download).path())!,'utf8'));
  expect(result.schemaVersion).toBe(3);expect(result.logical.cursor).toBe(1);expect(result.logical.requests).toHaveLength(2);
  expect(result.presentation.viewOrder).toEqual(['diagram','cube','sphere']);expect(result.presentation.visibleViews).toEqual(['diagram','cube']);
  const digest=await page.getByTestId('state-digest').textContent();
  await page.getByRole('button',{name:'↷ Redo',exact:true}).click();await expect(page.getByTestId('state-digest')).not.toHaveText(digest!);
});

test('standalone runtime Canvas uses only its published compiler-free package',async({page,baseURL})=>{
  const requests:string[]=[],errors:string[]=[];page.on('request',request=>requests.push(request.url()));page.on('pageerror',error=>errors.push(error.message));
  await page.goto('runtime/examples/canvas/');await expect(page.getByRole('status')).toHaveText('Ready',{timeout:15000});
  const initial=await page.locator('#digest').textContent();await page.getByRole('button',{name:'R',exact:true}).click();
  await expect(page.locator('#digest')).not.toHaveText(initial!);await page.getByRole('button',{name:'Undo',exact:true}).click();await expect(page.locator('#digest')).toHaveText(initial!);
  for(const [puzzle,move] of [['helicopter','UF_ab'],['tesseract3','Xp_fY']]){
    await page.getByLabel('Puzzle',{exact:true}).selectOption(puzzle);await expect(page.getByRole('button',{name:move,exact:true})).toBeVisible({timeout:30000});
    const before=await page.locator('#digest').textContent();await page.getByRole('button',{name:move,exact:true}).click();await expect(page.locator('#digest')).not.toHaveText(before!);
    if(puzzle==='helicopter'){
      await page.getByLabel('Family',{exact:true}).selectOption('UF');
      await page.locator('#destinations').getByRole('button',{name:'d',exact:true}).click();
      await expect(page.getByRole('status')).toHaveText('Ready');
    }
  }
  const prefix=new URL('runtime/',baseURL!).pathname;
  const modules=requests.filter(url=>/\.(?:js|mjs|wasm)(?:\?|$)/.test(url));
  expect(modules.every(url=>new URL(url).pathname.startsWith(prefix))).toBe(true);
  expect(modules.some(url=>url.endsWith('/runtime/runtime.js'))).toBe(true);
  expect(modules.filter(url=>url.endsWith('.wasm'))).toHaveLength(1);expect(errors).toEqual([]);
});
