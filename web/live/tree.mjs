/** Packed seven-class tree inference for prepared 128×128 uint16 depth in mm.
 *
 * Matches backends/reference.py and the reviewed CPU/Metal/Mojo integer profile.
 * This is the complete ten-split prefix, not the original full-depth detector.
 * Zero centre depth is background 7; leaf 255 remains unknown/no positive vote.
 * Constructor allocations are bounded by the model; predict allocates nothing.
 */
export const TREE_WIDTH = 128;
export const TREE_HEIGHT = 128;
export const TREE_PIXELS = TREE_WIDTH * TREE_HEIGHT;

function byteView(value, name) {
  if (value instanceof ArrayBuffer) return new Uint8Array(value);
  if (ArrayBuffer.isView(value)) {
    return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  }
  throw new TypeError(`${name} must be an ArrayBuffer or typed-array view`);
}

function overlaps(a, b) {
  return a.buffer === b.buffer && a.byteOffset < b.byteOffset + b.byteLength &&
    b.byteOffset < a.byteOffset + a.byteLength;
}

export class PackedDepthTree {
  #nodes;
  #leaves;
  #levels;
  #firstLeaf;

  /** Binary records are little endian: four int16 offsets, int16 cut, uint16 flags. */
  constructor(nodeBytes, leafBytes, { levels = 10 } = {}) {
    if (!Number.isInteger(levels) || levels < 1 || levels > 10) {
      throw new RangeError('Tree levels must be an integer in 1..10');
    }
    const nodes = byteView(nodeBytes, 'nodes');
    const leaves = byteView(leafBytes, 'leaves');
    const count = 2 ** levels - 1;
    if (nodes.byteLength !== count * 12 || leaves.byteLength !== 2 ** levels) {
      throw new RangeError('Node/leaf byte counts do not describe the requested complete tree');
    }
    const source = new DataView(nodes.buffer, nodes.byteOffset, nodes.byteLength);
    this.#nodes = new Int16Array(count * 6);
    for (let i = 0; i < count; ++i) {
      const flags = source.getUint16(i * 12 + 10, true);
      if (!(flags & 32) || (flags & 0xffc0) || (flags & 24) === 24) {
        throw new RangeError(`Invalid packed node flags at node ${i}`);
      }
      for (let field = 0; field < 5; ++field) {
        this.#nodes[i * 6 + field] = source.getInt16(i * 12 + field * 2, true);
      }
      this.#nodes[i * 6 + 5] = flags;
    }
    this.#leaves = new Uint8Array(leaves); // Own a stable copy of uploaded model bytes.
    this.#levels = levels;
    this.#firstLeaf = count;
    this.labels = new Uint8Array(TREE_PIXELS);
  }

  get width() { return TREE_WIDTH; }
  get height() { return TREE_HEIGHT; }
  get levels() { return this.#levels; }

  /** Fill caller buffers, or reuse this.labels. Optional heaps are complete heap indices.
   *
   * depth: Uint16Array(16384), row-major mm. labels: Uint8Array(16384).
   * heapIds: optional Uint16Array(16384), with 65535 for zero centres.
   * Input/output buffers must not overlap. Returned labels are reused next call.
   */
  predict(depth, labels = this.labels, heapIds = null) {
    if (!(depth instanceof Uint16Array) || depth.length !== TREE_PIXELS) {
      throw new TypeError('Depth must be Uint16Array(16384), in millimetres');
    }
    if (!(labels instanceof Uint8Array) || labels.length !== TREE_PIXELS) {
      throw new TypeError('Labels must be Uint8Array(16384)');
    }
    if (heapIds !== null && (!(heapIds instanceof Uint16Array) || heapIds.length !== TREE_PIXELS)) {
      throw new TypeError('Heap output must be Uint16Array(16384) or null');
    }
    if (overlaps(depth, labels) || (heapIds !== null &&
        (overlaps(depth, heapIds) || overlaps(labels, heapIds)))) {
      throw new RangeError('Depth, label and heap output storage must not overlap');
    }
    const nodes = this.#nodes;
    const leaves = this.#leaves;
    const firstLeaf = this.#firstLeaf;
    const levels = this.#levels;
    let index = 0;
    for (let row = 0; row < TREE_HEIGHT; ++row) {
      for (let col = 0; col < TREE_WIDTH; ++col, ++index) {
        const z = depth[index];
        if (z === 0) {
          labels[index] = 7;
          if (heapIds !== null) heapIds[index] = 65535;
          continue;
        }
        const rowZ = row * z;
        const colZ = col * z;
        let heap = 0;
        for (let level = 0; level < levels; ++level) {
          const base = heap * 6;
          // Cast AFTER adding the normalized offset. Numerators are exact integers
          // well inside Number's 53-bit range. Math.trunc handles negative fractions.
          const r1 = Math.trunc((rowZ + nodes[base]) / z);
          const c1 = Math.trunc((colZ + nodes[base + 1]) / z);
          const r2 = Math.trunc((rowZ + nodes[base + 2]) / z);
          const c2 = Math.trunc((colZ + nodes[base + 3]) / z);
          const a = r1 >= 0 && r1 < TREE_HEIGHT && c1 >= 0 && c1 < TREE_WIDTH ? depth[r1 * TREE_WIDTH + c1] : 0;
          const b = r2 >= 0 && r2 < TREE_HEIGHT && c2 >= 0 && c2 < TREE_WIDTH ? depth[r2 * TREE_WIDTH + c2] : 0;
          const flags = nodes[base + 5];
          let right;
          if (a === 0 && b === 0) right = (flags & 1) !== 0;
          else if (a === 0) right = (flags & 2) !== 0;
          else if (b === 0) right = (flags & 4) !== 0;
          else if (flags & 8) right = false;
          else if (flags & 16) right = true;
          else right = a - b > nodes[base + 4];
          heap = 2 * heap + 1 + (right ? 1 : 0);
        }
        labels[index] = leaves[heap - firstLeaf];
        if (heapIds !== null) heapIds[index] = heap;
      }
    }
    return labels;
  }
}
