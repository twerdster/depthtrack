const DEFAULT_LOAD_TIMEOUT = 180000;
const DEFAULT_INFERENCE_TIMEOUT = 30000;

/** A single-frame worker client. dispose() stops work immediately and is final. */
export class DepthModelClient {
  constructor({
    onStatus = () => {},
    onProgress = () => {},
    workerFactory = () => new Worker(new URL('./model-worker.mjs', import.meta.url), { type: 'module' }),
    loadTimeoutMs = DEFAULT_LOAD_TIMEOUT,
    inferenceTimeoutMs = DEFAULT_INFERENCE_TIMEOUT,
  } = {}) {
    for (const timeout of [loadTimeoutMs, inferenceTimeoutMs]) {
      if (!Number.isFinite(timeout) || timeout <= 0) throw new Error('Worker timeouts must be positive finite milliseconds');
    }
    this.onStatus = onStatus;
    this.onProgress = onProgress;
    this.workerFactory = workerFactory;
    this.loadTimeoutMs = loadTimeoutMs;
    this.inferenceTimeoutMs = inferenceTimeoutMs;
    this.worker = null;
    this.pending = null;
    this.nextId = 1;
    this.ready = false;
    this.disposed = false;
    this.metadata = null;
    this.options = null;
    this.epoch = 0;
  }

  _notify(callback, value) {
    // A presentation callback must not strand a pending inference promise.
    try { callback(value); } catch { /* The caller owns its presentation errors. */ }
  }

  _ensureWorker() {
    if (this.worker) return;
    const worker = this.workerFactory();
    this.worker = worker;
    worker.addEventListener('message', event => {
      if (this.worker !== worker) return;
      const message = event.data ?? {};
      if (message.type === 'status') {
        if (message.status?.backend) {
          this.metadata = { ...this.metadata, backend: message.status.backend, dtype: message.status.dtype };
        }
        this._notify(this.onStatus, message.status);
        return;
      }
      if (message.type === 'progress') {
        this._notify(this.onProgress, message.progress);
        return;
      }
      const pending = this.pending;
      if (!pending || message.id !== pending.id) return;
      if (message.type === 'error') {
        this._fatal(new Error(message.error || 'Model worker failed'));
      } else if (message.type === pending.expected) {
        this.pending = null;
        clearTimeout(pending.timer);
        pending.resolve(message.result);
      } else {
        this._fatal(new Error(`Unexpected worker response: ${message.type}`));
      }
    });
    worker.addEventListener('error', event => {
      if (this.worker === worker) this._fatal(new Error(event.message || 'Could not load or execute model worker'));
    });
    worker.addEventListener('messageerror', () => {
      if (this.worker === worker) this._fatal(new Error('Could not decode model worker response'));
    });
  }

  _terminate() {
    const worker = this.worker;
    this.worker = null;
    if (worker) worker.terminate();
  }

  _fatal(error) {
    this.epoch += 1;
    const pending = this.pending;
    this.pending = null;
    this.ready = false;
    this.metadata = null;
    this.options = null;
    this._terminate();
    if (pending) {
      clearTimeout(pending.timer);
      pending.reject(error);
    }
    this._notify(this.onStatus, { state: 'error', message: error.message });
  }

  _request(type, body, transfer, timeout, expected) {
    if (this.disposed) return Promise.reject(new Error('Depth model client is disposed'));
    if (this.pending) return Promise.reject(new Error('Depth model client is busy; overlapping work is not allowed'));
    try { this._ensureWorker(); } catch (error) {
      this._fatal(error);
      return Promise.reject(error);
    }
    return new Promise((resolve, reject) => {
      const id = this.nextId++;
      const timer = setTimeout(() => {
        if (this.pending?.id === id) this._fatal(new Error(`${type === 'load' ? 'Model loading' : 'Depth inference'} timed out after ${timeout} ms`));
      }, timeout);
      this.pending = { id, expected, timer, resolve, reject };
      try {
        this.worker.postMessage({ type, id, ...body }, transfer);
      } catch (error) { this._fatal(error); }
    });
  }

  async load({ device = 'auto', size = 294 } = {}) {
    if (this.disposed) throw new Error('Depth model client is disposed');
    if (!['auto', 'wasm'].includes(device)) throw new Error('device must be auto or wasm');
    if (!Number.isInteger(size) || size < 112 || size > 518 || size % 14 !== 0) {
      throw new Error('size must be a multiple of 14 from 112 through 518');
    }
    if (this.pending) throw new Error('Depth model client is busy; overlapping work is not allowed');
    if (this.ready && this.options?.device === device && this.options?.size === size) return this.metadata;
    this.ready = false;
    const epoch = this.epoch;
    const result = await this._request('load', { options: { device, size } }, [], this.loadTimeoutMs, 'loaded');
    if (this.disposed || epoch !== this.epoch) throw new Error('Depth model work stopped');
    this.ready = true;
    this.metadata = result;
    this.options = { device, size };
    return result;
  }

  async predict(imageData) {
    if (this.disposed) throw new Error('Depth model client is disposed');
    if (!this.ready) throw new Error('Load the depth model before predicting');
    if (this.pending) throw new Error('Depth model client is busy; overlapping work is not allowed');
    const width = imageData?.width;
    const height = imageData?.height;
    const source = imageData?.data;
    if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1 || width * height > 16777216 ||
        !(source instanceof Uint8Array || source instanceof Uint8ClampedArray) || source.length !== width * height * 4) {
      throw new Error('predict requires an RGBA ImageData-compatible image');
    }
    // Preserve the camera/display buffer; only transfer our private copy.
    const rgba = new Uint8ClampedArray(source);
    const epoch = this.epoch;
    const result = await this._request('predict', { width, height, rgba: rgba.buffer }, [rgba.buffer], this.inferenceTimeoutMs, 'prediction');
    if (this.disposed || epoch !== this.epoch) throw new Error('Depth model work stopped');
    if (!(result?.data instanceof Float32Array) || !Number.isInteger(result.width) || !Number.isInteger(result.height) ||
        result.width < 1 || result.height < 1 || result.data.length !== result.width * result.height || !result.data.every(Number.isFinite)) {
      const error = new Error('Invalid raw depth returned by model worker');
      this._fatal(error);
      throw error;
    }
    this.metadata = { ...this.metadata, backend: result.backend, dtype: result.dtype };
    return result;
  }

  _stopWork() {
    this.epoch += 1;
    const pending = this.pending;
    this.pending = null;
    this.ready = false;
    this.metadata = null;
    this.options = null;
    this._terminate();
    if (pending) {
      clearTimeout(pending.timer);
      pending.reject(new Error('Depth model work stopped'));
    }
  }

  stop() {
    // A stopped WASM/GPU job cannot be cancelled cooperatively: terminate it.
    if (this.disposed) return;
    this._stopWork();
    this._notify(this.onStatus, { state: 'stopped', message: 'Depth model worker stopped' });
  }

  dispose() {
    if (this.disposed) return;
    // Mark final disposal before any user callback can reenter load()/dispose().
    this.disposed = true;
    this._stopWork();
    this._notify(this.onStatus, { state: 'disposed', message: 'Depth model client disposed' });
  }
}
