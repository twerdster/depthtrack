/** Integration tests execute real app handlers, worker client, tree and depth code.
 * Only DOM/media/network/worker surfaces are faked. No camera/model download.
 */
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

const nodes=readFileSync(new URL('../../../backends/model/depth10/nodes.bin',import.meta.url));
const leaves=readFileSync(new URL('../../../backends/model/depth10/leaves.bin',import.meta.url));
const metadata={backend:'wasm',dtype:'q8',size:294};
const PIXELS=128*128;
let sequence=0;
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return{promise,resolve,reject};}
async function flush(){for(let i=0;i<12;++i)await Promise.resolve();}
async function until(condition){for(let i=0;i<30&&!condition();++i)await flush();assert.ok(condition(),'Expected async integration state was not reached');}
class Element {
  constructor(id=''){this.id=id;this.listeners=new Map();this.value='';this.textContent='';this.hidden=false;this.disabled=false;this.width=294;this.height=294;this.files=[];this.draws=0;this.arcs=0;}
  addEventListener(type,fn){if(!this.listeners.has(type))this.listeners.set(type,[]);this.listeners.get(type).push(fn);}
  removeEventListener(type,fn){this.listeners.set(type,(this.listeners.get(type)||[]).filter(x=>x!==fn));}
  fire(type,extra={}){const results=(this.listeners.get(type)||[]).map(fn=>fn.call(this,{type,target:this,...extra}));return results.length===1?results[0]:Promise.all(results);}
  setAttribute(){} removeAttribute(){}
  getContext(){if(!this.context){const owner=this;this.context={
    clearRect(){},putImageData(){},strokeRect(){},fillRect(){},fillText(){},setTransform(){},
    drawImage(){owner.draws++;},createImageData(w,h){return{width:w,height:h,data:new Uint8ClampedArray(w*h*4)};},
    getImageData(x,y,w,h){return{width:w,height:h,data:new Uint8ClampedArray(w*h*4)};},
    beginPath(){},moveTo(){},lineTo(){},stroke(){},setLineDash(){},arc(){owner.arcs++;},
  };}return this.context;}
}
class FakeWorker {
  constructor(url,options){this.url=String(url);this.options=options;this.handlers=new Map();this.messages=[];this.terminated=false;}
  addEventListener(type,fn){this.handlers.set(type,fn);}
  postMessage(message,transfer){this.messages.push({message,transfer});}
  terminate(){this.terminated=true;}
  emit(data){this.handlers.get('message')?.({data});}
  reply(type,result){this.emit({type,id:this.messages.at(-1).message.id,result});}
}
async function setup(t,{manualAssets=false}={}){
  const elements=new Map();const get=id=>{if(!elements.has(id))elements.set(id,new Element(id));return elements.get(id);};
  for(const[id,value]of Object.entries({device:'auto',facing:'user','near-cm':'30','far-cm':'70',cutoff:'80',roi:'.85'}))get(id).value=value;
  get('depth').width=get('depth').height=get('labels').width=get('labels').height=128;
  const document=new Element('document');document.hidden=false;document.getElementById=get;
  const canvases=[];document.createElement=tag=>{const element=new Element(tag);if(tag==='canvas')canvases.push(element);return element;};
  const window=new Element('window');const workers=[];const fetches=[];const intervals=[];const bitmapRequests=[];
  const permission=deferred();let bitmapFactory=()=>{const d=deferred();bitmapRequests.push(d);return d.promise;};
  const values={document,window,
    Worker:class extends FakeWorker{constructor(...args){super(...args);workers.push(this);}},
    fetch:(path)=>{const d=deferred();const file=String(path).includes('nodes.bin')?nodes:leaves;const response={ok:true,status:200,arrayBuffer:async()=>file.buffer.slice(file.byteOffset,file.byteOffset+file.byteLength)};fetches.push({...d,path,response});if(!manualAssets)d.resolve(response);return d.promise;},
    createImageBitmap:file=>bitmapFactory(file),
    setInterval:fn=>{intervals.push(fn);return intervals.length;},
    navigator:{mediaDevices:{getUserMedia:()=>permission.promise}},
  };
  const old=new Map();for(const[key,value]of Object.entries(values)){old.set(key,Object.getOwnPropertyDescriptor(globalThis,key));Object.defineProperty(globalThis,key,{configurable:true,writable:true,value});}
  const app=await import(new URL(`../app.mjs?integration=${++sequence}`,import.meta.url));
  t.after(async()=>{get('cancel').fire('click');await flush();for(const[key,descriptor]of old){if(descriptor)Object.defineProperty(globalThis,key,descriptor);else delete globalThis[key];}});
  const video=get('video');video.videoWidth=640;video.videoHeight=480;video.readyState=4;video.play=async()=>{};video.pause=()=>{};video.callbacks=new Map();video.requestVideoFrameCallback=fn=>{const id=video.callbacks.size+1;video.callbacks.set(id,fn);return id;};video.cancelVideoFrameCallback=id=>video.callbacks.delete(id);
  return{app,get,workers,fetches,canvases,bitmapRequests,permission,
    resolveAssets(indices=fetches.map((_,i)=>i)){for(const i of indices)fetches[i].resolve(fetches[i].response);},
    setBitmapFactory(fn){bitmapFactory=fn;},
    async load(){const work=app.loadModel();await until(()=>workers.some(w=>w.messages.at(-1)?.message.type==='load'&&!w.terminated));const worker=workers.at(-1);worker.reply('loaded',metadata);await work;return worker;},
  };
}
function result(raw,width=128,height=128){return{data:raw instanceof Float32Array?raw:new Float32Array(width*height).fill(raw),width,height,dims:[1,height,width],inputShape:[1,3,294,294],preprocessMs:2,inferenceMs:8,...metadata};}
function image(){return{width:294,height:294,data:new Uint8ClampedArray(294*294*4)};}

test('cancel during initial asset fetch cannot construct a worker or revive app',async t=>{
  const h=await setup(t,{manualAssets:true});const loading=h.app.loadModel();
  assert.equal(h.fetches.length,2);assert.equal(h.get('load').disabled,true);
  h.get('cancel').fire('click');const message=h.get('status').textContent;
  h.resolveAssets();await loading;
  assert.equal(h.workers.length,0);assert.equal(h.app.diagnostics().ready,false);
  assert.equal(h.get('status').textContent,message);assert.equal(h.get('load').disabled,false);
  assert.equal(h.get('start').disabled,true);assert.equal(h.get('progress').hidden,true);
});

test('late cancelled asset load cannot replace a newer loaded worker',async t=>{
  const h=await setup(t,{manualAssets:true});const oldLoad=h.app.loadModel();h.get('cancel').fire('click');
  const newLoad=h.app.loadModel();assert.equal(h.fetches.length,4);
  h.resolveAssets([2,3]);await until(()=>h.workers.length===1);
  h.workers[0].reply('loaded',metadata);await newLoad;const message=h.get('status').textContent;
  h.resolveAssets([0,1]);await oldLoad;
  assert.equal(h.workers.length,1);assert.equal(h.workers[0].terminated,false);
  assert.equal(h.app.diagnostics().ready,true);assert.equal(h.get('status').textContent,message);
});

test('cancelled worker load and its late callbacks cannot clobber an in-progress reload',async t=>{
  const h=await setup(t);const oldLoad=h.app.loadModel();await until(()=>h.workers.length===1);
  const old=h.workers[0];h.get('cancel').fire('click');const freshLoad=h.app.loadModel();
  await until(()=>h.workers.length===2);await oldLoad;
  assert.equal(old.terminated,true);assert.equal(h.get('load').disabled,true);assert.equal(h.app.diagnostics().ready,false);
  const message=h.get('status').textContent;
  old.emit({type:'status',status:{state:'ready',message:'STALE WORKER'}});
  old.reply('loaded',{...metadata,backend:'STALE'});await flush();
  assert.equal(h.get('status').textContent,message);assert.equal(h.get('load').disabled,true);
  h.workers[1].reply('loaded',metadata);await freshLoad;
  assert.equal(h.app.diagnostics().ready,true);assert.equal(h.get('start').disabled,false);
});

test('cancelled photo decode cannot predict, draw, or unlock a newer model load',async t=>{
  const h=await setup(t);await h.load();h.get('photo').files=[{name:'old-photo.png'}];
  const photo=h.get('photo').fire('change');assert.equal(h.bitmapRequests.length,1);
  h.get('cancel').fire('click');const reload=h.app.loadModel();await until(()=>h.workers.length===2);
  let closed=0;h.bitmapRequests[0].resolve({width:300,height:200,close(){closed++;}});await photo;
  assert.equal(closed,1);assert.equal(h.canvases.length,0);
  assert.equal(h.workers.flatMap(w=>w.messages).filter(x=>x.message.type==='predict').length,0);
  assert.equal(h.get('load').disabled,true);assert.equal(h.get('progress').hidden,false);assert.equal(h.app.diagnostics().frameNumber,0);
  h.workers[1].reply('loaded',metadata);await reload;assert.equal(h.app.diagnostics().ready,true);
});

test('cancelled photo inference cannot paint stale output or unlock a newer model load',async t=>{
  const h=await setup(t);await h.load();h.setBitmapFactory(async()=>({width:294,height:294,close(){}}));
  h.get('photo').files=[{name:'in-flight.png'}];const photo=h.get('photo').fire('change');
  await until(()=>h.workers[0].messages.at(-1)?.message.type==='predict');
  h.get('cancel').fire('click');const reload=h.app.loadModel();await until(()=>h.workers.length===2);await photo;
  h.workers[0].reply('prediction',result(2));await flush();
  assert.equal(h.app.diagnostics().frameNumber,0);assert.equal(h.app.rawSnapshot(),null);
  assert.equal(h.get('load').disabled,true);assert.equal(h.app.diagnostics().ready,false);
  h.workers[1].reply('loaded',metadata);await reload;
  assert.equal(h.app.diagnostics().ready,true);assert.equal(h.get('photo').disabled,false);
});

test('camera starting state disables competing input controls and stop cancels late permission',async t=>{
  const h=await setup(t);await h.load();const starting=h.get('start').fire('click');
  assert.equal(h.get('start').disabled,true);assert.equal(h.get('load').disabled,true);
  assert.equal(h.get('photo').disabled,true);assert.equal(h.get('facing').disabled,true);assert.equal(h.get('stop').disabled,false);
  await flush();h.get('stop').fire('click');await starting;
  let stops=0;const track={stop(){stops++;}};h.permission.resolve({getTracks:()=>[track]});await flush();
  assert.equal(stops,1);assert.equal(h.get('start').disabled,false);assert.equal(h.get('load').disabled,false);
});

test('real calibration buttons seed still-photo tracks and invalid cutoff retains valid gating',async t=>{
  const h=await setup(t);const worker=await h.load();h.setBitmapFactory(async()=>({width:294,height:294,close(){}}));
  async function photo(raw){const before=worker.messages.length;h.get('photo').files=[{name:'anchor.png'}];const work=h.get('photo').fire('change');await until(()=>worker.messages.length>before&&worker.messages.at(-1)?.message.type==='predict');worker.reply('prediction',result(raw));await work;}
  await photo(4);h.get('near').fire('click');await photo(1);h.get('far').fire('click');
  let measurement=h.app.diagnostics().measurement;
  assert.equal(measurement.calibration.valid,true);assert.equal(measurement.states.length,5,'Still photo must seed the reset trackers');
  assert.equal(measurement.estimated,true);assert.ok(Number.isFinite(measurement.modelRoundTripMs));
  // Drive actual tree/component geometry using a packaged depth example, not canned states.
  h.get('roi').value='1';h.get('roi').fire('change');h.get('cutoff').value='500';h.get('cutoff').fire('change');
  const bytes=readFileSync(new URL('../../../backends/fixtures/historical_003/depth.bin',import.meta.url));
  const raw=new Float32Array(PIXELS);const c=h.app.diagnostics().measurement.calibration;
  for(let i=0;i<PIXELS;++i){const z=bytes.readUInt16LE(2*i);raw[i]=z?((1000/z)-c.b)/c.a:-100;}
  await photo(raw);measurement=h.app.diagnostics().measurement;
  assert.equal(measurement.states.length,5);assert.ok(measurement.states.some(s=>s.accepted),'Actual pixel classes must produce fresh blob observations');
  const before=measurement.retainedPixels;
  h.get('cutoff').value='';assert.doesNotThrow(()=>h.get('cutoff').fire('change'));assert.equal(h.get('error').hidden,false);
  await photo(raw);measurement=h.app.diagnostics().measurement;
  assert.equal(measurement.retainedPixels,before);assert.equal(h.app.diagnostics().ready,true);
  assert.equal(measurement.states.length,5);assert.ok(h.get('labels').arcs>0,'Tracked XYZ must reach real canvas projection path');
});


test('a camera result whose isCurrent guard expires does not paint or increment frames',async t=>{
  const h=await setup(t);const worker=await h.load();let current=true;
  const work=h.app.processImage(image(),.2,()=>current);
  assert.equal(worker.messages.at(-1).message.type,'predict');current=false;
  worker.reply('prediction',result(2));assert.equal(await work,null);
  assert.equal(h.app.diagnostics().frameNumber,0);assert.equal(h.app.rawSnapshot(),null);
  assert.equal(h.app.diagnostics().ready,true);
});


test('pagehide unloads the model and a restored page requires a fresh explicit load', async t => {
  const h = await setup(t); const old = await h.load();
  globalThis.window.fire('pagehide');
  assert.equal(old.terminated, true); assert.equal(h.app.diagnostics().ready, false);
  assert.equal(h.get('load').disabled, false); assert.equal(h.get('start').disabled, true);
  assert.equal(h.get('photo').disabled, true); assert.equal(h.get('progress').hidden, true);
  const fresh = await h.load(); assert.notEqual(fresh, old);
  assert.equal(h.app.diagnostics().ready, true); assert.equal(h.get('start').disabled, false);
});
