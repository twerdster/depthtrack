import assert from 'node:assert/strict';
import test from 'node:test';
import { approximateIntrinsics, calibratedDepthMm, centeredSquareRoi, createClassObservationWorkspace,
  fitInverseDepthCalibration, largestClassObservations, medianRawPatch, resampleDepth } from '../depth.mjs';

const PIXELS = 128 * 128;
const approx = (a,b,tolerance=1e-10) => assert.ok(Math.abs(a-b) <= tolerance, `${a} != ${b}`);
const fit = () => fitInverseDepthCalibration({ nearRaw: 4, farRaw: 1, nearMm: 250, farMm: 1000 });

test('inverse-depth fit exactly recovers both anchors and intermediate inverse metres', () => {
  const c = fit();
  assert.equal(c.valid, true); assert.equal(c.a, 1); assert.equal(c.b, 0);
  assert.equal(c.estimated, true); assert.equal(c.units, 'estimated millimetres');
  assert.equal(calibratedDepthMm(4,c),250); assert.equal(calibratedDepthMm(1,c),1000);
  assert.equal(calibratedDepthMm(2,c),500);
  const shifted=fitInverseDepthCalibration({nearRaw:0,farRaw:-3,nearMm:250,farMm:1000});
  assert.equal(shifted.valid,true); assert.equal(calibratedDepthMm(-2,shifted),500);
  assert.equal(calibratedDepthMm(-5,shifted),0);
});

test('bad/reversed/degenerate anchors return explicit invalidity, not arbitrary depth', () => {
  const good={nearRaw:4,farRaw:1,nearMm:250,farMm:1000};
  for (const bad of [null,{}, {...good,nearRaw:1},{...good,nearRaw:0},{...good,nearMm:1000},
    {...good,farMm:-1},{...good,nearMm:0},{...good,farRaw:NaN},{...good,nearRaw:Infinity},
    {...good,nearRaw:'4'},{...good,nearRaw:1+Number.EPSILON},{...good,nearMm:Number.MIN_VALUE}]) {
    const c=fitInverseDepthCalibration(bad); assert.equal(c.valid,false); assert.ok(c.reason);
    assert.equal(calibratedDepthMm(2,c),0);
  }
});

test('nearest and bilinear resampling fill caller output, inclusive mm gating and zero invalid q', () => {
  const raw=new Float32Array(PIXELS).fill(2);
  raw[0]=4;raw[1]=1;raw[2]=4.01;raw[3]=.99;raw[4]=0;raw[5]=-2;raw[6]=NaN;raw[7]=Infinity;
  for (const method of ['nearest','bilinear']) {
    const out=new Uint16Array(PIXELS).fill(99);
    assert.equal(resampleDepth(raw,128,128,fit(),out,{method}),out);
    assert.deepEqual(Array.from(out.subarray(0,9)),[250,1000,0,0,0,0,0,0,500]);
    assert.equal(out[100],500);
    const c=fitInverseDepthCalibration({nearRaw:4,farRaw:1,nearMm:300,farMm:600});
    assert.equal(resampleDepth(new Float32Array(PIXELS).fill(4),128,128,c,out)[0],300);
    assert.equal(resampleDepth(new Float32Array(PIXELS).fill(1),128,128,c,out)[0],600);
    out.fill(99);resampleDepth(raw,128,128,{valid:false},out);assert.ok(out.every(x=>x===0));
  }
});

test('bilinear interpolation happens in raw inverse-depth, not already inverted depth', () => {
  const c=fit();const raw=new Float32Array([1,3,1,3]);const out=new Uint16Array(PIXELS);
  resampleDepth(raw,2,2,c,out,{method:'bilinear'});
  // Output col63 samples source x=.4921875 -> raw1.984375 ->503.937mm.
  assert.equal(out[63],504);
  resampleDepth(raw,2,2,c,out,{method:'nearest'});assert.equal(out[63],1000);assert.equal(out[64],333);
  const invalid=new Float32Array([1,NaN,1,3]);resampleDepth(invalid,2,2,c,out,{method:'bilinear'});
  assert.equal(out[63],0); // Never interpolate through an invalid positive-weight neighbour.
});

test('resampling uses an explicit centered square ROI and consistent pixel centers', () => {
  assert.deepEqual(centeredSquareRoi(256,128),{x:64,y:0,side:128});
  assert.deepEqual(centeredSquareRoi(256,128,.5),{x:96,y:32,side:64});
  const raw=new Float32Array(256*128);
  for(let row=0;row<128;++row)for(let col=0;col<256;++col)raw[row*256+col]=1+col/256;
  const c=fitInverseDepthCalibration({nearRaw:3,farRaw:1,nearMm:100,farMm:1000});
  const out=new Uint16Array(PIXELS);resampleDepth(raw,256,128,c,out,{method:'nearest'});
  assert.equal(out[0],Math.round(calibratedDepthMm(raw[64],c)));
  assert.equal(out[127],Math.round(calibratedDepthMm(raw[191],c)));
  resampleDepth(raw,256,128,c,out,{method:'nearest',roiFraction:.5});
  assert.equal(out[0],Math.round(calibratedDepthMm(raw[96],c)));
  assert.equal(out[127],Math.round(calibratedDepthMm(raw[159],c)));
});

test('resample input/option failures are atomic and non-uint16 output is rejected', () => {
  const out=new Uint16Array(PIXELS).fill(92);const raw=new Float32Array(PIXELS).fill(2);
  assert.throws(()=>resampleDepth(raw,127,128,fit(),out),RangeError);
  assert.throws(()=>resampleDepth(raw,128,128,fit(),new Uint8Array(PIXELS)),TypeError);
  for (const options of [{method:'cubic'},{roiFraction:0},{roiFraction:2},{nearMm:0},{nearMm:1000,farMm:100}]) {
    assert.throws(()=>resampleDepth(raw,128,128,fit(),out,options),RangeError);
  }
  const aliased=new Uint16Array(raw.buffer,0,PIXELS);assert.throws(()=>resampleDepth(raw,128,128,fit(),aliased),RangeError);
  assert.ok(out.every(x=>x===92));
  const huge=fitInverseDepthCalibration({nearRaw:2,farRaw:1,nearMm:70000,farMm:100000});
  resampleDepth(new Float32Array(PIXELS).fill(2),128,128,huge,out);assert.ok(out.every(x=>x===0));
});

test('selectable calibration patch median ignores nonfinite outliers and preserves input', () => {
  const raw=new Float32Array(16).fill(2);raw[0]=NaN;raw[1]=1000;raw[2]=-1000;raw[3]=Infinity;
  const before=raw.slice();const whole=medianRawPatch(raw,4,4,{fraction:1});
  assert.equal(whole.valid,true);assert.equal(whole.median,2);assert.equal(whole.count,14);
  assert.deepEqual(raw,before);
  const atCorner=medianRawPatch(new Float32Array([1,2,3,4]),2,2,{centerX:1,centerY:1,fraction:.1});
  assert.equal(atCorner.median,4);assert.deepEqual(atCorner.patch,{x:1,y:1,width:1,height:1});
  const empty=medianRawPatch(new Float32Array([NaN]),1,1);assert.equal(empty.valid,false);assert.equal(empty.median,null);
  const tooFew=medianRawPatch(new Float32Array([3]),1,1,{minSamples:2});assert.equal(tooFew.valid,false);
  const even=medianRawPatch(new Float32Array([1,3,5,7]),2,2,{fraction:1});assert.equal(even.median,4);
  assert.throws(()=>medianRawPatch(raw,4,4,{centerX:2}),RangeError);
});

test('largest components retain semantic class and ignore tiny disconnected/invalid labels', () => {
  const labels=new Uint8Array(PIXELS).fill(7);const depth=new Uint16Array(PIXELS).fill(500);
  const put=(r,c,label)=>{labels[r*128+c]=label;};
  for(let r=10;r<12;++r)for(let c=20;c<23;++c)put(r,c,0); // six-pixel accepted thumb
  put(30,30,0);put(40,40,0); // unrelated singletons must not shift it
  put(50,50,1);put(51,51,1); // diagonal-only singletons, not one two-pixel component
  put(60,60,2);depth[60*128+60]=0;
  put(70,70,255);put(71,71,5);put(72,72,6);
  const workspace=createClassObservationWorkspace();
  const result=largestClassObservations(labels,depth,{minArea:3,intrinsics:{fx:100,fy:100,cx:63.5,cy:63.5},workspace});
  assert.deepEqual(Array.from(result.counts),[6,1,0,0,0]);assert.deepEqual(Array.from(result.totalCounts),[8,2,0,0,0]);
  assert.deepEqual(result.centroids[0],[21,10.5]);assert.deepEqual(result.observations[0],[-212.5,-265,500]);
  assert.deepEqual(result.observations.slice(1),[null,null,null,null]);
  assert.equal(result.estimated,true);assert.equal(result.units,'estimated millimetres');
  labels.fill(255);const next=largestClassObservations(labels,depth,{workspace});
  assert.deepEqual(next.observations,[null,null,null,null,null]);assert.ok(next.counts.every(x=>x===0));
});

test('approximate FOV intrinsics give symmetric camera XYZ for symmetric image blobs', () => {
  const labels=new Uint8Array(PIXELS).fill(7);const depth=new Uint16Array(PIXELS).fill(500);
  for(let r=63;r<=64;++r)for(let c=31;c<=32;++c)labels[r*128+c]=0;
  for(let r=63;r<=64;++r)for(let c=95;c<=96;++c)labels[r*128+c]=1;
  const camera=approximateIntrinsics(90);approx(camera.fx,64);approx(camera.fy,64);
  const result=largestClassObservations(labels,depth,{horizontalFovDeg:90,minArea:4});
  approx(result.observations[0][0],-250);approx(result.observations[1][0],250);
  approx(result.observations[0][1],0);approx(result.observations[1][1],0);
  assert.equal(result.observations[0][2],500);assert.equal(result.intrinsics.source,'approximate_horizontal_fov');
});

test('3D output is mean backprojected XYZ, not pixel centroid times mean depth', () => {
  const labels=new Uint8Array(PIXELS).fill(255);const depth=new Uint16Array(PIXELS);
  labels[0]=0;labels[1]=0;depth[0]=100;depth[1]=300;
  const result=largestClassObservations(labels,depth,{minArea:2,intrinsics:{fx:1,fy:1,cx:0,cy:0}});
  assert.deepEqual(result.centroids[0],[.5,0]);assert.deepEqual(result.observations[0],[150,0,200]);
});

test('component validation rejects invalid intrinsics/workspaces without touching input', () => {
  const labels=new Uint8Array(PIXELS).fill(7);const depth=new Uint16Array(PIXELS).fill(500);
  for(const options of [{minArea:0},{minArea:1.5},{horizontalFovDeg:180},{horizontalFovDeg:NaN},
    {intrinsics:{fx:0,fy:1,cx:0,cy:0}},{intrinsics:{fx:1,fy:1,cx:NaN,cy:0}},
    {workspace:{visited:labels,queue:new Int32Array(PIXELS)}}]) {
    assert.throws(()=>largestClassObservations(labels,depth,options),RangeError);
  }
  assert.ok(labels.every(x=>x===7));assert.ok(depth.every(x=>x===500));
});


test('full-frame component uses bounded iterative workspace and deterministic equal-area ties', () => {
  const labels=new Uint8Array(PIXELS);const depth=new Uint16Array(PIXELS).fill(500);
  const workspace=createClassObservationWorkspace();
  const full=largestClassObservations(labels,depth,{minArea:8,workspace});
  assert.equal(full.counts[0],PIXELS);assert.equal(full.totalCounts[0],PIXELS);
  approx(full.observations[0][0],0,1e-9);approx(full.observations[0][1],0,1e-9);
  assert.equal(full.observations[0][2],500);
  labels.fill(7);labels[0]=0;labels[1]=0;labels[100]=0;labels[101]=0;
  const tie=largestClassObservations(labels,depth,{minArea:2,workspace});
  assert.equal(tie.counts[0],2);assert.equal(tie.totalCounts[0],4);assert.deepEqual(tie.centroids[0],[.5,0]);
});
