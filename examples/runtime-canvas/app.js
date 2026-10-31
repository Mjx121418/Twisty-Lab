import {createRuntime,RuntimeSession,RuntimeController,GeometryInstance} from '../../runtime.js';
const module=await createRuntime();
const canvas=document.querySelector('#view'),context=canvas.getContext('2d'),picker=document.querySelector('#puzzle');
const read=async path=>{const response=await fetch('../../data/'+path);if(!response.ok)throw Error(`Missing runtime data: ${path}`);return response.text();};
let session,controller,geometry;
const index=JSON.parse(await read('index.json'));
for(const item of index.puzzles){const option=document.createElement('option');option.value=option.textContent=item.id;picker.append(option);}
function familyDestinations(){
  const family=document.querySelector('#family').value,destinations=document.querySelector('#destinations');destinations.replaceChildren();
  if(!family)return;
  for(const destination of controller.destinations(family,0,32).destinations??[]){
    const button=document.createElement('button');button.textContent=Object.values(destination.targets??{}).join(' / ');
    button.disabled=destination.status!=='Planned';
    button.onclick=()=>accept(controller.turnTo({family,targets:destination.targets,pathId:destination.pathId}));destinations.append(button);
  }
}
function draw(){
  geometry.setState(session.stateText());const descriptor=geometry.scene;
  const positions=geometry.scene.dynamicMeshes?geometry.positions:new Float32Array(geometry.definition.positions());
  const indices=geometry.scene.dynamicMeshes?geometry.indices:new Uint32Array(geometry.definition.indices());
  const transforms=geometry.matrices,scale=34;
  context.clearRect(0,0,canvas.width,canvas.height);
  const meshes=new Map(descriptor.meshAssets.map(mesh=>[mesh.id,mesh]));
  descriptor.visualParts.forEach((part,partIndex)=>{
    const mesh=meshes.get(part.meshAssetId),offset=partIndex*16;
    context.fillStyle=({U:'#fff4cc',R:'#d86d53',F:'#4b9e83',D:'#e7c349',L:'#dd9c4f',B:'#5e85b8'})[part.materialBindingId]??'#8595a0';
    for(let i=0;i<mesh.indexCount;i+=3){context.beginPath();for(let k=0;k<3;++k){
      const vertex=mesh.positionOffset+indices[mesh.indexOffset+i+k]*3,x=positions[vertex],y=positions[vertex+1],z=positions[vertex+2];
      const px=canvas.width/2+scale*(transforms[offset]*x+transforms[offset+4]*y+transforms[offset+8]*z+transforms[offset+12]);
      const py=canvas.height/2-scale*(transforms[offset+1]*x+transforms[offset+5]*y+transforms[offset+9]*z+transforms[offset+13]);
      if(k===0)context.moveTo(px,py);else context.lineTo(px,py);
    }context.closePath();context.fill();}
  });
  const snapshot=session.snapshot();document.querySelector('#digest').textContent=snapshot.stateDigest;
  document.querySelector('#undo').disabled=!snapshot.canUndo;document.querySelector('#redo').disabled=!snapshot.canRedo;familyDestinations();
}
function accept(result){document.querySelector('#status').textContent=['Committed','Loaded'].includes(result.status)?'Ready':result.reasonCode??result.status;draw();}
async function select(){
  picker.disabled=true;
  document.querySelector('#status').textContent='Loading…';
  document.querySelector('#moves').replaceChildren();document.querySelector('#family').replaceChildren();
  document.querySelector('#families').hidden=true;
  for(const id of ['undo','redo','notation'])document.querySelector('#'+id).disabled=true;
  document.querySelector('#algorithm button').disabled=true;
  try{
    geometry?.dispose();geometry=undefined;controller?.dispose();controller=undefined;session?.dispose();session=undefined;
    const id=picker.value,item=index.puzzles.find(p=>p.id===id),view=item.views.find(v=>v.includes('port-diagram'))??item.views[0];
    const [abstract,controls,presentation,interpretation,envelope,payload]=await Promise.all([
      'abstract.json','controls.json','presentation.json','interpretation.json',`realizations/${view}/realization.json`,`realizations/${view}/geometry.json`
    ].map(path=>read(id+'/'+path)));
    session=new RuntimeSession(module,{abstract,controls,presentation});controller=new RuntimeController(module,session);
    geometry=new GeometryInstance(module,session.native,{interpretation,envelope,payload});
    const familyPicker=document.querySelector('#family');familyPicker.replaceChildren();
    for(const family of controller.catalog().families){const option=document.createElement('option');option.value=family.id;option.textContent=family.label??family.id;familyPicker.append(option);}
    document.querySelector('#families').hidden=!familyPicker.options.length;familyPicker.onchange=familyDestinations;
    const buttons=document.querySelector('#moves');buttons.replaceChildren();
    for(const operation of session.definition.operations){const button=document.createElement('button');button.textContent=operation.id;button.onclick=()=>accept(session.move(operation.id));buttons.append(button);}
    document.querySelector('#status').textContent='Ready';draw();
  }catch(error){
    geometry?.dispose();geometry=undefined;controller?.dispose();controller=undefined;session?.dispose();session=undefined;
    document.querySelector('#status').textContent=String(error);
  }finally{
    picker.disabled=false;document.querySelector('#notation').disabled=!session;
    document.querySelector('#algorithm button').disabled=!session;
  }
}
picker.onchange=select;picker.value='cube3';await select();
document.querySelector('#undo').onclick=()=>accept(session.undo());document.querySelector('#redo').onclick=()=>accept(session.redo());
document.querySelector('#algorithm').onsubmit=event=>{event.preventDefault();if(!session)return;accept(session.run(document.querySelector('#notation').value,'interactive'));};
