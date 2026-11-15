import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { performance } from 'node:perf_hooks';
import test from 'node:test';
import { PackedDepthTree, TREE_PIXELS } from '../tree.mjs';

const backends = new URL('../../../backends/', import.meta.url);
const fixtures = JSON.parse(readFileSync(new URL('fixtures/manifest.json', backends), 'utf8'));
const nodes = readFileSync(new URL('model/depth10/nodes.bin', backends));
const leaves = readFileSync(new URL('model/depth10/leaves.bin', backends));
function u16LE(bytes) {
  const out = new Uint16Array(bytes.byteLength / 2);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let i = 0; i < out.length; ++i) out[i] = view.getUint16(i * 2, true);
  return out;
}
function model(fields = [0, 0, 0, 0, 0, 32], labels = [5, 6]) {
  const bytes = new ArrayBuffer(12);
  const view = new DataView(bytes);
  fields.forEach((v, i) => view.setInt16(i * 2, v, true));
  return new PackedDepthTree(bytes, new Uint8Array(labels), { levels: 1 });
}

for (const name of fixtures.order) {
  test(`exact labels and full heap paths: ${name}`, () => {
    const depth = u16LE(readFileSync(new URL(`fixtures/${name}/depth.bin`, backends)));
    const expected = readFileSync(new URL(`fixtures/${name}/depth10_labels.bin`, backends));
    const expectedHeaps = u16LE(readFileSync(new URL(`fixtures/${name}/depth10_leafids.bin`, backends)));
    const tree = new PackedDepthTree(nodes, leaves);
    const labels = new Uint8Array(TREE_PIXELS);
    const heaps = new Uint16Array(TREE_PIXELS);
    const start = performance.now();
    assert.equal(tree.predict(depth, labels, heaps), labels);
    const elapsed = performance.now() - start;
    assert.deepEqual(labels, new Uint8Array(expected), 'class labels differ');
    assert.deepEqual(heaps, expectedHeaps, 'reached heap indices differ');
    assert.deepEqual(tree.predict(depth), labels, 'default reusable output differs');
    assert.equal(tree.predict(depth), tree.labels, 'default output must be reused');
    assert.equal(tree.levels, 10);
    console.log(`tree fixture ${name}: ${TREE_PIXELS} pixels, ${elapsed.toFixed(3)} ms first validation call (not a controlled benchmark)`);
  });
}

test('binary ArrayBuffers and offset views decode identically, with owned model copies', () => {
  const paddedNodes = new Uint8Array(nodes.length + 17);
  const paddedLeaves = new Uint8Array(leaves.length + 11);
  paddedNodes.set(nodes, 7); paddedLeaves.set(leaves, 3);
  const tree = new PackedDepthTree(new DataView(paddedNodes.buffer, 7, nodes.length), paddedLeaves.subarray(3, 3 + leaves.length));
  const direct = new PackedDepthTree(nodes.buffer.slice(nodes.byteOffset, nodes.byteOffset + nodes.length), leaves.buffer.slice(leaves.byteOffset, leaves.byteOffset + leaves.length));
  const depth = new Uint16Array(TREE_PIXELS).fill(1000);
  paddedNodes.fill(0); paddedLeaves.fill(255);
  assert.deepEqual(tree.predict(depth), direct.predict(depth));
});

test('full-sum coordinate truncation crosses a negative fractional offset correctly', () => {
  const tree = model([-1, 0, 0, 0, 0, 32]);
  const depth = new Uint16Array(TREE_PIXELS);
  depth[10 * 128] = 1000;
  depth[9 * 128] = 1100;
  assert.equal(tree.predict(depth)[10 * 128], 6, 'trunc(10 - 1/1000) must sample row9, not row10');
  const atEdge = model([-1, 0, 0, 0, 0, 34]);
  depth.fill(0); depth[0] = 1000;
  assert.equal(atEdge.predict(depth)[0], 5, 'trunc(-1/1000) is zero, not OOB row-1');
});

test('missing and OOB neighbor branches preserve flags and strict cut equality', () => {
  const depth = new Uint16Array(TREE_PIXELS).fill(1000);
  // Negative32768 offset at z1 is truly OOB; center remains valid.
  depth[0] = 1;
  assert.equal(model([-32768, 0, -32768, 0, 0, 33]).predict(depth)[0], 6, 'both missing right');
  assert.equal(model([-32768, 0, 0, 0, 0, 34]).predict(depth)[0], 6, 'first missing right');
  assert.equal(model([0, 0, -32768, 0, 0, 36]).predict(depth)[0], 6, 'second missing right');
  assert.equal(model([0, 0, 0, 0, 0, 32]).predict(depth)[0], 5, 'a-b equals cut goes left');
  assert.equal(model([0, 0, 0, 0, -1, 32]).predict(depth)[0], 6, 'signed negative cut');
  assert.equal(model([0, 0, 0, 0, -1, 40]).predict(depth)[0], 5, 'normalAlwaysLeft overrides cut');
  assert.equal(model([0, 0, 0, 0, 1, 48]).predict(depth)[0], 6, 'normalAlwaysRight overrides cut');
});

test('zero-center7 and unknown255 are distinct, including uint16 maximum depths', () => {
  const tree = model([0, 0, 0, 0, 0, 32], [255, 6]);
  const depth = new Uint16Array(TREE_PIXELS);
  depth[0] = 1; depth[TREE_PIXELS - 1] = 65535;
  const heaps = new Uint16Array(TREE_PIXELS);
  const labels = tree.predict(depth, undefined, heaps);
  assert.equal(labels[0], 255); assert.equal(labels[TREE_PIXELS - 1], 255);
  assert.equal(labels[1], 7); assert.equal(heaps[1], 65535);
  assert.equal(heaps[0], 1); assert.equal(heaps[TREE_PIXELS - 1], 1);
  depth.fill(0); tree.predict(depth, labels, heaps);
  assert.ok(labels.every(x => x === 7), 'reused output must be completely overwritten');
  assert.ok(heaps.every(x => x === 65535));
});

test('invalid model dimensions, flags and prediction buffers fail before writing output', () => {
  assert.throws(() => new PackedDepthTree(null, leaves), TypeError);
  for (const levels of [0, 11, 1.5, NaN]) assert.throws(() => new PackedDepthTree(nodes, leaves, { levels }), RangeError);
  assert.throws(() => new PackedDepthTree(nodes.subarray(1), leaves), RangeError);
  assert.throws(() => new PackedDepthTree(nodes, leaves.subarray(1)), RangeError);
  for (const flags of [0, 56, 96, 0x8020]) assert.throws(() => model([0, 0, 0, 0, 0, flags]), RangeError);
  const tree = model();
  const depth = new Uint16Array(TREE_PIXELS);
  const output = new Uint8Array(TREE_PIXELS).fill(91);
  assert.throws(() => tree.predict(new Uint8Array(TREE_PIXELS), output), TypeError);
  assert.throws(() => tree.predict(depth.subarray(1), output), TypeError);
  assert.throws(() => tree.predict(depth, new Uint8Array(1)), TypeError);
  assert.throws(() => tree.predict(depth, output, new Uint16Array(1)), TypeError);
  assert.throws(() => tree.predict(depth, new Uint8Array(depth.buffer, 0, TREE_PIXELS)), RangeError);
  assert.throws(() => tree.predict(depth, output, depth), RangeError);
  const shared = new ArrayBuffer(TREE_PIXELS * 3);
  assert.throws(() => tree.predict(depth, new Uint8Array(shared, 0, TREE_PIXELS), new Uint16Array(shared, 0, TREE_PIXELS)), RangeError);
  assert.ok(output.every(x => x === 91));
});

test('deployed tree assets are byte-identical to the checked backend model', () => {
  assert.deepEqual(readFileSync(new URL('../model/nodes.bin', import.meta.url)), nodes);
  assert.deepEqual(readFileSync(new URL('../model/leaves.bin', import.meta.url)), leaves);
  assert.deepEqual(readFileSync(new URL('../model/model.json', import.meta.url)), readFileSync(new URL('model/depth10/model.json', backends)));
});
