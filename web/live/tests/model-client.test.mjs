import test from 'node:test';
import assert from 'node:assert/strict';
import { DepthModelClient } from '../model-client.mjs';

class FakeWorker {
  constructor() { this.handlers = new Map(); this.messages = []; this.terminated = false; }
  addEventListener(type, handler) { this.handlers.set(type, handler); }
  postMessage(message, transfer) { this.messages.push({ message, transfer }); }
  terminate() { this.terminated = true; }
  emit(type, payload) { this.handlers.get(type)?.(type === 'message' ? { data: payload } : payload); }
  reply(type, result, id = this.messages.at(-1).message.id) { this.emit('message', { type, id, result }); }
}

const meta = { backend: 'wasm', dtype: 'q8', size: 294 };
const image = () => ({ width: 2, height: 1, data: new Uint8ClampedArray([0, 1, 2, 255, 4, 5, 6, 255]) });
const prediction = () => ({ data: new Float32Array([0.25, 1.75]), width: 2, height: 1, dims: [1, 1, 2], inputShape: [1, 3, 294, 294], preprocessMs: 2, inferenceMs: 8, backend: 'wasm', dtype: 'q8' });

function setup(options = {}) {
  const workers = [];
  const client = new DepthModelClient({ workerFactory: () => { const worker = new FakeWorker(); workers.push(worker); return worker; }, ...options });
  return { client, workers };
}

async function loaded(options = {}) {
  const state = setup(options);
  const promise = state.client.load();
  state.workers[0].reply('loaded', meta);
  await promise;
  return state;
}

test('load propagates progress/backend and reuses an identical ready model', async () => {
  const statuses = [], progress = [];
  const { client, workers } = setup({ onStatus: event => statuses.push(event), onProgress: event => progress.push(event) });
  const promise = client.load({ device: 'wasm', size: 224 });
  const worker = workers[0];
  assert.deepEqual(worker.messages[0].message.options, { device: 'wasm', size: 224 });
  worker.emit('message', { type: 'progress', progress: { file: 'model_quantized.onnx', progress: 50 } });
  worker.emit('message', { type: 'status', status: { state: 'ready', backend: 'wasm', dtype: 'q8' } });
  worker.reply('loaded', { ...meta, size: 224 });
  assert.equal((await promise).dtype, 'q8');
  await client.load({ device: 'wasm', size: 224 });
  assert.equal(worker.messages.length, 1);
  assert.equal(statuses[0].backend, 'wasm');
  assert.equal(progress[0].progress, 50);
  client.dispose();
});

test('predict transfers a private RGBA copy and preserves raw float values', async () => {
  const { client, workers } = await loaded();
  const input = image();
  const promise = client.predict(input);
  const sent = workers[0].messages.at(-1);
  assert.notEqual(sent.message.rgba, input.data.buffer);
  assert.equal(sent.transfer[0], sent.message.rgba);
  assert.deepEqual([...new Uint8Array(sent.message.rgba)], [...input.data]);
  workers[0].reply('prediction', prediction());
  const output = await promise;
  assert.deepEqual([...output.data], [0.25, 1.75]);
  assert.deepEqual(output.inputShape, [1, 3, 294, 294]);
  assert.equal(input.data.byteLength, 8);
  client.dispose();
});

test('rejects overlap without replacing the active promise; ignores stale replies', async () => {
  const { client, workers } = await loaded();
  const first = client.predict(image());
  await assert.rejects(client.predict(image()), /busy/);
  await assert.rejects(client.load(), /busy/);
  workers[0].reply('prediction', prediction(), 999);
  assert.ok(client.pending);
  workers[0].reply('prediction', prediction());
  await first;
  client.dispose();
});

test('load timeout terminates worker and a later explicit load gets a fresh worker', async () => {
  const { client, workers } = setup({ loadTimeoutMs: 15 });
  await assert.rejects(client.load(), /loading timed out/);
  assert.equal(workers[0].terminated, true);
  assert.equal(client.pending, null);
  const retry = client.load();
  workers[0].reply('loaded', { ...meta, backend: 'webgpu' });
  assert.ok(client.pending, 'stale worker cannot resolve retry');
  workers[1].reply('loaded', meta);
  await retry;
  client.dispose();
});

test('inference timeout terminates hung work and requires another load', async () => {
  const { client, workers } = await loaded({ inferenceTimeoutMs: 15 });
  await assert.rejects(client.predict(image()), /inference timed out/);
  assert.equal(workers[0].terminated, true);
  assert.equal(client.pending, null);
  await assert.rejects(client.predict(image()), /Load/);
  client.dispose();
});

test('dispose aborts pending load, clears timer, and prevents reuse', async () => {
  const { client, workers } = setup();
  const promise = client.load();
  client.dispose();
  await assert.rejects(promise, /stopped/);
  assert.equal(client.pending, null);
  assert.equal(workers[0].terminated, true);
  await assert.rejects(client.load(), /disposed/);
  client.dispose();
});

test('stop aborts pending inference and permits explicit reloading', async () => {
  const { client, workers } = await loaded();
  const promise = client.predict(image());
  client.stop();
  await assert.rejects(promise, /stopped/);
  const retry = client.load();
  workers[1].reply('loaded', meta);
  await retry;
  client.dispose();
});

test('stop between worker completion and promise continuation cannot revive a model', async () => {
  const { client, workers } = setup();
  const promise = client.load();
  workers[0].reply('loaded', meta);
  client.stop();
  await assert.rejects(promise, /stopped/);
  assert.equal(client.ready, false);
  client.dispose();
});

test('dispose between worker result and promise continuation does not return a stale frame', async () => {
  const { client, workers } = await loaded();
  const promise = client.predict(image());
  workers[0].reply('prediction', prediction());
  client.dispose();
  await assert.rejects(promise, /stopped/);
  assert.equal(client.ready, false);
});

test('dispose callback cannot reenter load or recursively dispose and leak a worker', async () => {
  let reload;
  let client;
  const state = setup({ onStatus: event => {
    if (event.state === 'disposed') {
      client.dispose();
      reload = client.load();
      reload.catch(() => {});
    }
  } });
  client = state.client;
  const promise = client.load();
  client.dispose();
  await assert.rejects(promise, /stopped/);
  await assert.rejects(reload, /disposed/);
  assert.equal(state.workers.length, 1);
  assert.equal(state.workers[0].terminated, true);
  assert.equal(client.pending, null);
});

for (const kind of ['error', 'messageerror', 'protocol']) {
  test(`${kind} failure rejects pending work and terminates worker`, async () => {
    const { client, workers } = setup();
    const promise = client.load();
    if (kind === 'protocol') workers[0].emit('message', { type: 'error', id: 1, error: 'network unavailable' });
    else workers[0].emit(kind, { message: 'worker module failed' });
    await assert.rejects(promise);
    assert.equal(workers[0].terminated, true);
    assert.equal(client.pending, null);
    client.dispose();
  });
}

test('presentation callback errors cannot strand model promises', async () => {
  const { client, workers } = setup({ onStatus: () => { throw Error('UI bug'); }, onProgress: () => { throw Error('UI bug'); } });
  const promise = client.load();
  workers[0].emit('message', { type: 'status', status: { state: 'loading' } });
  workers[0].emit('message', { type: 'progress', progress: {} });
  workers[0].reply('loaded', meta);
  await promise;
  client.dispose();
});

test('rejects invalid options/images without starting worker work', async () => {
  const { client, workers } = setup();
  await assert.rejects(client.load({ device: 'cuda' }), /device/);
  await assert.rejects(client.load({ size: 256 }), /multiple/);
  assert.equal(workers.length, 0);
  await assert.rejects(client.predict(image()), /Load/);
  const loading = client.load();
  workers[0].reply('loaded', meta);
  await loading;
  await assert.rejects(client.predict({ ...image(), width: 4 }), /RGBA/);
  assert.equal(workers[0].messages.length, 1);
  client.dispose();
});

test('malformed/non-finite depth fails explicitly rather than entering the tree', async () => {
  const { client, workers } = await loaded();
  const promise = client.predict(image());
  workers[0].reply('prediction', { ...prediction(), data: new Float32Array([NaN, 1]) });
  await assert.rejects(promise, /Invalid raw depth/);
  assert.equal(workers[0].terminated, true);
  client.dispose();
});
