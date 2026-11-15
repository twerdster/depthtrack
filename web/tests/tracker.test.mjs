/** Run with: node --test web/tests/tracker.test.mjs (no npm packages). */
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { AdaptivePointTracker, trackSequence } from '../tracker.mjs';

const sequence = JSON.parse(fs.readFileSync(new URL('../data/sequence.json', import.meta.url), 'utf8'));
const golden = JSON.parse(fs.readFileSync(new URL('./golden.json', import.meta.url), 'utf8'));
let maximumDifference = 0;

function close(actual, expected, path = 'state') {
  if (typeof expected === 'number') {
    assert.equal(typeof actual, 'number', `${path}: expected numeric value`);
    const difference = Math.abs(actual - expected);
    maximumDifference = Math.max(maximumDifference, difference);
    assert.ok(difference <= 1e-8, `${path}: ${actual} != ${expected}`);
  } else if (Array.isArray(expected)) {
    assert.ok(Array.isArray(actual), `${path}: expected array`);
    assert.equal(actual.length, expected.length, `${path}: array length`);
    expected.forEach((v, i) => close(actual[i], v, `${path}[${i}]`));
  } else if (expected !== null && typeof expected === 'object') {
    assert.deepEqual(Object.keys(actual).sort(), Object.keys(expected).sort(), `${path}: keys`);
    for (const [key, value] of Object.entries(expected)) close(actual[key], value, `${path}.${key}`);
  } else assert.equal(actual, expected, path);
}

test('all recorded Python states: 130 frames / 635 observations, including history', (t) => {
  const states = trackSequence(sequence.frames, sequence.parameters);
  let observed = 0, accepted = 0, heldJump = 0;
  states.forEach((frame, i) => {
    frame.fingers.forEach((state, c) => {
      close(state, sequence.frames[i].expected[c], `recorded[${i}][${c}]`);
      observed += sequence.frames[i].observations[c] !== null;
      accepted += state.accepted;
      heldJump += state.reason === 'jump';
    });
  });
  assert.equal(states.length, 130);
  assert.equal(observed, 635);
  assert.equal(accepted, 634);
  assert.equal(heldJump, 1);
  t.diagnostic(`Maximum numeric deviation from Python: ${maximumDifference}`);
});

for (const c of golden.cases) {
  test(`Python failure-case parity: ${c.name}`, () => {
    const tracker = new AdaptivePointTracker(c.parameters);
    for (const [i, update] of c.updates.entries()) close(tracker.update(update.time_s, update.point), update.expected, `${c.name}[${i}]`);
  });
}

test('causal prefix and snapshot immutability', () => {
  const prefix = trackSequence(sequence.frames.slice(0, 50), sequence.parameters);
  const full = trackSequence(sequence.frames, sequence.parameters);
  assert.deepEqual(prefix, full.slice(0, 50));
  const tracker = new AdaptivePointTracker();
  const first = tracker.update(0, [30, -20, 350]);
  const frozen = JSON.stringify(first);
  tracker.update(.1, [31, -18, 355]);
  tracker.update(.2, null);
  assert.equal(JSON.stringify(first), frozen);
});

test('nonfinite observation is missing and invalid timestamps are atomic', () => {
  const tracker = new AdaptivePointTracker();
  tracker.update(0, [30, -20, 350]);
  const invalid = tracker.update(.1, [NaN, 0, 350]);
  assert.equal(invalid.reason, 'invalid');
  assert.equal(invalid.accepted, false);
  assert.deepEqual(invalid.position, [30, -20, 350]);
  const before = JSON.stringify(tracker);
  for (const bad of [.1, 0, NaN, Infinity]) {
    assert.throws(() => tracker.update(bad, [30, -20, 350]), /strictly increasing/);
    assert.equal(JSON.stringify(tracker), before);
  }
});

test('invalid parameters and malformed points fail explicitly', () => {
  for (const p of [{ min_cutoff_hz: 0 }, { speed_beta: -1 }, { max_cutoff_hz: .5 }, { reset_gap_seconds: .1 }, { speed_beta: NaN }]) {
    assert.throws(() => new AdaptivePointTracker(p));
  }
  const tracker = new AdaptivePointTracker();
  assert.throws(() => tracker.update(0, [1, 2]), /XYZ/);
  assert.equal(tracker.inputTime, null);
});
