/** Causal timestamp-aware 3D tracking, mm / seconds.
 * JavaScript port of DepthTrack's Python adaptive_tracker.py.
 * Based on the 1€ filter (Casiez, Roussel & Vogel, 2012), with shared XYZ
 * cutoff, bounded hold, jump confirmation and immutable timestamped trails.
 * It is a successor to Aaron Wetzler's historical ZScan tracker, not that code.
 */
export const DEFAULT_PARAMETERS = Object.freeze({
  min_cutoff_hz: 1.2,
  speed_beta: 0.02,
  derivative_cutoff_hz: 1.0,
  max_cutoff_hz: 10.0,
  jump_slack_mm: 25.0,
  max_speed_mm_s: 1200.0,
  hold_seconds: 0.25,
  reset_gap_seconds: 0.45,
  trail_seconds: 0.65,
});

export function alpha(cutoffHz, dt) {
  const x = 2 * Math.PI * cutoffHz * dt;
  return x / (1 + x);
}

const norm = (v) => Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
const distance = (a, b) => norm(a.map((v, i) => v - b[i]));

export class AdaptivePointTracker {
  constructor(parameters = {}) {
    this.parameters = { ...DEFAULT_PARAMETERS, ...parameters };
    for (const [key, value] of Object.entries(this.parameters)) {
      if (!(key in DEFAULT_PARAMETERS) || !Number.isFinite(value) || value < 0) {
        throw new RangeError(`Invalid parameter: ${key}`);
      }
    }
    const p = this.parameters;
    if (Math.min(p.min_cutoff_hz, p.derivative_cutoff_hz) <= 0) throw new RangeError('Cutoffs must be positive');
    if (p.max_cutoff_hz < p.min_cutoff_hz) throw new RangeError('Maximum cutoff below minimum');
    if (p.reset_gap_seconds < p.hold_seconds) throw new RangeError('Reset gap below hold duration');
    this.position = null;
    this.raw = null;
    this.velocity = [0, 0, 0];
    this.acceptedTime = null;
    this.inputTime = null;
    this.pending = null;
    this.pendingTime = null;
    this.generation = 0;
    this.everInitialized = false;
    this.history = [];
  }

  _initialize(t, z, status) {
    this.position = [...z];
    this.raw = [...z];
    this.velocity = [0, 0, 0];
    this.acceptedTime = t;
    this.pending = null;
    this.pendingTime = null;
    this.generation += 1;
    this.everInitialized = true;
    this.history = [{ time_s: t, position: [...z] }];
    return this._snapshot(t, status, true, null);
  }

  _snapshot(t, status, accepted, reason) {
    const age = this.acceptedTime === null ? null : t - this.acceptedTime;
    const visible = this.position !== null && age <= this.parameters.hold_seconds;
    if (!visible) status = 'lost';
    this.history = this.history.filter((h) => t - h.time_s <= this.parameters.trail_seconds);
    return {
      status, accepted, reason,
      position: visible ? [...this.position] : null,
      observation_age_s: age,
      generation: this.generation,
      history: this.history.map((h) => ({ time_s: h.time_s, position: [...h.position] })),
    };
  }

  update(timeS, point = null) {
    const t = Number(timeS);
    if (!Number.isFinite(t) || (this.inputTime !== null && t <= this.inputTime)) {
      throw new RangeError('Input timestamps must be finite and strictly increasing');
    }
    let z = null;
    let invalid = false;
    if (point !== null && point !== undefined) {
      if (!Array.isArray(point) || point.length !== 3) throw new TypeError('Expected one XYZ point');
      if (!point.every(Number.isFinite) || point[2] <= 0) invalid = true;
      else z = [...point];
    }
    this.inputTime = t;
    const p = this.parameters;
    if (z === null) {
      this.pending = null;
      this.pendingTime = null;
      return this._snapshot(t, 'held', false, invalid ? 'invalid' : 'missing');
    }
    if (this.position === null || t - this.acceptedTime > p.reset_gap_seconds) {
      return this._initialize(t, z, this.everInitialized ? 'reacquired' : 'initialized');
    }
    const dt = t - this.acceptedTime;
    const gate = p.jump_slack_mm + p.max_speed_mm_s * dt;
    if (distance(z, this.raw) > gate) {
      const confirmed = this.pending !== null
        && t - this.pendingTime <= p.reset_gap_seconds
        && distance(z, this.pending) <= p.jump_slack_mm + p.max_speed_mm_s * (t - this.pendingTime);
      if (confirmed) return this._initialize(t, z, 'reacquired');
      this.pending = [...z];
      this.pendingTime = t;
      return this._snapshot(t, 'held', false, 'jump');
    }
    this.pending = null;
    this.pendingTime = null;
    const ad = alpha(p.derivative_cutoff_hz, dt);
    this.velocity = z.map((v, i) => ad * ((v - this.raw[i]) / dt) + (1 - ad) * this.velocity[i]);
    const cutoff = Math.min(p.max_cutoff_hz, p.min_cutoff_hz + p.speed_beta * norm(this.velocity));
    const a = alpha(cutoff, dt);
    this.position = z.map((v, i) => a * v + (1 - a) * this.position[i]);
    this.raw = [...z];
    this.acceptedTime = t;
    this.history.push({ time_s: t, position: [...this.position] });
    const state = this._snapshot(t, 'updated', true, null);
    state.cutoff_hz = cutoff;
    return state;
  }
}

/** Precompute in timestamp order; no tracker update reads a later frame.
 * Scrubbing can then retrieve the same causal state without changing history.
 */
export function trackSequence(frames, parameters = {}) {
  const trackers = Array.from({ length: 5 }, () => new AdaptivePointTracker(parameters));
  return frames.map((frame) => {
    if (!Array.isArray(frame.observations) || frame.observations.length !== 5) throw new TypeError('Expected five observation slots');
    return {
      frame: frame.frame,
      time_s: frame.time_s,
      fingers: trackers.map((tracker, c) => tracker.update(frame.time_s, frame.observations[c])),
    };
  });
}
