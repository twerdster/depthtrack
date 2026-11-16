import { PackedDepthTree } from './tree.mjs';
import { fitInverseDepthCalibration, resampleDepth, medianRawPatch, largestClassObservations } from './depth.mjs';
import { DepthModelClient } from './model-client.mjs';
import { FrameScheduler } from './camera.mjs';
import { AdaptivePointTracker } from '../tracker.mjs';
const $ = id => document.getElementById(id);
const rgb=$('rgb'), depthCanvas=$('depth'), labelsCanvas=$('labels');
const colors=[[255,104,104],[255,203,77],[70,219,140],[72,191,255],[194,128,255],[200,208,204],[130,156,148],[5,14,18]];
let tree=null, client=null, scheduler=null, ready=false, busy=false, frameNumber=0, latest=null, nearAnchor=null, farAnchor=null, calibration=null;
let trackers=Array.from({length:5},()=>new AdaptivePointTracker());
const depth=new Uint16Array(128*128), labels=new Uint8Array(128*128);
let lastCompleted=null, lastBackend='', rateTimes=[], processingSince=null, loadEpoch=0, sourceKind=null, cutoffMm=800;
function status(message){$('status').textContent=message;}
function error(err){$('error').textContent=String(err?.message||err);$('error').hidden=false;}
function clearError(){$('error').hidden=true;}
function clearFrame(){latest=null;lastCompleted=null;$('near').disabled=$('far').disabled=$('save').disabled=true;for(const c of [rgb,depthCanvas,labelsCanvas])c.getContext('2d').clearRect(0,0,c.width,c.height);}
function resetTracking(){trackers=Array.from({length:5},()=>new AdaptivePointTracker());rateTimes=[];}
function resetCalibration(){nearAnchor=farAnchor=calibration=null;$('near-value').textContent=$('far-value').textContent='Not captured';$('cal-status').textContent='Two anchors required. The centre box samples raw relative inverse depth.';resetTracking();if(latest)renderResult(latest,false);}
function controls(){const cameraActive=!!scheduler&&scheduler.state!=='stopped', inFlight=!!scheduler?.inFlight;$('load').disabled=busy||cameraActive||inFlight;$('device').disabled=busy||cameraActive||inFlight;$('start').disabled=!ready||busy||cameraActive||inFlight;$('stop').disabled=!cameraActive;$('facing').disabled=cameraActive;$('photo').disabled=!ready||busy||cameraActive||inFlight;$('cancel').disabled=!busy;}
async function boundedFetch(path,expected){const response=await fetch(path,{signal:AbortSignal.timeout(20000)});if(!response.ok)throw new Error(`Cannot load ${path}: HTTP ${response.status}`);const buffer=await response.arrayBuffer();if(buffer.byteLength!==expected)throw new Error(`Invalid tree asset: ${path}`);return buffer;}
export async function loadModel(){
  if(busy)return;const epoch=++loadEpoch;busy=true;ready=false;controls();clearError();client?.dispose();client=null;clearFrame();resetCalibration();$('progress').hidden=false;$('progress').removeAttribute('value');
  try{
    if(!tree){const [n,l]=await Promise.all([boundedFetch('./model/nodes.bin',12276),boundedFetch('./model/leaves.bin',1024)]);if(epoch!==loadEpoch)return;tree=new PackedDepthTree(n,l);}
    if(epoch!==loadEpoch)return;const candidate=new DepthModelClient({onStatus:s=>{if(epoch===loadEpoch)status(s.message||String(s.state));},onProgress:p=>{if(epoch===loadEpoch&&Number.isFinite(p.progress)){$('progress').value=p.progress;status(`Downloading ${p.file||'model'} · ${p.progress.toFixed(0)}%`);}}});
    client=candidate;const info=await candidate.load({device:$('device').value,size:294});if(epoch!==loadEpoch){candidate.dispose();return;}ready=true;lastBackend=`${info.backend} / ${info.dtype}`;
    status(`Model ready · ${lastBackend}. Start the camera or choose a local photo.`);
  }catch(err){if(epoch!==loadEpoch)return;client?.dispose();client=null;error(err);status('Load failed. You can retry, or select CPU / compatibility mode.');}
  finally{if(epoch===loadEpoch){busy=false;$('progress').hidden=true;controls();}}
}
function fit(){
 if(!nearAnchor||!farAnchor)return;
 calibration=fitInverseDepthCalibration({nearRaw:nearAnchor.raw,farRaw:farAnchor.raw,nearMm:nearAnchor.mm,farMm:farAnchor.mm});
 if(!calibration.valid){$('cal-status').textContent=`Calibration rejected: ${calibration.reason}. Recapture the anchors.`;calibration=null;}
 else{$('cal-status').textContent=`Approximate scale set at ${nearAnchor.mm/10} and ${farAnchor.mm/10} cm. Keep the scene fixed; distances remain estimates.`;}
 resetTracking();if(latest)renderResult(latest,true);
}
function anchor(which){
 if(!latest)return;
 const patch=medianRawPatch(latest.data,latest.width,latest.height,{fraction:.12});
 const mm=Number($(which+'-cm').value)*10;
 if(!patch.valid||!Number.isFinite(mm)||mm<=0){error(new Error('Use a positive measured distance and a finite centre depth patch.'));return;}
 const value={raw:patch.median,mm};if(which==='near')nearAnchor=value;else farAnchor=value;
 $(which+'-value').textContent=`Raw ${value.raw.toFixed(4)} · ${mm/10} cm · frame ${frameNumber}`;fit();
}
function paintRelative(raw,w,h){
 const valid=Array.from(raw).filter(Number.isFinite).sort((a,b)=>a-b);
 const lo=valid[Math.floor(valid.length*.02)]||0,hi=valid[Math.floor(valid.length*.98)]||lo+1;
 const ctx=depthCanvas.getContext('2d'),im=ctx.createImageData(128,128);
 for(let y=0;y<128;y++)for(let x=0;x<128;x++){const v=raw[Math.min(h-1,Math.floor((y+.5)*h/128))*w+Math.min(w-1,Math.floor((x+.5)*w/128))];const g=Number.isFinite(v)?Math.max(0,Math.min(255,Math.round((v-lo)/(hi-lo||1)*255))):0;const i=(y*128+x)*4;im.data.set([g,g,g,255],i);}
 ctx.putImageData(im,0,0);$('depth-caption').textContent='relative · brighter = nearer';
}
function renderResult(result,advance=true){
 const start=performance.now();let retained=0,observations=null,states=[];
 if(calibration){
   const cutoff=cutoffMm;
   resampleDepth(result.data,result.width,result.height,calibration,depth,{nearMm:150,farMm:cutoff,method:'bilinear'});
   const fraction=Number($('roi').value), edge=64*(1-fraction);
   for(let y=0;y<128;y++)for(let x=0;x<128;x++){const i=y*128+x;if(x<edge||y<edge||x>=128-edge||y>=128-edge)depth[i]=0;if(depth[i])retained++;}
   tree.predict(depth,labels);
   observations=largestClassObservations(labels,depth,{horizontalFovDeg:60,minArea:12});
   if(advance)states=trackers.map((t,i)=>t.update(result.timestampS,observations.observations[i]));
   const ctx=depthCanvas.getContext('2d'),im=ctx.createImageData(128,128);
   for(let i=0;i<depth.length;i++){const g=depth[i]?Math.max(35,Math.round(255*(1-depth[i]/cutoff))):0;im.data.set([g,g,g,255],i*4);}ctx.putImageData(im,0,0);
   $('depth-caption').textContent=`estimated · > ${cutoff/10} cm removed`;
 }else{labels.fill(7);paintRelative(result.data,result.width,result.height);}
 const ctx=labelsCanvas.getContext('2d'),im=ctx.createImageData(128,128);
 for(let i=0;i<labels.length;i++)im.data.set([...(colors[labels[i]]||[42,52,59]),255],i*4);
 ctx.putImageData(im,0,0);
 const f=128/(2*Math.tan(Math.PI/6));
 const project=p=>[p[0]*f/p[2]+63.5,p[1]*f/p[2]+63.5];
 for(let i=0;i<states.length;i++){const s=states[i];if(!s.position)continue;ctx.strokeStyle=`rgb(${colors[i].join(',')})`;ctx.lineWidth=1.1;ctx.beginPath();s.history.forEach((h,j)=>{const [x,y]=project(h.position);j?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();ctx.setLineDash(s.accepted?[]:[2,2]);const[x,y]=project(s.position);ctx.beginPath();ctx.arc(x,y,3.8,0,2*Math.PI);ctx.strokeStyle='#fff';ctx.stroke();ctx.setLineDash([]);}
 // Keep RGB and its derived outputs synchronized; never overlay an old result on live video.
 const rgbCtx=rgb.getContext('2d');rgbCtx.putImageData(result.image,0,0);rgbCtx.strokeStyle='#7ce8c1';rgbCtx.lineWidth=1;rgbCtx.strokeRect(294*.44,294*.44,294*.12,294*.12);
 const treeMs=performance.now()-start;
 $('counts').textContent=calibration?`${retained} gated pixels · ${states.filter(s=>s.accepted).length}/5 fresh candidate tracks`:'Tree awaits two measured anchors';
 const total=result.preprocessMs+result.inferenceMs+treeMs;
 $('timing').textContent=`${lastBackend} · depth ${result.inferenceMs.toFixed(0)} ms · adapter/tree/render ${treeMs.toFixed(1)} ms · ${result.inputShape?.join('×')||'294²'} input${rateTimes.length>1?' · '+((rateTimes.length-1)*1000/(rateTimes.at(-1)-rateTimes[0])).toFixed(1)+' results/s':''}`;
 result.measurement={frame:frameNumber,preprocessMs:result.preprocessMs,inferenceMs:result.inferenceMs,adapterTreeRenderMs:treeMs,accountedComputeMs:total,modelRoundTripMs:result.modelRoundTripMs,retainedPixels:retained,observations:observations?.observations??null,states,calibration:calibration?{...calibration}:null,estimated:true};
 return result.measurement;
}
export async function processImage(image,timestampS=performance.now()/1000,isCurrent=()=>true){
 if(!ready||!client)throw new Error('Load the depth model first.');
 const model=client,epoch=loadEpoch,token={started:performance.now()};processingSince=token;let result;try{result=await model.predict(image);}catch(err){if(epoch!==loadEpoch||client!==model||!isCurrent())return null;ready=false;controls();throw err;}finally{if(processingSince===token)processingSince=null;}if(epoch!==loadEpoch||client!==model||!isCurrent())return null;lastBackend=`${result.backend} / ${result.dtype}`;result.modelRoundTripMs=performance.now()-token.started;result.image=image;result.timestampS=timestampS;
 latest=result;frameNumber++;lastCompleted=performance.now();rateTimes.push(lastCompleted);if(rateTimes.length>15)rateTimes.shift();
 const measurement=renderResult(result);$('near').disabled=$('far').disabled=$('save').disabled=false;
 status(`Frame ${frameNumber} processed locally · ${result.backend} / ${result.dtype}. ${calibration?'Tree running on estimated depth.':'Capture two anchors to enable the tree.'}`);
 return measurement;
}
$('load').addEventListener('click',loadModel);
$('cancel').addEventListener('click',()=>{loadEpoch++;client?.dispose();client=null;ready=false;busy=false;$('progress').hidden=true;processingSince=null;clearError();status('Model work cancelled. Load the model again to continue.');controls();});
$('start').addEventListener('click',async()=>{
 clearError();sourceKind='camera';clearFrame();resetCalibration();
 scheduler??=new FrameScheduler({video:$('video'),size:294,maxFps:15,frameTimeoutMs:35000,onFrame:async({canvas,timestampS,signal,isCurrent})=>{const image=canvas.getContext('2d').getImageData(0,0,294,294);if(signal.aborted)return;await processImage(image,timestampS,isCurrent);},onStateChange:()=>controls(),onError:err=>{error(err);status('Camera processing stopped. Retry the camera or reload the model.');controls();}});
 try{await scheduler.start({facingMode:$('facing').value});status('Camera started. Waiting for the first depth result…');}catch(err){error(err);}finally{controls();}
});
$('stop').addEventListener('click',()=>{scheduler?.stop('user');status('Camera stopped and released. Last completed result remains visible.');controls();});
$('photo').addEventListener('change',async()=>{
 const file=$('photo').files?.[0];if(!file||busy)return;const epoch=loadEpoch,model=client;busy=true;controls();clearError();if(sourceKind!=='photo'){clearFrame();resetCalibration();}sourceKind='photo';resetTracking();
 let bitmap;try{bitmap=await createImageBitmap(file);if(epoch!==loadEpoch||client!==model)return;const canvas=document.createElement('canvas');canvas.width=canvas.height=294;const s=Math.min(bitmap.width,bitmap.height);const ctx=canvas.getContext('2d');ctx.drawImage(bitmap,(bitmap.width-s)/2,(bitmap.height-s)/2,s,s,0,0,294,294);await processImage(ctx.getImageData(0,0,294,294),performance.now()/1000,()=>epoch===loadEpoch&&client===model);}catch(err){if(epoch===loadEpoch&&client===model)error(err);}finally{bitmap?.close();if(epoch===loadEpoch&&client===model){busy=false;controls();}}
});
$('near').addEventListener('click',()=>anchor('near'));$('far').addEventListener('click',()=>anchor('far'));
for(const id of ['near-cm','far-cm'])$(id).addEventListener('change',resetCalibration);
$('reset').addEventListener('click',resetCalibration);
$('roi').addEventListener('change',()=>{resetTracking();if(latest)renderResult(latest,true);});
$('cutoff').addEventListener('change',()=>{const candidate=Number($('cutoff').value)*10;if(!Number.isFinite(candidate)||candidate<=150||candidate>5000){error(new Error('Cutoff must be greater than 15 cm and at most 500 cm. Keeping the previous cutoff.'));return;}cutoffMm=candidate;clearError();resetTracking();if(latest)renderResult(latest,true);});
$('facing').addEventListener('change',resetCalibration);
$('save').addEventListener('click',()=>{
 const canvas=document.createElement('canvas');canvas.width=1020;canvas.height=460;const ctx=canvas.getContext('2d');ctx.fillStyle='#0c171c';ctx.fillRect(0,0,1020,460);ctx.font='bold 24px system-ui';ctx.fillStyle='#7ce8c1';ctx.fillText('DepthTrack · monocular depth experiment',24,35);[rgb,depthCanvas,labelsCanvas].forEach((c,i)=>ctx.drawImage(c,24+i*332,78,308,308));ctx.font='14px system-ui';ctx.fillStyle='#e3eeeb';['RGB · synchronized frame','Estimated depth · two-anchor scale','Ten-split tree · candidate regions'].forEach((s,i)=>ctx.fillText(s,24+i*332,65));ctx.fillText($('timing').textContent,24,412);ctx.fillStyle='#a6b9b7';ctx.fillText('Experimental labels, unverified anatomy. '+(calibration?'Approximate calibrated scale.':'Uncalibrated relative depth; tree disabled.'),24,440);
 canvas.toBlob(blob=>{if(!blob)return;const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='depthtrack-monocular-frame.png';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);});
});
window.addEventListener('pagehide',()=>{loadEpoch++;scheduler?.stop('page hidden');client?.dispose();client=null;ready=false;busy=false;processingSince=null;$('progress').hidden=true;status('Camera released and model unloaded. Load the model again to continue.');controls();});
document.addEventListener('visibilitychange',()=>{if(document.hidden&&scheduler?.running){scheduler.stop('page hidden');status('Camera released while the page is hidden. Start again to continue.');controls();}});
window.addEventListener('unhandledrejection',e=>{error(e.reason);});
window.depthtrackLiveReady=true;
// Small diagnostics surface for reproducible local smoke tests; never contains image uploads.
setInterval(()=>{const now=performance.now();$('age').textContent=processingSince!==null?`Processing for ${((now-processingSince.started)/1000).toFixed(1)} s`:lastCompleted!==null?`Last result ${((now-lastCompleted)/1000).toFixed(1)} s ago`:'';},500);
export const rawSnapshot=()=>latest?{data:new Float32Array(latest.data),width:latest.width,height:latest.height}:null;
export const diagnostics=()=>({ready,frameNumber,measurement:latest?.measurement??null,rawShape:latest?[latest.height,latest.width]:null});
