import { AutoModelForDepthEstimation, AutoProcessor, RawImage, env } from 'https://cdn.jsdelivr.net/npm/@huggingface/transformers@3.8.1/dist/transformers.min.js';

// This checkpoint predicts relative inverse depth, not metres or millimetres.
const MODEL = 'onnx-community/depth-anything-v2-small';
const REVISION = '4472b7362082ad9968fee890ca0f1e5aca36b93d';
env.allowLocalModels = false;
// GitHub Pages does not supply the COOP/COEP headers required by WASM threads.
env.backends.onnx.wasm.numThreads = 1;

let model = null;
let processor = null;
let configuration = null;
let backend = null;
let dtype = null;
let busy = false;
let successfulPredictions = 0;

const status = (state, message, extra = {}) => self.postMessage({
  type: 'status', status: { state, message, backend, dtype, ...extra },
});
const progress = event => {
  const result = {};
  for (const key of ['status', 'name', 'file', 'progress', 'loaded', 'total']) {
    if (typeof event[key] === 'string' || typeof event[key] === 'number') result[key] = event[key];
  }
  self.postMessage({ type: 'progress', progress: result });
};

function validateOptions(options = {}) {
  const device = options.device ?? 'auto';
  const size = options.size ?? 294;
  if (!['auto', 'wasm'].includes(device)) throw new Error('device must be auto or wasm');
  if (!Number.isInteger(size) || size < 112 || size > 518 || size % 14 !== 0) {
    throw new Error('size must be a multiple of 14 from 112 through 518');
  }
  return { device, size };
}

async function disposeModel() {
  const oldModel = model;
  model = null;
  if (oldModel?.dispose) await oldModel.dispose();
}

async function loadBackend(device, precision) {
  backend = device;
  dtype = precision;
  status('loading', `Loading ${device.toUpperCase()} ${precision} model`);
  model = await AutoModelForDepthEstimation.from_pretrained(MODEL, {
    revision: REVISION,
    device,
    dtype: precision,
    progress_callback: progress,
  });
}

async function load(options) {
  configuration = validateOptions(options);
  await disposeModel();
  processor = null;
  backend = null;
  dtype = null;
  successfulPredictions = 0;
  status('loading', 'Loading depth processor');
  processor = await AutoProcessor.from_pretrained(MODEL, {
    revision: REVISION,
    progress_callback: progress,
  });
  // The depth pipeline does not forward size overrides in Transformers.js 3.8.1.
  // Configure the processor itself and check its real tensor on every frame.
  processor.image_processor.size = { height: configuration.size, width: configuration.size };
  processor.image_processor.config.keep_aspect_ratio = false;
  if (configuration.device === 'auto') {
    try {
      const adapter = await self.navigator.gpu?.requestAdapter();
      if (!adapter) throw new Error('WebGPU adapter unavailable');
      await loadBackend('webgpu', adapter.features.has('shader-f16') ? 'fp16' : 'fp32');
    } catch (error) {
      await disposeModel();
      status('fallback', 'WebGPU load unavailable; trying WASM q8', { reason: String(error.message ?? error) });
      await loadBackend('wasm', 'q8');
    }
  } else {
    await loadBackend('wasm', 'q8');
  }
  const result = { backend, dtype, size: configuration.size, model: MODEL, revision: REVISION };
  status('ready', 'Relative inverse-depth model ready', result);
  return result;
}

function disposeTensors(values) {
  const seen = new Set();
  for (const value of Object.values(values ?? {})) {
    if (value && !seen.has(value) && typeof value.dispose === 'function') {
      seen.add(value);
      value.dispose();
    }
  }
}

function copyFloatDepth(tensor) {
  const source = tensor.data;
  if (!['float16', 'float32', 'float64'].includes(tensor.type)) {
    throw new Error(`Unexpected predicted_depth dtype: ${tensor.type}`);
  }
  if (tensor.type !== 'float16' || !(source instanceof Uint16Array)) return new Float32Array(source);
  // ORT uses Uint16Array half-bit storage when native Float16Array is absent.
  // Transformers.js 3.8.1 Tensor.to() does not decode that representation.
  return Float32Array.from(source, bits => {
    const sign = bits & 0x8000 ? -1 : 1;
    const exponent = (bits >>> 10) & 31;
    const fraction = bits & 1023;
    if (exponent === 31) return fraction ? NaN : sign * Infinity;
    return sign * (exponent ? (1 + fraction / 1024) * 2 ** (exponent - 15) : fraction * 2 ** -24);
  });
}

async function inferOnce(image) {
  let inputs;
  let outputs;
  try {
    const preprocessStart = performance.now();
    inputs = await processor(image);
    const inputShape = [...inputs.pixel_values.dims];
    const size = configuration.size;
    if (inputShape.length !== 4 || inputShape[0] !== 1 || inputShape[1] !== 3 || inputShape[2] !== size || inputShape[3] !== size) {
      throw new Error(`Unexpected model input shape: ${JSON.stringify(inputShape)}`);
    }
    const inferenceStart = performance.now();
    const preprocessMs = inferenceStart - preprocessStart;
    outputs = await model(inputs);
    const inferenceMs = performance.now() - inferenceStart;
    const tensor = outputs.predicted_depth;
    if (!tensor) throw new Error('Model returned no predicted_depth tensor');
    const dims = [...tensor.dims];
    const height = dims.at(-2);
    const width = dims.at(-1);
    if (dims.length < 2 || !Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1 || dims.reduce((a, b) => a * b, 1) !== width * height) {
      throw new Error(`Unexpected predicted_depth shape: ${JSON.stringify(dims)}`);
    }
    const data = copyFloatDepth(tensor);
    if (data.length !== width * height || !data.every(Number.isFinite)) {
      throw new Error('Model returned incomplete or non-finite relative depth');
    }
    return { data, width, height, dims, inputShape, preprocessMs, inferenceMs, backend, dtype };
  } finally {
    disposeTensors(outputs);
    disposeTensors(inputs);
  }
}

async function predict(message) {
  if (!model || !processor) throw new Error('Load the model before predicting');
  const { width, height } = message;
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1 || width * height > 16777216) {
    throw new Error('Invalid RGBA image dimensions');
  }
  const rgba = new Uint8ClampedArray(message.rgba);
  if (rgba.length !== width * height * 4) throw new Error('RGBA buffer length does not match dimensions');
  const image = new RawImage(rgba, width, height, 4);
  status('running', 'Estimating relative inverse depth');
  try {
    let result;
    try {
      result = await inferOnce(image);
    } catch (error) {
      if (backend !== 'webgpu' || successfulPredictions !== 0 || configuration.device !== 'auto') throw error;
      await disposeModel();
      status('fallback', 'First WebGPU inference failed; retrying with WASM q8', { reason: String(error.message ?? error) });
      await loadBackend('wasm', 'q8');
      result = await inferOnce(image);
    }
    successfulPredictions += 1;
    status('ready', 'Relative inverse-depth frame complete');
    return result;
  } catch (error) {
    status('error', String(error.message ?? error));
    throw error;
  }
}

self.addEventListener('message', async event => {
  const message = event.data ?? {};
  if (!Number.isInteger(message.id)) return;
  if (busy) {
    self.postMessage({ type: 'error', id: message.id, error: 'Model worker is busy' });
    return;
  }
  busy = true;
  try {
    if (message.type === 'load') {
      self.postMessage({ type: 'loaded', id: message.id, result: await load(message.options) });
    } else if (message.type === 'predict') {
      const result = await predict(message);
      self.postMessage({ type: 'prediction', id: message.id, result }, [result.data.buffer]);
    } else {
      throw new Error(`Unknown worker command: ${message.type}`);
    }
  } catch (error) {
    self.postMessage({ type: 'error', id: message.id, error: String(error.message ?? error) });
  } finally {
    busy = false;
  }
});
