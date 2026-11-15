import test from 'node:test';
import assert from 'node:assert/strict';
import { FrameScheduler, centerSquareCrop, outputToSource, sourceToOutput } from '../camera.mjs';

const deferred = () => {
  let resolve, reject;
  const promise = new Promise((a, b) => { resolve = a; reject = b; });
  return { promise, resolve, reject };
};
async function flush() { for (let i = 0; i < 12; i += 1) await Promise.resolve(); }

class Clock {
  constructor() { this.time = 0; this.next = 1; this.timers = new Map(); this.rafs = new Map(); }
  setTimeout = (fn, delay) => { const id = this.next++; this.timers.set(id, { at: this.time + delay, fn }); return id; };
  clearTimeout = (id) => this.timers.delete(id);
  requestAnimationFrame = (fn) => { const id = this.next++; this.rafs.set(id, fn); return id; };
  cancelAnimationFrame = (id) => this.rafs.delete(id);
  advance(ms) {
    this.time += ms;
    for (;;) {
      const due = [...this.timers].filter(([, timer]) => timer.at <= this.time).sort((a, b) => a[1].at - b[1].at);
      if (!due.length) break;
      const [id, timer] = due[0]; this.timers.delete(id); timer.fn();
    }
  }
  animation() { const callbacks = [...this.rafs.values()]; this.rafs.clear(); callbacks.forEach((fn) => fn(this.time)); }
}

class Track extends EventTarget {
  constructor(settings = {}) { super(); this.stopCount = 0; this.readyState = 'live'; this.settings = { width: 640, height: 480, frameRate: 15, facingMode: 'user', deviceId: 'not-exposed', ...settings }; }
  stop() { this.stopCount += 1; this.readyState = 'ended'; }
  getSettings() { return this.settings; }
  end() { this.readyState = 'ended'; this.dispatchEvent(new Event('ended')); }
}

class Video extends EventTarget {
  constructor() {
    super(); this.videoWidth = 640; this.videoHeight = 480; this.readyState = 2;
    this.currentTime = 0; this.srcObject = null; this.paused = true;
    this.callbacks = new Map(); this.next = 1; this.listeners = new Map();
  }
  addEventListener(type, fn, options) { super.addEventListener(type, fn, options); if (!this.listeners.has(type)) this.listeners.set(type, new Set()); this.listeners.get(type).add(fn); }
  removeEventListener(type, fn, options) { super.removeEventListener(type, fn, options); this.listeners.get(type)?.delete(fn); }
  setAttribute() {}
  play() { this.paused = false; return this.playFailure ? Promise.reject(this.playFailure) : Promise.resolve(); }
  pause() { this.paused = true; }
  requestVideoFrameCallback(fn) { const id = this.next++; this.callbacks.set(id, fn); return id; }
  cancelVideoFrameCallback(id) { this.callbacks.delete(id); }
  frame(time, extra = {}) {
    this.currentTime = time;
    const callbacks = [...this.callbacks.values()]; this.callbacks.clear();
    callbacks.forEach((fn) => fn(time * 1000, { mediaTime: time, presentedFrames: Math.round(time * 15) + 1, ...extra }));
  }
}

function setup(options = {}) {
  const clock = new Clock();
  const video = new Video();
  const track = new Track();
  const stream = { getTracks: () => [track], getVideoTracks: () => [track] };
  const context = { transforms: [], draws: [], setTransform(...args) { this.transforms.push(args); }, drawImage(...args) { this.draws.push(args); } };
  const canvas = { width: 0, height: 0, getContext: () => context };
  const lifecycle = new EventTarget();
  const document = new EventTarget(); document.hidden = false;
  const errors = [], packets = [], constraints = [], states = [];
  const scheduler = new FrameScheduler({
    video, onFrame: (packet) => { packets.push(packet); }, onError: (e) => errors.push(e), onStateChange: (s) => states.push(s),
    size: 294, maxFps: 10, startTimeoutMs: 100, frameTimeoutMs: 200,
    getUserMedia: async (c) => { constraints.push(c); return stream; },
    createCanvas: () => canvas, requestAnimationFrame: clock.requestAnimationFrame, cancelAnimationFrame: clock.cancelAnimationFrame,
    setTimeout: clock.setTimeout, clearTimeout: clock.clearTimeout, now: () => clock.time,
    lifecycleTarget: lifecycle, document, ...options,
  });
  return { scheduler, clock, video, track, stream, context, canvas, lifecycle, document, errors, packets, constraints, states };
}

test('square crop preserves aspect, portrait orientation and exact pixel-center mapping', () => {
  const landscape = centerSquareCrop(1280, 720, 294);
  assert.deepEqual([landscape.sx, landscape.sy, landscape.sw, landscape.sh], [280, 0, 720, 720]);
  assert.equal(landscape.inferenceMirrored, false);
  const portrait = centerSquareCrop(721, 1280, 128);
  assert.deepEqual([portrait.sx, portrait.sy, portrait.sw, portrait.sh], [0, 279, 721, 721]);
  for (const crop of [landscape, portrait]) for (const p of [[0, 0], [30.25, 17.5], [crop.width-1, crop.height-1]]) {
    const q = sourceToOutput(...outputToSource(...p, crop), crop);
    assert.ok(Math.abs(q[0]-p[0]) < 1e-10 && Math.abs(q[1]-p[1]) < 1e-10);
  }
  assert.throws(() => centerSquareCrop(0, 720), /width/);
});

test('constructor does not access camera; start requests video only and unmirrored294 snapshot', async () => {
  const s = setup();
  assert.equal(s.constraints.length, 0);
  const info = await s.scheduler.start({ facingMode: 'user' });
  assert.equal(s.constraints[0].audio, false);
  assert.deepEqual(s.constraints[0].video.facingMode, { ideal: 'user' });
  assert.equal(info.displayMirror, true);
  assert.equal(info.size, 294);
  assert.equal('deviceId' in info.settings, false);
  assert.equal(s.video.playsInline, true);
  assert.equal(s.video.muted, true);
  s.video.frame(0); await s.scheduler.whenIdle();
  assert.equal(s.packets.length, 1);
  assert.equal(s.packets[0].canvas.width, 294);
  assert.equal(s.packets[0].timestampS, 0);
  assert.deepEqual(s.context.transforms[0], [1, 0, 0, 1, 0, 0]);
  assert.deepEqual(s.context.draws[0].slice(1), [80, 0, 480, 480, 0, 0, 294, 294]);
  assert.equal(s.packets[0].metadata.inferenceMirrored, false);
  s.scheduler.stop();
  assert.equal(s.track.stopCount, 1);
  assert.equal(s.video.srcObject, null);
  assert.equal(s.video.callbacks.size, 0);
  assert.equal(s.clock.timers.size, 0);
});

test('backpressure never overlaps or rewrites the borrowed canvas; next work uses latest frame', async () => {
  const pending = deferred(); let active = 0, maximum = 0; const stamps = [];
  const s = setup({ onFrame: async (p) => { active += 1; maximum = Math.max(active, maximum); stamps.push(p.timestampS); await pending.promise; active -= 1; } });
  await s.scheduler.start();
  s.video.frame(0); await flush();
  s.video.frame(.1); s.video.frame(.2); await flush();
  assert.equal(s.scheduler.inFlight, true);
  assert.equal(maximum, 1);
  assert.equal(s.context.draws.length, 1);
  assert.equal(s.scheduler.stats.droppedBusy, 2);
  pending.resolve(); await s.scheduler.whenIdle();
  s.video.frame(.3); await s.scheduler.whenIdle();
  assert.deepEqual(stamps, [0, .3]);
  assert.equal(maximum, 1);
  s.scheduler.stop();
});

test('stop aborts work immediately and blocks restart until nonabortable inference settles', async () => {
  const pending = deferred(); let packet;
  const s = setup({ onFrame: (p) => { packet = p; return pending.promise; } });
  await s.scheduler.start(); s.video.frame(0); await flush();
  s.scheduler.stop();
  assert.equal(s.track.stopCount, 1);
  assert.equal(packet.signal.aborted, true);
  assert.equal(packet.isCurrent(), false);
  assert.equal(s.video.srcObject, null);
  await assert.rejects(s.scheduler.start(), { name: 'CameraBusyError' });
  pending.resolve(); await s.scheduler.whenIdle();
  assert.equal(s.scheduler.inFlight, false);
  assert.equal(s.scheduler.stats.completed, 0);
  assert.deepEqual(s.states.at(-1), { state: 'stopped', reason: 'inference-settled', inFlight: false });
});

test('processing timeout stops tracks once, reports error and keeps old-job lock', async () => {
  const pending = deferred();
  const s = setup({ onFrame: () => pending.promise });
  await s.scheduler.start(); s.video.frame(0); await flush();
  s.clock.advance(200); await flush();
  assert.equal(s.scheduler.running, false);
  assert.equal(s.track.stopCount, 1);
  assert.equal(s.errors.length, 1);
  assert.equal(s.errors[0].name, 'CameraFrameTimeoutError');
  await assert.rejects(s.scheduler.start(), { name: 'CameraBusyError' });
  pending.reject(new Error('late backend failure')); await s.scheduler.whenIdle();
  assert.equal(s.errors.length, 1);
  assert.equal(s.scheduler.inFlight, false);
});

test('permission timeout cannot leave a late-granted stream running', async () => {
  const permission = deferred();
  const s = setup({ getUserMedia: () => permission.promise });
  const started = s.scheduler.start();
  const rejection = assert.rejects(started, { name: 'CameraStartTimeoutError' });
  await flush(); s.clock.advance(100); await rejection;
  assert.equal(s.scheduler.state, 'stopped');
  permission.resolve(s.stream); await flush();
  assert.equal(s.track.stopCount, 1);
  assert.equal(s.video.srcObject, null);
  assert.equal(s.clock.timers.size, 0);
});

test('stop during permission wait releases a later stream; immediate stop avoids permission call', async () => {
  const permission = deferred();
  const s = setup({ getUserMedia: () => permission.promise });
  const started = s.scheduler.start(); const rejection = assert.rejects(started, { name: 'AbortError' });
  await flush(); s.scheduler.stop(); await rejection;
  permission.resolve(s.stream); await flush();
  assert.equal(s.track.stopCount, 1);
  const second = setup();
  const early = second.scheduler.start(); const cancelled = assert.rejects(early, { name: 'AbortError' });
  second.scheduler.stop(); await cancelled;
  assert.equal(second.constraints.length, 0);
});

test('page-hide during permission wait aborts startup and releases any later grant', async () => {
  for (const event of ['pagehide', 'visibilitychange']) {
    const permission = deferred();
    const s = setup({ getUserMedia: () => permission.promise });
    const started = s.scheduler.start();
    const rejected = assert.rejects(started, { name: 'AbortError' });
    await flush();
    if (event === 'pagehide') s.lifecycle.dispatchEvent(new Event(event));
    else { s.document.hidden = true; s.document.dispatchEvent(new Event(event)); }
    await rejected;
    permission.resolve(s.stream); await flush();
    assert.equal(s.track.stopCount, 1);
    assert.equal(s.video.srcObject, null);
    assert.equal(s.scheduler.state, 'stopped');
    assert.equal(s.clock.timers.size, 0);
  }
});

test('play failure removes readiness listeners and releases granted tracks', async () => {
  const s = setup(); s.video.readyState = 0; s.video.playFailure = new Error('play denied');
  await assert.rejects(s.scheduler.start(), /play denied/);
  assert.equal(s.track.stopCount, 1);
  assert.equal(s.video.srcObject, null);
  assert.equal(s.scheduler.state, 'stopped');
  assert.equal(s.clock.timers.size, 0);
  for (const listeners of s.video.listeners.values()) assert.equal(listeners.size, 0);
});

test('metadata startup timeout releases tracks and all listeners', async () => {
  const s = setup(); s.video.videoWidth = 0; s.video.readyState = 0;
  const started = s.scheduler.start(); const rejected = assert.rejects(started, { name: 'CameraStartTimeoutError' });
  await flush(); s.clock.advance(100); await rejected;
  assert.equal(s.track.stopCount, 1);
  assert.equal(s.video.srcObject, null);
  for (const listeners of s.video.listeners.values()) assert.equal(listeners.size, 0);
});

test('frame processor rejection stops stream; late camera callbacks cannot process more', async () => {
  let calls = 0;
  const s = setup({ onFrame: () => { calls += 1; throw new Error('backend failed'); } });
  await s.scheduler.start(); s.video.frame(0); await s.scheduler.whenIdle();
  assert.equal(s.scheduler.running, false);
  assert.equal(s.track.stopCount, 1);
  assert.equal(s.errors[0].message, 'backend failed');
  s.video.frame(.2); await flush();
  assert.equal(calls, 1);
});

test('RAF fallback ignores duplicate decoded frames and enforces capture rate', async () => {
  const s = setup(); s.video.requestVideoFrameCallback = undefined;
  await s.scheduler.start();
  s.video.currentTime = 0; s.clock.animation(); await s.scheduler.whenIdle();
  s.clock.advance(16); s.clock.animation(); await flush();
  s.video.currentTime = .04; s.clock.animation(); await flush();
  s.video.currentTime = .1; s.clock.animation(); await s.scheduler.whenIdle();
  assert.deepEqual(s.packets.map((p) => p.timestampS), [0, .1]);
  assert.equal(s.scheduler.stats.skippedDuplicate, 1);
  assert.equal(s.scheduler.stats.skippedRate, 1);
  s.scheduler.stop(); assert.equal(s.clock.rafs.size, 0);
});

test('front/back constraints and changed source orientation keep inference crop unmirrored', async () => {
  const s = setup(); s.track.settings.facingMode = 'environment';
  await s.scheduler.start({ facingMode: 'environment', deviceId: 'selected-camera' });
  assert.deepEqual(s.constraints[0].video.deviceId, { exact: 'selected-camera' });
  assert.equal(s.constraints[0].video.facingMode, undefined);
  assert.equal(s.scheduler.displayMirror, false);
  s.video.videoWidth = 480; s.video.videoHeight = 640;
  s.video.frame(0); await s.scheduler.whenIdle();
  assert.deepEqual([s.packets[0].crop.sx, s.packets[0].crop.sy], [0, 80]);
  assert.equal(s.packets[0].crop.rotationAppliedDegrees, 0);
  s.scheduler.stop();
});

test('camera end and page-hide release tracks without depending on UI cleanup', async () => {
  const s = setup(); await s.scheduler.start(); s.track.end();
  assert.equal(s.scheduler.running, false);
  assert.equal(s.track.stopCount, 1);
  assert.equal(s.errors[0].name, 'CameraEndedError');
  const second = setup(); await second.scheduler.start(); second.lifecycle.dispatchEvent(new Event('pagehide'));
  assert.equal(second.track.stopCount, 1);
  assert.equal(second.scheduler.state, 'stopped');
  const third = setup(); await third.scheduler.start(); third.document.hidden = true; third.document.dispatchEvent(new Event('visibilitychange'));
  assert.equal(third.track.stopCount, 1);
});

test('backwards timestamps fail explicitly rather than feeding nonmonotonic tracker times', async () => {
  const s = setup(); await s.scheduler.start();
  s.video.frame(.2); await s.scheduler.whenIdle(); s.video.frame(.1);
  assert.equal(s.errors[0].name, 'CameraTimingError');
  assert.equal(s.track.stopCount, 1);
});
