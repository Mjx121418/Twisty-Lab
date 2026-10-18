import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { copyFloatView, copyIndexView, type FlatSurface, type MainModule, type Realization, type SceneDescriptor } from '../../kernel/index';
import type { PieceDestination } from './destinations';
import { insideBounds, surfaceBounds, surfaceImages, type Bounds, type SurfaceImage, type SurfaceMode } from './flatSurface';

const palette: Record<string, number> = { A:0xe76561, B:0x66c7a0, C:0xf1cf67, D:0x7fa5ed, body:0x24313b, ink:0x16252d };
type SurfaceMesh = THREE.InstancedMesh<THREE.BufferGeometry, THREE.MeshBasicMaterial>;
type Instance = { part: number; image: number };
type Batch = { mesh: SurfaceMesh; instances: Instance[] };
type DestinationGroup = { options: PieceDestination[] };
type GhostBatch = { mesh: SurfaceMesh; groups: DestinationGroup[] };

/** A chart renderer: the kernel has no knowledge of these displayed copies. */
export class SurfaceView {
  readonly geometry: InstanceType<MainModule['Geometry']>;
  private readonly descriptor: SceneDescriptor;
  private readonly surface: FlatSurface;
  private readonly bounds: Bounds;
  private readonly images: SurfaceImage[];
  private readonly scene = new THREE.Scene();
  private readonly renderer: THREE.WebGLRenderer;
  private readonly camera = new THREE.OrthographicCamera(-2,2,2,-2,.1,100);
  private readonly controls: OrbitControls;
  private readonly assets = new Map<string,THREE.BufferGeometry>();
  private readonly batches: Batch[] = [];
  private readonly ghosts: GhostBatch[] = [];
  private readonly outlines: THREE.LineSegments[] = [];
  private readonly edgeAssets = new Map<string,THREE.EdgesGeometry>();
  private readonly outlineMaterial = new THREE.LineBasicMaterial({color:0xffc44c,depthTest:false,depthWrite:false});
  private readonly decorations: THREE.Object3D[] = [];
  private readonly textures: THREE.Texture[] = [];
  private readonly frameData: Float32Array;
  private readonly resizeObserver: ResizeObserver;
  private readonly raycaster = new THREE.Raycaster();
  private readonly matrix = new THREE.Matrix4();
  private readonly local = new THREE.Matrix4();
  private readonly color = new THREE.Color();
  private preview?: InstanceType<MainModule['Geometry']>;
  private groups: DestinationGroup[] = [];
  private readonly root = document.createElement('div');
  private readonly destinationControls = document.createElement('div');
  private readonly chooser = document.createElement('div');
  private readonly hint = document.createElement('div');
  private pointer?: {x:number;y:number;id:number;dragged:boolean};
  private frame = 0;
  private frameId = 0;
  private disposed = false;

  constructor(private readonly module: MainModule, private readonly host: HTMLElement,
    private readonly session: InstanceType<MainModule['Session']>, private readonly realization: Realization,
    private readonly onPick: (piece:string|undefined)=>void, private readonly canPick: ()=>boolean,
    private readonly onDestination: (destination:PieceDestination)=>void, mode: SurfaceMode) {
    const geometry = module.createGeometry(session,JSON.stringify(realization));
    if (!geometry) throw new Error('Cannot create flat-surface geometry.');
    this.geometry = geometry;
    this.descriptor = JSON.parse(geometry.sceneJSON()) as SceneDescriptor;
    if (!this.descriptor.flatSurface) { geometry.delete(); throw new Error('Missing quotient surface metadata.'); }
    if (this.descriptor.requiredCapabilities.some(c=>!['triangle-meshes','rigid-transforms','quotient-surface'].includes(c))) {
      geometry.delete(); throw new Error('Unsupported surface-rendering capability.');
    }
    this.surface = this.descriptor.flatSurface;
    this.bounds = surfaceBounds(this.surface,mode);
    this.images = surfaceImages(this.surface,mode);
    this.frameData = copyFloatView(geometry.transforms() as Float32Array);
    try { this.renderer = new THREE.WebGLRenderer({antialias:true,alpha:true}); }
    catch (error) { geometry.delete(); throw error; }
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio,1.5));
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    const [left,bottom,right,top] = this.bounds;
    this.renderer.clippingPlanes = [new THREE.Plane(new THREE.Vector3(1,0,0),-left),
      new THREE.Plane(new THREE.Vector3(-1,0,0),right),new THREE.Plane(new THREE.Vector3(0,1,0),-bottom),
      new THREE.Plane(new THREE.Vector3(0,-1,0),top)];
    this.renderer.domElement.setAttribute('aria-label',mode === 'square' ? 'Fundamental square puzzle' : 'Tiled plane puzzle');
    this.host.dataset.realizationId = realization.id;
    this.host.dataset.surfaceMode = mode;
    this.host.dataset.topology = this.surface.topology;
    this.host.dataset.imageCount = String(this.images.length);
    this.host.append(this.renderer.domElement);
    const cx=(left+right)/2, cy=(bottom+top)/2;
    this.camera.position.set(cx,cy,15);
    this.controls = new OrbitControls(this.camera,this.renderer.domElement);
    this.controls.target.set(cx,cy,0);
    this.controls.enableRotate = false;
    this.controls.enableDamping = false;
    this.controls.screenSpacePanning = true;
    this.controls.mouseButtons.LEFT = THREE.MOUSE.PAN;
    this.controls.touches.ONE = THREE.TOUCH.PAN;
    this.controls.touches.TWO = THREE.TOUCH.DOLLY_PAN;
    this.controls.minZoom = .7;
    this.controls.maxZoom = 5;
    this.controls.update();
    const positions=copyFloatView(geometry.positions() as Float32Array), indices=copyIndexView(geometry.indices() as Uint32Array);
    for (const asset of this.descriptor.meshAssets) {
      const g=new THREE.BufferGeometry();
      g.setAttribute('position',new THREE.BufferAttribute(positions.slice(asset.positionOffset,asset.positionOffset+asset.vertexCount*3),3));
      g.setIndex(new THREE.BufferAttribute(indices.slice(asset.indexOffset,asset.indexOffset+asset.indexCount),1));
      g.computeBoundingBox(); g.computeBoundingSphere(); this.assets.set(asset.id,g);
    }
    const byAsset = new Map<string,Instance[]>();
    this.descriptor.visualParts.forEach((part,index)=>{
      const list=byAsset.get(part.meshAssetId) ?? [];
      this.images.forEach((_,image)=>list.push({part:index,image}));
      byAsset.set(part.meshAssetId,list);
    });
    for (const [asset,instances] of byAsset) {
      const part=this.descriptor.visualParts[instances[0].part];
      const mesh=new THREE.InstancedMesh(this.assets.get(asset)!,new THREE.MeshBasicMaterial({side:THREE.DoubleSide,depthTest:false,depthWrite:false}),instances.length);
      mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
      mesh.renderOrder=part.materialBindingId==='ink' ? 2 : part.role==='port' ? 1 : 0;
      mesh.frustumCulled=false;
      mesh.boundingSphere=new THREE.Sphere(new THREE.Vector3(cx,cy,0),(right-left)*8);
      this.batches.push({mesh,instances}); this.scene.add(mesh);
    }
    this.root.className='destination-overlay'; this.destinationControls.className='destination-controls';
    this.chooser.className='destination-chooser'; this.hint.className='destination-hint';
    this.chooser.setAttribute('role','group'); this.chooser.setAttribute('aria-label','Choose a twist for this destination');
    this.root.append(this.destinationControls,this.hint,this.chooser); host.append(this.root);
    this.clearDestinations(); this.decorate(mode); this.updateTransforms(); this.highlight(undefined,[]);
    this.resizeObserver=new ResizeObserver(()=>this.resize()); this.resizeObserver.observe(host);
    const canvas=this.renderer.domElement;
    canvas.addEventListener('pointerdown',this.pointerDown); canvas.addEventListener('pointermove',this.pointerMove);
    canvas.addEventListener('pointerup',this.pointerUp); canvas.addEventListener('pointercancel',this.pointerCancel);
    canvas.addEventListener('pointerleave',this.pointerLeave); canvas.addEventListener('webglcontextlost',this.contextLost);
    canvas.addEventListener('webglcontextrestored',this.contextRestored);
    this.resize(); this.draw();
  }

  private decorate(mode: SurfaceMode): void {
    const side=this.surface.fundamentalBounds[2], low=mode==='square'?0:-1, high=mode==='square'?1:2;
    const lines=(points:number[],color:number)=>{
      const g=new THREE.BufferGeometry(); g.setAttribute('position',new THREE.Float32BufferAttribute(points,3));
      const line=new THREE.LineSegments(g,new THREE.LineBasicMaterial({color,transparent:true,opacity:.7,depthTest:false}));
      line.renderOrder=4; this.decorations.push(line); this.scene.add(line);
    };
    for (let i=low;i<=high;i++) {
      lines([i*side,low*side,0,i*side,high*side,0],0x758ca5);
      lines([low*side,i*side,0,high*side,i*side,0],this.surface.topology==='klein-bottle'?0xbc729b:0x758ca5);
    }
    // Matched seam arrows. Their direction, including the twisted pair, is
    // attached to the chart rather than to a moving puzzle piece.
    for (let y=low;y<high;y++) for (let x=low;x<high;x++) {
      const vertical=(sx:number)=>lines([sx,y*side+.85,0,sx,y*side+1.15,0,sx,y*side+1.15,0,sx-.045,y*side+1.07,0,sx,y*side+1.15,0,sx+.045,y*side+1.07,0],0x416e99);
      const horizontal=(sy:number,sign:number)=>{
        const cx=x*side+1;
        lines([cx-sign*.15,sy,0,cx+sign*.15,sy,0,cx+sign*.15,sy,0,cx+sign*.07,sy-.045,0,cx+sign*.15,sy,0,cx+sign*.07,sy+.045,0],0xad4c83);
      };
      const seamDirection=(row:number)=>this.surface.topology==='klein-bottle'&&row%2!==0?-1:1;
      vertical(x*side+.012); horizontal(y*side+.012,seamDirection(y));
      if (x===high-1) vertical((x+1)*side-.012);
      if (y===high-1) horizontal((y+1)*side-.012,seamDirection(y+1));
    }
    for (const [name,center] of Object.entries(this.surface.diskCenters ?? {})) {
      const canvas=document.createElement('canvas'); canvas.width=128;canvas.height=128;
      const context=canvas.getContext('2d')!;context.fillStyle='#23343b';context.font='600 94px monospace';
      context.textAlign='center';context.textBaseline='middle';context.fillText(name,64,66);
      const texture=new THREE.CanvasTexture(canvas);this.textures.push(texture);
      for (const image of this.images) {
        const p=new THREE.Vector3(center[0],center[1],0).applyMatrix4(image.matrix);
        if (!insideBounds(this.bounds,p.x,p.y)) continue;
        const sprite=new THREE.Sprite(new THREE.SpriteMaterial({map:texture,depthTest:false,depthWrite:false}));
        sprite.position.copy(p).add(new THREE.Vector3(0,.125,0));sprite.scale.set(.14,.14,1);sprite.renderOrder=5;
        this.decorations.push(sprite);this.scene.add(sprite);
      }
    }
  }

  private updateTransforms(): void {
    this.frameData.set(this.geometry.transforms() as Float32Array);
    for (const batch of this.batches) {
      batch.instances.forEach(({part,image},i)=>{
        this.local.fromArray(this.frameData,part*16);
        this.matrix.multiplyMatrices(this.images[image].matrix,this.local);
        batch.mesh.setMatrixAt(i,this.matrix);
      });
      batch.mesh.instanceMatrix.needsUpdate=true;
    }
    this.updateOutlines();
    this.host.dataset.frameId=String(++this.frameId);
  }

  private resize(): void {
    const {width,height}=this.host.getBoundingClientRect();if(width<1||height<1)return;
    this.renderer.setSize(width,height,false);
    const horizontal=(this.bounds[2]-this.bounds[0])*.57,vertical=(this.bounds[3]-this.bounds[1])*.57;
    const w=Math.max(horizontal,vertical*width/height),h=w*height/width;
    Object.assign(this.camera,{left:-w,right:w,top:h,bottom:-h});this.camera.updateProjectionMatrix();
  }
  private draw=():void=>{
    if(this.disposed)return;
    this.controls.update();
    const target=this.controls.target, dx=THREE.MathUtils.clamp(target.x,this.bounds[0],this.bounds[2])-target.x,
      dy=THREE.MathUtils.clamp(target.y,this.bounds[1],this.bounds[3])-target.y;
    this.camera.position.x+=dx;this.camera.position.y+=dy;target.x+=dx;target.y+=dy;
    if(this.host.clientWidth>0)this.renderer.render(this.scene,this.camera);
    this.host.dataset.drawCalls=String(this.renderer.info.render.calls);
    this.frame=requestAnimationFrame(this.draw);
  };
  private aim(event:PointerEvent):void{
    const r=this.renderer.domElement.getBoundingClientRect();
    this.raycaster.setFromCamera(new THREE.Vector2((event.clientX-r.left)/r.width*2-1,-(event.clientY-r.top)/r.height*2+1),this.camera);
    this.scene.updateMatrixWorld(true);
  }
  private pointerDown=(e:PointerEvent):void=>{
    this.dismiss();this.pointer=e.isPrimary&&e.button===0?{x:e.clientX,y:e.clientY,id:e.pointerId,dragged:false}:undefined;
  };
  private pointerMove=(e:PointerEvent):void=>{
    if(this.pointer&&Math.hypot(e.clientX-this.pointer.x,e.clientY-this.pointer.y)>5)this.pointer.dragged=true;
    if(e.buttons||!this.canPick()){this.dismiss();return;}
    if(!this.chooser.hidden)return;
    this.aim(e);const groups=this.destinationHits();this.emphasize(groups);
    this.hint.hidden=!groups.length;
    if(groups.length){this.hint.textContent=groups.flatMap(g=>g.options.map(o=>o.request.operation)).join(' / ');this.position(this.hint,e.clientX+12,e.clientY+12);}
  };
  private pointerUp=(e:PointerEvent):void=>{
    const start=this.pointer;this.pointer=undefined;
    if(!start||start.id!==e.pointerId||start.dragged||!this.canPick()||Math.hypot(e.clientX-start.x,e.clientY-start.y)>5)return;
    this.aim(e);const groups=this.destinationHits();
    if(groups.length){
      const options=groups.flatMap(g=>g.options);
      if(options.length===1)this.onDestination(options[0]);
      else{const close=document.createElement('button');close.textContent='×';close.setAttribute('aria-label','Close destination chooser');close.onclick=()=>this.dismiss();
        const title=document.createElement('span');title.textContent='Choose a twist';
        const list=document.createElement('div');list.className='destination-options';options.forEach(o=>list.append(this.destinationButton(o)));
        this.chooser.replaceChildren(title,close,list);this.chooser.hidden=false;this.position(this.chooser,e.clientX,e.clientY);}
      return;
    }
    const hit=this.raycaster.intersectObjects(this.batches.map(b=>b.mesh)).find(h=>insideBounds(this.bounds,h.point.x,h.point.y));
    if(!hit||hit.instanceId===undefined){this.onPick(undefined);return;}
    const batch=this.batches.find(b=>b.mesh===hit.object)!;
    const part=this.descriptor.visualParts[batch.instances[hit.instanceId].part];
    const target=JSON.parse(this.geometry.bindHitJSON(part.visualPartId));
    this.onPick(target.status==='Target'?target.pieceId:undefined);
  };
  private pointerCancel=():void=>{this.pointer=undefined;this.dismiss();};
  private pointerLeave=():void=>{this.hint.hidden=true;};
  private contextLost=(e:Event):void=>{e.preventDefault();};
  private contextRestored=():void=>{this.updateTransforms();this.resize();};

  private clearDestinations():void{
    for(const {mesh} of this.ghosts){this.scene.remove(mesh);mesh.dispose();mesh.material.dispose();}
    this.ghosts.length=0;this.groups=[];this.destinationControls.replaceChildren();this.dismiss();this.root.hidden=true;
    Object.assign(this.host.dataset,{destinationCount:'0',destinationGroups:'0',destinationPiece:'',destinationOperations:'',destinationPartCount:'0'});
  }
  private dismiss():void{this.hint.hidden=true;this.chooser.hidden=true;this.chooser.replaceChildren();this.emphasize([]);}
  private destinationButton(option:PieceDestination):HTMLButtonElement{
    const button=document.createElement('button');button.type='button';button.textContent=option.request.operation;
    button.setAttribute('aria-label',`Twist ${option.request.operation}`);
    button.onclick=()=>{if(this.canPick())this.onDestination(option);};return button;
  }
  private position(element:HTMLElement,x:number,y:number):void{
    const r=this.host.getBoundingClientRect();element.style.left=`${Math.max(8,Math.min(r.width-238,x-r.left))}px`;
    element.style.top=`${Math.max(8,Math.min(r.height-140,y-r.top))}px`;
  }
  private destinationHits():DestinationGroup[]{
    const result=new Set<DestinationGroup>();
    for(const hit of this.raycaster.intersectObjects(this.ghosts.map(b=>b.mesh))){
      if(hit.instanceId===undefined||!insideBounds(this.bounds,hit.point.x,hit.point.y))continue;
      const batch=this.ghosts.find(b=>b.mesh===hit.object)!;result.add(batch.groups[hit.instanceId]);
    }
    return [...result];
  }
  private emphasize(groups:DestinationGroup[]):void{
    for(const batch of this.ghosts){batch.groups.forEach((g,i)=>batch.mesh.setColorAt(i,this.color.setHex(groups.includes(g)?0xffdb80:0x9fe6cf)));
      if(batch.mesh.instanceColor)batch.mesh.instanceColor.needsUpdate=true;}
  }
  setDestinations(piece:string|undefined,options:PieceDestination[]):void{
    this.clearDestinations();if(!piece)return;
    if(options.length&&!this.preview){this.preview=this.module.createGeometry(this.session,JSON.stringify(this.realization))??undefined;if(!this.preview)throw new Error('Cannot create surface preview.');}
    const byPose=new Map<string,DestinationGroup>();
    const batches=new Map<string,{matrix:THREE.Matrix4;group:DestinationGroup;part:number}[]>();
    const box=new THREE.Box3();
    for(const option of options){
      const transition=JSON.parse(option.transitionRecord),key=transition.afterState.placementOf[piece] as string;
      const existing=byPose.get(key);if(existing){existing.options.push(option);continue;}
      const group={options:[option]};byPose.set(key,group);this.groups.push(group);
      const prepared=JSON.parse(this.preview!.prepareAnimationJSON(option.transitionRecord));
      if(prepared.status!=='Prepared')throw new Error('Unsupported surface destination.');
      this.preview!.sample(1);const transforms=copyFloatView(this.preview!.transforms() as Float32Array);
      this.descriptor.visualParts.forEach((part,index)=>{
        if(part.pieceId!==piece||part.materialBindingId==='ink')return;
        const list=batches.get(part.meshAssetId)??[];
        for(const image of this.images){
          const matrix=new THREE.Matrix4().multiplyMatrices(image.matrix,this.local.fromArray(transforms,index*16));
          box.copy(this.assets.get(part.meshAssetId)!.boundingBox!).applyMatrix4(matrix);
          if(box.max.x<this.bounds[0]||box.min.x>this.bounds[2]||box.max.y<this.bounds[1]||box.min.y>this.bounds[3])continue;
          list.push({matrix,group,part:index});
        }
        batches.set(part.meshAssetId,list);
      });
    }
    let count=0;
    for(const [asset,list] of batches){
      if(!list.length)continue;
      const port=this.descriptor.visualParts[list[0].part].role==='port';
      const mesh=new THREE.InstancedMesh(this.assets.get(asset)!,new THREE.MeshBasicMaterial({transparent:true,opacity:port?.22:.09,
        depthTest:false,depthWrite:false,side:THREE.DoubleSide,forceSinglePass:true}),list.length);
      list.forEach((item,i)=>{mesh.setMatrixAt(i,item.matrix);mesh.setColorAt(i,this.color.setHex(0x9fe6cf));});
      mesh.renderOrder=port?11:10;mesh.computeBoundingSphere();this.scene.add(mesh);
      this.ghosts.push({mesh,groups:list.map(i=>i.group)});count+=list.length;
    }
    this.root.hidden=false;
    Object.assign(this.host.dataset,{destinationPiece:piece,destinationCount:String(options.length),destinationGroups:String(this.groups.length),
      destinationOperations:options.map(o=>o.request.operation).join(','),destinationPartCount:String(count)});
    const status=document.createElement('span');status.setAttribute('role','status');status.textContent=options.length?'Click a translucent destination':'No legal twists for this piece';
    this.destinationControls.append(status);
    if(options.length){const details=document.createElement('details'),summary=document.createElement('summary'),list=document.createElement('div');
      summary.textContent=`${options.length} twists`;list.className='destination-options';options.forEach(o=>list.append(this.destinationButton(o)));
      details.append(summary,list);this.destinationControls.append(details);}
  }

  private updateOutlines():void{
    for(const outline of this.outlines){const {part,image}=outline.userData as Instance;outline.matrix.multiplyMatrices(this.images[image].matrix,this.local.fromArray(this.frameData,part*16));}
  }
  highlight(selected:string|undefined,blocked:string[]):void{
    for(const {mesh,instances} of this.batches){
      instances.forEach(({part},i)=>{
        const p=this.descriptor.visualParts[part],isSelected=p.pieceId===selected,isBlocked=blocked.includes(p.pieceId);
        let color=palette[p.materialBindingId]??0x999999;
        if(isBlocked)color=0xff553e;else if(isSelected&&p.materialBindingId==='body')color=0xffc44c;
        this.color.setHex(color);if(selected&&!isSelected&&!isBlocked)this.color.multiplyScalar(.62);mesh.setColorAt(i,this.color);
      });if(mesh.instanceColor)mesh.instanceColor.needsUpdate=true;
    }
    for(const line of this.outlines)this.scene.remove(line);this.outlines.length=0;
    this.descriptor.visualParts.forEach((p,part)=>{
      if(p.pieceId!==selected||p.materialBindingId!=='body')return;
      let edges=this.edgeAssets.get(p.meshAssetId);if(!edges){edges=new THREE.EdgesGeometry(this.assets.get(p.meshAssetId)!);this.edgeAssets.set(p.meshAssetId,edges);}
      this.images.forEach((_,image)=>{const line=new THREE.LineSegments(edges,this.outlineMaterial);line.matrixAutoUpdate=false;line.renderOrder=22;line.userData={part,image};this.outlines.push(line);this.scene.add(line);});
    });this.updateOutlines();
    this.host.dataset.selectedPiece=selected??'';this.host.dataset.blockedPieces=blocked.join(',');
  }
  setState(state:string):void{this.clearDestinations();this.geometry.setStateJSON(state);this.updateTransforms();}
  prepare(record:string):void{this.clearDestinations();const r=JSON.parse(this.geometry.prepareAnimationJSON(record));if(r.status!=='Prepared')throw new Error(r.diagnostics?.[0]?.message??'Unsupported surface animation.');}
  sample(progress:number):void{this.geometry.sample(progress);this.updateTransforms();}
  cameraState():unknown{return{position:this.camera.position.toArray(),target:this.controls.target.toArray(),zoom:this.camera.zoom};}
  restoreCamera(value:unknown):void{
    const d=value as {position?:number[];target?:number[];zoom?:number}|undefined;
    if(!d||d.position?.length!==3||d.target?.length!==3||![...d.position,...d.target,d.zoom??1].every(Number.isFinite))return;
    this.camera.position.set(d.position[0],d.position[1],15);this.controls.target.set(d.target[0],d.target[1],0);
    this.camera.zoom=THREE.MathUtils.clamp(d.zoom??1,.7,5);this.camera.updateProjectionMatrix();this.controls.update();
  }
  dispose():void{
    this.disposed=true;cancelAnimationFrame(this.frame);this.resizeObserver.disconnect();this.controls.dispose();
    const c=this.renderer.domElement;c.removeEventListener('pointerdown',this.pointerDown);c.removeEventListener('pointermove',this.pointerMove);
    c.removeEventListener('pointerup',this.pointerUp);c.removeEventListener('pointercancel',this.pointerCancel);c.removeEventListener('pointerleave',this.pointerLeave);
    c.removeEventListener('webglcontextlost',this.contextLost);c.removeEventListener('webglcontextrestored',this.contextRestored);
    this.clearDestinations();this.preview?.delete();this.root.remove();
    for(const {mesh} of this.batches){mesh.dispose();mesh.material.dispose();}
    for(const asset of this.assets.values())asset.dispose();for(const asset of this.edgeAssets.values())asset.dispose();this.outlineMaterial.dispose();
    for(const object of this.decorations){if(object instanceof THREE.LineSegments){object.geometry.dispose();object.material.dispose();}else if(object instanceof THREE.Sprite)object.material.dispose();}
    for(const texture of this.textures)texture.dispose();this.renderer.dispose();this.renderer.forceContextLoss();c.remove();this.geometry.delete();
  }
}
