/** Button-triggered camera lifecycle and one-in-flight RGB frame scheduling.
 * No camera access occurs in the constructor. Call start() from a user action.
 * Inference pixels are never mirrored; mirror only the displayed preview.
 *
 * onFrame receives {canvas,timestampS,crop,metadata,signal,isCurrent}.
 * Treat canvas as borrowed until the returned Promise settles. Check signal
 * or isCurrent() after awaits before displaying results. stop() immediately
 * releases camera tracks, even if nonabortable model work is still finishing.
 * A timed-out/unfinished job retains its lock: start() cannot overlap it.
 *
 * Primary API docs:
 * https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/getUserMedia
 * https://developer.mozilla.org/en-US/docs/Web/API/HTMLVideoElement/requestVideoFrameCallback
 * https://developer.mozilla.org/en-US/docs/Web/API/MediaStreamTrack/stop
 */

function error(name, message) {
  const e = new Error(message);
  e.name = name;
  return e;
}

/** Pixel-edge crop rectangle. Output pixels are square, with no stretch.
 * Browser videoWidth/videoHeight already describe the presented orientation;
 * no additional rotation, aspect correction or mirroring is applied here.
 */
export function centerSquareCrop(width, height, size = 128) {
  for (const [key, value] of Object.entries({ width, height, size })) {
    if (!Number.isInteger(value) || value < 1) throw new RangeError(`Invalid ${key}`);
  }
  const side = Math.min(width, height);
  return Object.freeze({
    sourceWidth: width, sourceHeight: height,
    sx: Math.floor((width - side) / 2), sy: Math.floor((height - side) / 2),
    sw: side, sh: side, width: size, height: size,
    sourcePixelsPerOutputPixel: side / size,
    inferenceMirrored: false, rotationAppliedDegrees: 0,
  });
}

/** Map a zero-based output pixel center to a native video pixel center. */
export function outputToSource(x, y, crop) {
  const scale = crop.sourcePixelsPerOutputPixel;
  return [crop.sx + (x + .5) * scale - .5, crop.sy + (y + .5) * scale - .5];
}

export function sourceToOutput(x, y, crop) {
  const scale = crop.sourcePixelsPerOutputPixel;
  return [(x - crop.sx + .5) / scale - .5, (y - crop.sy + .5) / scale - .5];
}

function stopTracks(stream) {
  for (const track of stream?.getTracks?.() || []) {
    try { track.stop(); } catch { /* Release other tracks even if one fails. */ }
  }
}

function videoReady(video, signal) {
  return new Promise((resolve, reject) => {
    const events = ['loadedmetadata', 'loadeddata', 'canplay', 'playing', 'resize'];
    const clean = () => {
      for (const event of events) video.removeEventListener(event, check);
      video.removeEventListener('error', failed);
      signal.removeEventListener('abort', aborted);
    };
    const check = () => {
      if (video.videoWidth > 0 && video.videoHeight > 0 && video.readyState >= 2) {
        clean(); resolve();
      }
    };
    const failed = () => { clean(); reject(error('CameraVideoError', 'The camera video could not be decoded.')); };
    const aborted = () => { clean(); reject(signal.reason || error('AbortError', 'Camera start was cancelled.')); };
    for (const event of events) video.addEventListener(event, check);
    video.addEventListener('error', failed);
    signal.addEventListener('abort', aborted, { once: true });
    if (signal.aborted) aborted();
    else check();
  });
}

export class FrameScheduler {
  constructor({
    video, onFrame, onError = () => {}, onStateChange = () => {},
    size = 128, maxFps = 10, startTimeoutMs = 20000, frameTimeoutMs = 30000,
    getUserMedia = globalThis.navigator?.mediaDevices?.getUserMedia?.bind(globalThis.navigator?.mediaDevices),
    createCanvas = () => globalThis.document.createElement('canvas'),
    requestAnimationFrame = globalThis.requestAnimationFrame?.bind(globalThis),
    cancelAnimationFrame = globalThis.cancelAnimationFrame?.bind(globalThis),
    setTimeout = globalThis.setTimeout.bind(globalThis),
    clearTimeout = globalThis.clearTimeout.bind(globalThis),
    now = () => globalThis.performance.now(),
    lifecycleTarget = globalThis, document = globalThis.document,
  } = {}) {
    if (!video || typeof video.play !== 'function' || typeof onFrame !== 'function') throw new TypeError('video and onFrame are required');
    if (!Number.isInteger(size) || size < 1) throw new RangeError('size must be a positive integer');
    for (const [key, value] of Object.entries({ maxFps, startTimeoutMs, frameTimeoutMs })) {
      if (!Number.isFinite(value) || value <= 0) throw new RangeError(`${key} must be positive`);
    }
    this.video = video;
    this.onFrame = onFrame;
    this.onError = onError;
    this.onStateChange = onStateChange;
    this.size = size;
    this.maxFps = maxFps;
    this.startTimeoutMs = startTimeoutMs;
    this.frameTimeoutMs = frameTimeoutMs;
    this._getUserMedia = getUserMedia;
    this._createCanvas = createCanvas;
    this._raf = requestAnimationFrame;
    this._cancelRaf = cancelAnimationFrame;
    this._setTimeout = setTimeout;
    this._clearTimeout = clearTimeout;
    this._now = now;
    this._lifecycle = lifecycleTarget;
    this._document = document;
    this.state = 'stopped';
    this.stream = null;
    this.settings = null;
    this.displayMirror = false;
    this._epoch = 0;
    this._startup = null;
    this._job = null;
    this._scheduled = null;
    this._trackEndHandlers = [];
    this._canvas = null;
    this._lastSeenMediaTime = null;
    this._lastSubmittedMediaTime = null;
    this.stats = { callbacks: 0, submitted: 0, completed: 0, droppedBusy: 0, skippedDuplicate: 0, skippedRate: 0 };
    this._pageHide = () => this.stop('page-hidden');
    this._visibility = () => { if (this._document?.hidden) this.stop('page-hidden'); };
  }

  get running() { return this.state === 'running'; }
  get inFlight() { return this._job !== null; }

  _state(next, reason) {
    this.state = next;
    try { this.onStateChange({ state: next, reason, inFlight: this.inFlight }); } catch { /* UI callbacks cannot prevent cleanup. */ }
  }

  /** Permission may remain pending in the browser after timeout. Any stream
   * granted later is stopped immediately; it is never attached or processed.
   */
  async start({ facingMode = 'user', deviceId } = {}) {
    if (this.state !== 'stopped' || this.inFlight) throw error('CameraBusyError', 'Stop the camera and wait for pending inference to finish before starting again.');
    if (!['user', 'environment'].includes(facingMode)) throw new RangeError('facingMode must be user or environment');
    if (deviceId !== undefined && (typeof deviceId !== 'string' || !deviceId)) throw new TypeError('deviceId must be a nonempty string');
    if (typeof this._getUserMedia !== 'function') throw error('CameraUnavailableError', 'Camera access requires getUserMedia on HTTPS or localhost.');
    if (typeof this.video.requestVideoFrameCallback !== 'function' && typeof this._raf !== 'function') throw error('CameraSchedulingError', 'No video-frame or animation-frame callback is available.');
    const epoch = ++this._epoch;
    const controller = new AbortController();
    this._startup = controller;
    // A page can disappear while the browser permission prompt is pending.
    // Observe lifecycle during startup too; late grants are still released.
    this._lifecycle?.addEventListener?.('pagehide', this._pageHide);
    this._document?.addEventListener?.('visibilitychange', this._visibility);
    this._lastSeenMediaTime = null;
    this._lastSubmittedMediaTime = null;
    this.stats = { callbacks: 0, submitted: 0, completed: 0, droppedBusy: 0, skippedDuplicate: 0, skippedRate: 0 };
    this._state('starting', null);
    const abort = new Promise((_, reject) => {
      controller.signal.addEventListener('abort', () => reject(controller.signal.reason || error('AbortError', 'Camera start was cancelled.')), { once: true });
    });
    const timeout = this._setTimeout(() => controller.abort(error('CameraStartTimeoutError', 'Camera permission or startup timed out.')), this.startTimeoutMs);
    const constraints = { audio: false, video: {
      width: { ideal: 640 }, height: { ideal: 480 }, frameRate: { ideal: 15, max: 30 },
      ...(deviceId ? { deviceId: { exact: deviceId } } : { facingMode: { ideal: facingMode } }),
    } };
    try {
      const requested = Promise.resolve().then(() => {
        if (epoch !== this._epoch || controller.signal.aborted) throw controller.signal.reason || error('AbortError', 'Camera start was cancelled.');
        return this._getUserMedia(constraints);
      });
      requested.then((stream) => {
        if (epoch !== this._epoch || controller.signal.aborted) stopTracks(stream);
      }, () => {});
      const stream = await Promise.race([requested, abort]);
      if (epoch !== this._epoch || controller.signal.aborted) throw controller.signal.reason || error('AbortError', 'Camera start was cancelled.');
      this.stream = stream;
      const tracks = stream.getVideoTracks?.() || stream.getTracks?.() || [];
      if (!tracks.length) throw error('CameraUnavailableError', 'The granted stream has no video track.');
      const settings = tracks[0].getSettings?.() || {};
      // Do not expose persistent device identifiers in routine frame metadata.
      this.settings = Object.fromEntries(['width', 'height', 'frameRate', 'facingMode', 'resizeMode'].filter((key) => settings[key] !== undefined).map((key) => [key, settings[key]]));
      this.displayMirror = (settings.facingMode || facingMode) === 'user';
      this.video.muted = true;
      this.video.playsInline = true;
      this.video.setAttribute?.('playsinline', '');
      this.video.srcObject = stream;
      const ready = videoReady(this.video, controller.signal);
      await Promise.race([Promise.all([ready, Promise.resolve().then(() => this.video.play())]), abort]);
      if (epoch !== this._epoch || controller.signal.aborted) throw controller.signal.reason || error('AbortError', 'Camera start was cancelled.');
      this._canvas ||= this._createCanvas();
      this._canvas.width = this.size;
      this._canvas.height = this.size;
      if (!this._canvas.getContext('2d')) throw error('CameraCanvasError', 'A 2D canvas is required to snapshot camera frames.');
      for (const track of tracks) {
        const ended = () => this._fail(error('CameraEndedError', 'The camera stream ended.'), epoch);
        track.addEventListener?.('ended', ended);
        this._trackEndHandlers.push([track, ended]);
      }
      this._startup = null;
      this._state('running', null);
      this._schedule(epoch);
      return { settings: { ...this.settings }, displayMirror: this.displayMirror, size: this.size };
    } catch (cause) {
      if (epoch === this._epoch) this.stop('start-error');
      throw cause;
    } finally {
      this._clearTimeout(timeout);
      if (this._startup === controller) this._startup = null;
      // Remove readiness listeners even when play() rejects before readiness.
      if (!controller.signal.aborted) controller.abort(error('AbortError', 'Startup settled.'));
    }
  }

  _schedule(epoch) {
    if (!this.running || epoch !== this._epoch || this._scheduled) return;
    const callback = (wall, metadata = {}) => {
      this._scheduled = null;
      if (!this.running || epoch !== this._epoch) return;
      this._schedule(epoch);
      this._frame(epoch, wall, metadata);
    };
    if (typeof this.video.requestVideoFrameCallback === 'function') {
      this._scheduled = { kind: 'video', id: this.video.requestVideoFrameCallback(callback) };
    } else if (typeof this._raf === 'function') {
      this._scheduled = { kind: 'animation', id: this._raf(callback) };
    } else this._fail(error('CameraSchedulingError', 'No video-frame or animation-frame callback is available.'), epoch);
  }

  _frame(epoch, wall, metadata) {
    this.stats.callbacks += 1;
    if (this.video.readyState < 2 || !this.video.videoWidth || !this.video.videoHeight) return;
    const mediaTime = Number.isFinite(metadata.mediaTime) ? metadata.mediaTime : this.video.currentTime;
    if (!Number.isFinite(mediaTime) || mediaTime < 0) return;
    if (this._lastSeenMediaTime !== null && mediaTime < this._lastSeenMediaTime) {
      this._fail(error('CameraTimingError', 'Camera timestamps moved backwards; restart the stream.'), epoch);
      return;
    }
    if (mediaTime === this._lastSeenMediaTime) { this.stats.skippedDuplicate += 1; return; }
    this._lastSeenMediaTime = mediaTime;
    if (this._job) { this.stats.droppedBusy += 1; return; }
    if (this._lastSubmittedMediaTime !== null && mediaTime - this._lastSubmittedMediaTime < 1 / this.maxFps - 1e-9) {
      this.stats.skippedRate += 1; return;
    }
    const controller = new AbortController();
    const job = { epoch, controller, promise: null, timeout: null };
    this._job = job;
    this.stats.submitted += 1;
    this._lastSubmittedMediaTime = mediaTime;
    let crop;
    try {
      crop = centerSquareCrop(this.video.videoWidth, this.video.videoHeight, this.size);
      const context = this._canvas.getContext('2d');
      context.setTransform(1, 0, 0, 1, 0, 0);
      context.imageSmoothingEnabled = true;
      context.drawImage(this.video, crop.sx, crop.sy, crop.sw, crop.sh, 0, 0, this.size, this.size);
    } catch (cause) {
      this._job = null;
      this._fail(cause, epoch);
      return;
    }
    job.timeout = this._setTimeout(() => this._fail(error('CameraFrameTimeoutError', 'Frame processing timed out; camera stopped. Wait for pending inference before restarting.'), epoch), this.frameTimeoutMs);
    const packet = {
      canvas: this._canvas, timestampS: mediaTime, crop,
      metadata: Object.freeze({
        mediaTime, wallTimeS: (Number.isFinite(wall) ? wall : this._now()) / 1000,
        presentedFrames: Number.isFinite(metadata.presentedFrames) ? metadata.presentedFrames : null,
        sourceWidth: crop.sourceWidth, sourceHeight: crop.sourceHeight,
        displayMirror: this.displayMirror, inferenceMirrored: false,
      }),
      signal: controller.signal,
      isCurrent: () => this.running && epoch === this._epoch && !controller.signal.aborted,
    };
    job.promise = Promise.resolve().then(() => {
      if (!packet.isCurrent()) return;
      return this.onFrame(packet);
    }).then(() => {
      if (packet.isCurrent()) this.stats.completed += 1;
    }).catch((cause) => {
      if (packet.isCurrent()) this._fail(cause, epoch);
    }).finally(() => {
      this._clearTimeout(job.timeout);
      if (this._job === job) {
        this._job = null;
        // In particular, re-enable restart after stopped nonabortable work
        // settles. running alone does not describe this short locked interval.
        this._state(this.state, 'inference-settled');
      }
    });
  }

  _fail(cause, epoch) {
    if (epoch !== this._epoch) return;
    this.stop('error');
    try { this.onError(cause); } catch { /* Camera cleanup already completed. */ }
  }

  /** Synchronous camera release; model cancellation is cooperative via signal. */
  stop(reason = 'stopped') {
    ++this._epoch;
    this._startup?.abort(error('AbortError', 'Camera start was cancelled.'));
    this._startup = null;
    if (this._scheduled) {
      const { kind, id } = this._scheduled;
      if (kind === 'video') this.video.cancelVideoFrameCallback?.(id);
      else this._cancelRaf?.(id);
      this._scheduled = null;
    }
    for (const [track, ended] of this._trackEndHandlers) track.removeEventListener?.('ended', ended);
    this._trackEndHandlers = [];
    this._lifecycle?.removeEventListener?.('pagehide', this._pageHide);
    this._document?.removeEventListener?.('visibilitychange', this._visibility);
    if (this._job) {
      this._clearTimeout(this._job.timeout);
      this._job.controller.abort(error('AbortError', 'Frame processing was cancelled.'));
    }
    const stream = this.stream;
    this.stream = null;
    try { this.video.pause(); } catch { /* Still release all tracks. */ }
    if (this.video.srcObject === stream) this.video.srcObject = null;
    stopTracks(stream);
    this._state('stopped', reason);
  }

  /** Wait for an already-running nonabortable processor without opening camera. */
  async whenIdle() { if (this._job?.promise) await this._job.promise; }
}
