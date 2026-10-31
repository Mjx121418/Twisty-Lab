import { runtimeViews, type Realization, type RuntimeBundle, type RuntimeDocuments } from '../../kernel/runtime';
export type Preset = {
  id: string; label: string; dataId?: string; source: RuntimeDocuments | undefined; realizations: Realization[];
  spherical?: Realization; surface?: boolean; cameraScale?: number;
  euclideanLabel?: string; stereographic?: boolean; gearedEdges?: string[]; legacySource?: string;
};
const assets = import.meta.glob<string>('../../runtime-puzzles/**/*.json', { query: '?url', import: 'default', eager: true });
const loading = new Map<string, Promise<void>>();
async function read(path: string): Promise<string> {
  const url = assets['../../runtime-puzzles/'+path];
  if (!url) throw new Error('Missing runtime asset: '+path);
  const response = await fetch(url);
  if (!response.ok) throw new Error(`Could not load runtime asset (${response.status}): ${path}`);
  return response.text();
}
export function installBundle(preset: Preset, bundle: RuntimeBundle): void {
  const views = runtimeViews(bundle);
  preset.realizations = views.filter(view => !view.kind.endsWith('-spherical'));
  preset.spherical = views.find(view => view.kind.endsWith('-spherical'));
  if (preset.surface) {
    const model = preset.realizations[0];
    preset.realizations = ['square','tiles'].map(mode => ({ ...model, id: `${model.id}-${mode}` }));
  }
  preset.source = { abstract: JSON.stringify(bundle.abstract), controls: JSON.stringify(bundle.controls), presentation: JSON.stringify(bundle.presentation) };
}
export function loadPreset(preset: Preset): Promise<void> {
  if (preset.source) return Promise.resolve();
  const previous=loading.get(preset.id);if(previous)return previous;
  const id=preset.dataId??preset.id;
  const result=Promise.all(['abstract','controls','presentation','interpretation','build-manifest'].map(name=>read(`${id}/${name}.json`)))
    .then(async ([abstract,controls,presentation,interpretation,receiptText])=> {
      const receipt=JSON.parse(receiptText) as {realizations:{id:string}[]};
      const views=await Promise.all(receipt.realizations.map(async view=> {
        const prefix=`${id}/realizations/${view.id}/`;
        const [envelope,payload,hint]=await Promise.all(['realization','geometry','presentation'].map(name=>read(prefix+name+'.json')));
        return {realization:JSON.parse(envelope),payload,presentation:JSON.parse(hint)};
      }));
      installBundle(preset,{abstract:JSON.parse(abstract),controls:JSON.parse(controls),presentation:JSON.parse(presentation),interpretation:JSON.parse(interpretation),views});
      // Preserve original native serialization for digest-sensitive documents.
      preset.source={abstract,controls,presentation};
    }).catch(error=>{loading.delete(preset.id);throw error;});
  loading.set(preset.id,result);return result;
}
const item=(id:string,label:string,extra:Partial<Preset>={}):Preset=>({id,label,source:undefined,realizations:[],...extra});
export const gearEdges=['UF','UR','UB','UL','DF','DR','DB','DL','FR','FL','BR','BL'];
export const presets:Preset[]=[
  item('cube3','3 × 3 cube'),
  item('bandaged-uf-ufr','Bandaged cube · UF + UFR',{dataId:'bandaged'}),
  item('helicopter','Helicopter Cube · jumbling'),item('bagua','Bagua Cube'),
  ...['a','b','c','d'].map(letter=>item('melty-'+letter,`Melty ${letter.toUpperCase()} · 85° axes`,{cameraScale:2/3})),
  item('cell24','S³ 24-cell · 672 pieces',{stereographic:true}),
  item('cell24-192','S³ 24-cell · 192 movable pieces · 45°',{stereographic:true}),
  item('tesseract3','S³ 8-cell · 3 × 3 × 3 × 3',{stereographic:true}),
  item('icosahedron-110','Face-turning icosahedron · 110 pieces',{euclideanLabel:'Icosahedron'}),
  item('icosahedron-152','Face-turning icosahedron · 152 pieces',{euclideanLabel:'Icosahedron'}),
  item('gear','Gear Cube',{gearedEdges:gearEdges}),item('gear-extreme','Gear Cube Extreme',{gearedEdges:gearEdges.slice(0,8)}),
  item('gear-plain','Gear family · all plain',{gearedEdges:[]}),
  ...[['flat-torus','Flat torus · face turning'],['flat-klein-bottle','Klein bottle · face turning'],
    ['flat-hex-torus','Hexagonal torus · face turning'],['flat-hex-klein-bottle','Hexagonal Klein bottle · face turning'],
    ['flat-pillowcase','Pillowcase · 4 disks · radius 0.846'],['flat-pillowcase-6','Pillowcase · 6 disks · radius 0.6'],
    ['flat-333','333 orbifold · 3 disks · radius 0.72'],['flat-333-6','333 orbifold · 6 disks · radius 0.36']]
    .map(([id,label])=>item(id,label,{surface:true})),
];
