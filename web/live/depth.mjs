/** DOM-free helpers for a monocular-depth prototype, not calibrated sensor depth.
 * Raw model values are affine relative inverse-depth: larger means closer.
 * All reported mm/XYZ values remain estimates, even after two user anchors.
 */
const WIDTH = 128;
const HEIGHT = 128;
const PIXELS = WIDTH * HEIGHT;
const UNITS = 'estimated millimetres';

function dimensions(raw, width, height) {
  if (!Number.isSafeInteger(width) || !Number.isSafeInteger(height) || width < 1 || height < 1 ||
      !Number.isSafeInteger(width * height) || raw == null || raw.length !== width * height) {
    throw new RangeError('Raw tensor must have exactly width×height numeric samples');
  }
}
function finiteNumber(value) { return typeof value === 'number' && Number.isFinite(value); }
function overlaps(a, b) {
  return ArrayBuffer.isView(a) && ArrayBuffer.isView(b) && a.buffer === b.buffer &&
    a.byteOffset < b.byteOffset + b.byteLength && b.byteOffset < a.byteOffset + a.byteLength;
}
function validCalibration(calibration) {
  return calibration?.valid === true && finiteNumber(calibration.a) && calibration.a > 0 && finiteNumber(calibration.b);
}

/** Fit q=a*raw+b (q in inverse metres), then estimated z_mm=1000/q.
 * Raw anchors may be negative or zero because the network scale/offset are relative.
 * Reject unidentifiable/reversed anchors; do not silently invert the depth order.
 */
export function fitInverseDepthCalibration(anchors) {
  const invalid = reason => ({ valid: false, estimated: true, units: UNITS, reason });
  if (anchors === null || typeof anchors !== 'object') return invalid('Two finite raw/mm anchors are required');
  const { nearRaw, farRaw, nearMm, farMm } = anchors;
  if (![nearRaw, farRaw, nearMm, farMm].every(finiteNumber)) return invalid('Anchors must be finite numbers');
  if (!(nearRaw > farRaw)) return invalid('Near raw value must exceed far raw value');
  if (!(nearMm > 0 && nearMm < farMm)) return invalid('Require 0 < near millimetres < far millimetres');
  const separation = nearRaw - farRaw;
  if (!Number.isFinite(separation) || separation <= 32 * Number.EPSILON * Math.max(1, Math.abs(nearRaw), Math.abs(farRaw))) {
    return invalid('Raw anchors are numerically degenerate');
  }
  const nearQ = 1000 / nearMm;
  const farQ = 1000 / farMm;
  const a = (nearQ - farQ) / separation;
  const b = farQ - a * farRaw;
  if (!(finiteNumber(a) && a > 0 && finiteNumber(b) && finiteNumber(nearQ) && nearQ > 0 && finiteNumber(farQ) && farQ > 0)) {
    return invalid('Anchor fit overflowed or is numerically degenerate');
  }
  return { valid: true, a, b, nearRaw, farRaw, nearMm, farMm, estimated: true, units: UNITS,
    equation: 'q=a*raw+b; z_mm=1000/q', reason: null };
}

/** Ungated floating-point estimated millimetres; zero means invalid/nonpositive q. */
export function calibratedDepthMm(raw, calibration) {
  if (!finiteNumber(raw) || !validCalibration(calibration)) return 0;
  const q = calibration.a * raw + calibration.b;
  if (!(Number.isFinite(q) && q > 0)) return 0;
  const mm = 1000 / q;
  return Number.isFinite(mm) && mm > 0 ? mm : 0;
}

/** Source pixel-edge coordinates of the explicit centred square crop. */
export function centeredSquareRoi(width, height, fraction = 1) {
  if (!Number.isSafeInteger(width) || !Number.isSafeInteger(height) || width < 1 || height < 1 ||
      !finiteNumber(fraction) || fraction <= 0 || fraction > 1) {
    throw new RangeError('ROI requires positive dimensions and fraction in (0,1]');
  }
  const side = Math.min(width, height) * fraction;
  return { x: (width - side) / 2, y: (height - side) / 2, side };
}

function bilinear(raw, width, height, x, y) {
  const x0 = Math.floor(x); const y0 = Math.floor(y);
  const x1 = Math.min(width - 1, x0 + 1); const y1 = Math.min(height - 1, y0 + 1);
  const dx = x - x0; const dy = y - y0;
  const w00 = (1 - dx) * (1 - dy); const w10 = dx * (1 - dy);
  const w01 = (1 - dx) * dy; const w11 = dx * dy;
  const a = raw[y0 * width + x0]; const b = raw[y0 * width + x1];
  const c = raw[y1 * width + x0]; const d = raw[y1 * width + x1];
  // An invalid positive-weight neighbour makes the sample invalid. Zero-weight
  // neighbours do not poison exact pixel centres with NaN*0.
  if ((w00 > 0 && !finiteNumber(a)) || (w10 > 0 && !finiteNumber(b)) ||
      (w01 > 0 && !finiteNumber(c)) || (w11 > 0 && !finiteNumber(d))) return NaN;
  return (w00 > 0 ? a * w00 : 0) + (w10 > 0 ? b * w10 : 0) +
    (w01 > 0 ? c * w01 : 0) + (w11 > 0 ? d * w11 : 0);
}

/** Fill caller-owned Uint16Array(16384); no new output image is allocated.
 * Samples use pixel-centre mapping through a centred square source ROI. Raw
 * inverse-depth is interpolated before conversion to mm. Bounds are inclusive;
 * invalid q, range rejection, or unencodable mm become zero. Millimetres round
 * to nearest uint16 after gating. Raw sign alone is not a validity condition.
 */
export function resampleDepth(raw, width, height, calibration, output, options = {}) {
  dimensions(raw, width, height);
  if (!(output instanceof Uint16Array) || output.length !== PIXELS) throw new TypeError('Output must be Uint16Array(16384)');
  if (overlaps(raw, output)) throw new RangeError('Raw tensor and output storage must not overlap');
  const method = options.method ?? 'bilinear';
  if (method !== 'bilinear' && method !== 'nearest') throw new RangeError('Method must be bilinear or nearest');
  const roi = centeredSquareRoi(width, height, options.roiFraction ?? 1);
  if (!validCalibration(calibration)) { output.fill(0); return output; }
  const nearMm = options.nearMm ?? calibration.nearMm;
  const farMm = options.farMm ?? calibration.farMm;
  if (!finiteNumber(nearMm) || !finiteNumber(farMm) || nearMm <= 0 || farMm < nearMm) {
    throw new RangeError('Gate requires finite 0 < nearMm <= farMm');
  }
  const a = calibration.a; const b = calibration.b;
  const scale = roi.side / WIDTH;
  let index = 0;
  for (let row = 0; row < HEIGHT; ++row) {
    const y = Math.max(0, Math.min(height - 1, roi.y + (row + 0.5) * scale - 0.5));
    for (let col = 0; col < WIDTH; ++col, ++index) {
      const x = Math.max(0, Math.min(width - 1, roi.x + (col + 0.5) * scale - 0.5));
      const value = method === 'nearest' ? raw[Math.round(y) * width + Math.round(x)] : bilinear(raw, width, height, x, y);
      if (!finiteNumber(value)) { output[index] = 0; continue; }
      const q = a * value + b;
      if (!(Number.isFinite(q) && q > 0)) { output[index] = 0; continue; }
      const mm = 1000 / q;
      // Only absorb floating-point roundoff at inclusive anchor boundaries.
      const tolerance = 64 * Number.EPSILON * Math.max(1, mm, nearMm, farMm);
      if (!Number.isFinite(mm) || mm <= 0 || mm + tolerance < nearMm || mm - tolerance > farMm) {
        output[index] = 0; continue;
      }
      const rounded = Math.round(mm);
      output[index] = rounded >= 1 && rounded <= 65535 ? rounded : 0;
    }
  }
  return output;
}

/** Median of finite raw samples in a selectable square patch; no source mutation.
 * Centre coordinates are normalized [0,1] across the source tensor. fraction is
 * relative to its shorter side. Raw zero/negative values are allowed as above.
 */
export function medianRawPatch(raw, width, height, { centerX = 0.5, centerY = 0.5, fraction = 0.1, minSamples = 1 } = {}) {
  dimensions(raw, width, height);
  if (![centerX, centerY].every(finiteNumber) || centerX < 0 || centerX > 1 || centerY < 0 || centerY > 1 ||
      !finiteNumber(fraction) || fraction <= 0 || fraction > 1 || !Number.isSafeInteger(minSamples) || minSamples < 1) {
    throw new RangeError('Invalid normalized calibration patch or minimum count');
  }
  const side = Math.max(1, Math.round(Math.min(width, height) * fraction));
  const x = Math.max(0, Math.min(width - side, Math.round(centerX * (width - 1) - (side - 1) / 2)));
  const y = Math.max(0, Math.min(height - side, Math.round(centerY * (height - 1) - (side - 1) / 2)));
  const values = [];
  for (let row = y; row < y + side; ++row) for (let col = x; col < x + side; ++col) {
    const value = raw[row * width + col];
    if (finiteNumber(value)) values.push(value);
  }
  const patch = { x, y, width: side, height: side };
  if (values.length < minSamples) return { valid: false, median: null, count: values.length, patch, reason: 'Too few finite calibration samples' };
  values.sort((a, b) => a - b);
  const middle = Math.floor(values.length / 2);
  const median = values.length % 2 ? values[middle] : values[middle - 1] / 2 + values[middle] / 2;
  return { valid: true, median, count: values.length, patch, reason: null };
}

/** Reuse this optional workspace across frames to avoid allocating flood queues. */
export function createClassObservationWorkspace() {
  return { visited: new Uint8Array(PIXELS), queue: new Int32Array(PIXELS) };
}

/** Approximate pinhole intrinsics for the already-cropped 128×128 depth image.
 * Explicit intrinsics override FOV; both routes still label mono depth estimated.
 */
export function approximateIntrinsics(horizontalFovDeg = 60) {
  if (!finiteNumber(horizontalFovDeg) || horizontalFovDeg <= 0 || horizontalFovDeg >= 180) throw new RangeError('Horizontal FOV must be in (0,180) degrees');
  const f = (WIDTH / 2) / Math.tan(horizontalFovDeg * Math.PI / 360);
  return { fx: f, fy: f, cx: (WIDTH - 1) / 2, cy: (HEIGHT - 1) / 2, source: 'approximate_horizontal_fov', horizontalFovDeg };
}

/** Largest four-connected valid-depth component per class0..4, then 3D centroid.
 * This is new cheap blob postprocessing, not historical mean shift or a learned
 * fingertip-coordinate model. counts reports each largest area even if below
 * minArea; totalCounts includes every valid-depth pixel of that class. Ties use
 * first row-major component. No other semantic class can fill a missing finger.
 */
export function largestClassObservations(labels, depth, { horizontalFovDeg = 60, minArea = 8, intrinsics = null, workspace = null } = {}) {
  if (!(labels instanceof Uint8Array) || labels.length !== PIXELS || !(depth instanceof Uint16Array) || depth.length !== PIXELS) {
    throw new TypeError('Labels/depth must be Uint8Array/Uint16Array(16384)');
  }
  if (!Number.isSafeInteger(minArea) || minArea < 1 || minArea > PIXELS) throw new RangeError('minArea must be an integer in1..16384');
  const camera = intrinsics === null ? approximateIntrinsics(horizontalFovDeg) : { ...intrinsics, source: 'explicit' };
  if (![camera.fx, camera.fy, camera.cx, camera.cy].every(finiteNumber) || camera.fx <= 0 || camera.fy <= 0) throw new RangeError('Intrinsics require finite positive fx/fy and finite cx/cy');
  const scratch = workspace ?? createClassObservationWorkspace();
  if (!(scratch.visited instanceof Uint8Array) || scratch.visited.length !== PIXELS ||
      !(scratch.queue instanceof Int32Array) || scratch.queue.length !== PIXELS ||
      overlaps(scratch.visited, scratch.queue) || overlaps(scratch.visited, labels) || overlaps(scratch.visited, depth) ||
      overlaps(scratch.queue, labels) || overlaps(scratch.queue, depth)) throw new RangeError('Invalid or aliased component workspace');
  const visited = scratch.visited; const queue = scratch.queue;
  visited.fill(0);
  const counts = new Uint32Array(5); const totalCounts = new Uint32Array(5);
  const sumU = new Float64Array(5); const sumV = new Float64Array(5);
  const sumX = new Float64Array(5); const sumY = new Float64Array(5); const sumZ = new Float64Array(5);
  for (let first = 0; first < PIXELS; ++first) {
    const cls = labels[first];
    if (cls >= 5 || depth[first] === 0 || visited[first]) continue;
    let begin = 0; let end = 1; let area = 0;
    let uSum = 0; let vSum = 0; let xSum = 0; let ySum = 0; let zSum = 0;
    queue[0] = first; visited[first] = 1;
    while (begin < end) {
      const i = queue[begin++]; const row = Math.floor(i / WIDTH); const col = i % WIDTH; const z = depth[i];
      ++area; uSum += col; vSum += row;
      xSum += (col - camera.cx) * z / camera.fx; ySum += (row - camera.cy) * z / camera.fy; zSum += z;
      let next;
      if (row > 0) { next = i - WIDTH; if (!visited[next] && labels[next] === cls && depth[next] > 0) { visited[next] = 1; queue[end++] = next; } }
      if (row + 1 < HEIGHT) { next = i + WIDTH; if (!visited[next] && labels[next] === cls && depth[next] > 0) { visited[next] = 1; queue[end++] = next; } }
      if (col > 0) { next = i - 1; if (!visited[next] && labels[next] === cls && depth[next] > 0) { visited[next] = 1; queue[end++] = next; } }
      if (col + 1 < WIDTH) { next = i + 1; if (!visited[next] && labels[next] === cls && depth[next] > 0) { visited[next] = 1; queue[end++] = next; } }
    }
    totalCounts[cls] += area;
    if (area > counts[cls]) {
      counts[cls] = area; sumU[cls] = uSum; sumV[cls] = vSum;
      sumX[cls] = xSum; sumY[cls] = ySum; sumZ[cls] = zSum;
    }
  }
  const observations = [null, null, null, null, null]; const centroids = [null, null, null, null, null];
  for (let cls = 0; cls < 5; ++cls) if (counts[cls] >= minArea) {
    const area = counts[cls];
    observations[cls] = [sumX[cls] / area, sumY[cls] / area, sumZ[cls] / area];
    centroids[cls] = [sumU[cls] / area, sumV[cls] / area];
  }
  return { observations, counts, totalCounts, centroids, intrinsics: camera, estimated: true, units: UNITS,
    coordinateSystem: 'camera XYZ: +x right, +y down, +z away',
    method: 'largest four-connected component, mean of backprojected pixel XYZ' };
}
