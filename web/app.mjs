import { trackSequence } from './tracker.mjs';

const $ = (id) => document.getElementById(id);
const NAMES = ['Thumb', 'Index', 'Middle', 'Ring', 'Pinky'];
const COLORS = ['#ff6868', '#ffcb4d', '#46db8c', '#48bfff', '#c280ff'];
const canvas = { baseline: $('baseline-canvas'), improved: $('improved-canvas') };
const ctx = Object.fromEntries(Object.entries(canvas).map(([k, v]) => [k, v.getContext('2d')]));
let sequence, states, atlas, baselineTrails;
let index = 0;
let playing = false;
let view = 'improved';
let animation = 0;
let anchorWall = 0;
let anchorTime = 0;
let endTime = 0;
let speed = 1;

function announce(text) { $('announcement').textContent = text; }

function validateSequence(s) {
  if (s.schema_version !== 1 || s.width !== 320 || s.height !== 240 || !s.frames?.length) throw new Error('Unsupported recorded sequence format.');
  for (const key of ['fx', 'fy', 'cx', 'cy']) if (!Number.isFinite(s.intrinsics?.[key])) throw new Error('Missing camera projection.');
  if (s.intrinsics.fx <= 0 || s.intrinsics.fy <= 0) throw new Error('Invalid camera projection.');
  if (!Number.isInteger(s.atlas?.columns) || s.atlas.columns < 1 || s.atlas.tile_width !== 320 || s.atlas.tile_height !== 240) throw new Error('Invalid depth atlas.');
  s.frames.forEach((f, i) => {
    if (f.frame !== i + 1 || !Number.isFinite(f.time_s) || (i && f.time_s <= s.frames[i - 1].time_s)) throw new Error('Recorded frames must be chronological.');
    if (f.observations?.length !== 5 || f.baseline?.length !== 5) throw new Error('Expected five fingertip slots.');
    for (const p of f.observations) if (p !== null && (!Array.isArray(p) || p.length !== 3 || !p.every(Number.isFinite))) throw new Error('Invalid detector position.');
  });
}

function baselineHistory(frames, trailSeconds) {
  const histories = Array.from({ length: 5 }, () => []);
  const previousAge = Array(5).fill(null);
  return frames.map((f) => f.baseline.map((point, c) => {
    if (!point || point.age < 4) {
      histories[c] = [];
      previousAge[c] = null;
      return [];
    }
    if (previousAge[c] === null || point.age <= previousAge[c]) histories[c] = [];
    previousAge[c] = point.age;
    histories[c] = histories[c].filter((h) => f.time_s - h.time_s <= trailSeconds);
    histories[c].push({ time_s: f.time_s, position: [...point.position] });
    return histories[c].map((h) => ({ time_s: h.time_s, position: [...h.position] }));
  }));
}

function project(p) {
  if (!p || !p.every(Number.isFinite) || p[2] <= 0) return null;
  const k = sequence.intrinsics;
  return [2 * (k.fx * p[0] / p[2] + k.cx), 2 * (k.fy * p[1] / p[2] + k.cy)];
}

function drawPanel(kind) {
  const g = ctx[kind];
  const f = sequence.frames[index];
  const a = sequence.atlas;
  g.clearRect(0, 0, 640, 480);
  g.imageSmoothingEnabled = false;
  g.drawImage(atlas, (index % a.columns) * a.tile_width, Math.floor(index / a.columns) * a.tile_height, a.tile_width, a.tile_height, 0, 0, 640, 480);
  let fresh = 0;
  for (let c = 0; c < 5; c += 1) {
    const color = COLORS[c];
    const raw = project(f.observations[c]);
    g.strokeStyle = color;
    if ($('show-raw').checked && raw) {
      g.lineWidth = 1.6;
      g.beginPath(); g.moveTo(raw[0] - 6, raw[1]); g.lineTo(raw[0] + 6, raw[1]);
      g.moveTo(raw[0], raw[1] - 6); g.lineTo(raw[0], raw[1] + 6); g.stroke();
    }
    const point = kind === 'improved' ? states[index].fingers[c] : f.baseline[c];
    if (!point || (kind === 'baseline' && point.age < 4)) continue;
    const p = project(point.position);
    if (!p) continue;
    const held = kind === 'improved' ? !point.accepted : point.unmatched > 0;
    if (!held) fresh += 1;
    const history = kind === 'improved' ? point.history : baselineTrails[index][c];
    if ($('show-trails').checked) {
      const line = history.map((h) => project(h.position)).filter(Boolean);
      if (line.length > 1) {
        g.lineWidth = 2.6; g.beginPath(); g.moveTo(...line[0]);
        line.slice(1).forEach((v) => g.lineTo(...v)); g.stroke();
      }
    }
    g.lineWidth = 2.4;
    g.beginPath(); g.arc(...p, 6.5, 0, 2 * Math.PI); g.stroke();
    if (held) {
      g.strokeStyle = '#fff'; g.lineWidth = 1.3;
      g.beginPath(); g.arc(...p, 10.5, 0, 2 * Math.PI); g.stroke();
    }
    // Label offsets affect text only; measurement/state coordinates stay exact.
    const offsets = [[10, 19], [11, -7], [9, -27], [-50, -9], [-51, 20]];
    const x = Math.max(2, Math.min(550, p[0] + offsets[c][0]));
    const y = Math.max(20, Math.min(475, p[1] + offsets[c][1]));
    g.font = '18px system-ui, sans-serif';
    g.lineWidth = 3; g.strokeStyle = '#000b'; g.strokeText(NAMES[c], x, y);
    g.fillStyle = color; g.fillText(NAMES[c], x, y);
  }
  $(`${kind}-count`).textContent = `${fresh} / 5 fresh`;
  canvas[kind].setAttribute('aria-label', `${kind === 'improved' ? 'New JavaScript' : 'Original'} tracker, recorded frame ${index + 1}, ${fresh} of five outputs fresh. Crosses are detections; double rings indicate held or unmatched states.`);
}

function draw() {
  if (!sequence) return;
  if (view !== 'improved') drawPanel('baseline');
  if (view !== 'baseline') drawPanel('improved');
  const f = sequence.frames[index];
  $('scrub').value = index;
  $('scrub').setAttribute('aria-valuetext', `Frame ${index + 1} of ${sequence.frames.length}, ${f.time_s.toFixed(2)} seconds`);
  $('frame-label').textContent = `Frame ${String(index + 1).padStart(3, '0')} / ${sequence.frames.length}`;
  $('time-label').textContent = `${f.time_s.toFixed(2)} / ${sequence.frames.at(-1).time_s.toFixed(2)} s`;
  for (let c = 0; c < 5; c += 1) {
    const state = states[index].fingers[c];
    const text = state.accepted ? (state.status === 'initialized' || state.status === 'reacquired' ? state.status : 'fresh') : state.status;
    $(`finger-state-${c}`).textContent = text;
  }
  $('previous').disabled = index === 0;
  $('next').disabled = index === sequence.frames.length - 1;
}

function setPlaying(next) {
  if (!sequence) return;
  cancelAnimationFrame(animation);
  playing = next;
  if (playing && index === sequence.frames.length - 1) index = 0;
  $('play').innerHTML = playing ? '<span aria-hidden="true">Ⅱ</span> Pause' : '<span aria-hidden="true">▶</span> Play';
  $('play').setAttribute('aria-label', `${playing ? 'Pause' : 'Play'} recorded sequence`);
  if (playing) {
    anchorWall = performance.now();
    anchorTime = sequence.frames[index].time_s;
    animation = requestAnimationFrame(tick);
  }
  draw();
}

function tick(wall) {
  if (!playing) return;
  const time = anchorTime + (wall - anchorWall) / 1000 * speed;
  let next = index;
  while (next + 1 < sequence.frames.length && sequence.frames[next + 1].time_s <= time) next += 1;
  if (next !== index) { index = next; draw(); }
  if (time >= endTime) { setPlaying(false); announce('Recording finished. Play to replay from the start.'); return; }
  animation = requestAnimationFrame(tick);
}

function seek(value) {
  setPlaying(false);
  index = Math.max(0, Math.min(sequence.frames.length - 1, value));
  draw();
}

$('play').addEventListener('click', () => setPlaying(!playing));
$('previous').addEventListener('click', () => seek(index - 1));
$('next').addEventListener('click', () => seek(index + 1));
$('scrub').addEventListener('input', (e) => seek(Number(e.target.value)));
$('speed').addEventListener('change', (e) => { speed = Number(e.target.value); if (playing) setPlaying(true); });
for (const id of ['show-trails', 'show-raw']) $(id).addEventListener('change', draw);
document.querySelectorAll('[data-view]').forEach((button) => {
  if (button.tagName !== 'BUTTON') return;
  button.addEventListener('click', () => {
    view = button.dataset.view;
    document.querySelectorAll('button[data-view]').forEach((b) => b.setAttribute('aria-pressed', String(b === button)));
    $('viewports').dataset.view = view;
    $('baseline-panel').hidden = view === 'improved';
    $('improved-panel').hidden = view === 'baseline';
    draw();
  });
});
document.addEventListener('visibilitychange', () => { if (document.hidden && playing) setPlaying(false); });
document.addEventListener('keydown', (e) => {
  if (!sequence || /INPUT|SELECT|TEXTAREA|BUTTON|SUMMARY|A/.test(e.target.tagName)) return;
  if (e.key === 'ArrowRight') { e.preventDefault(); seek(index + 1); }
  if (e.key === 'ArrowLeft') { e.preventDefault(); seek(index - 1); }
  if (e.code === 'Space') { e.preventDefault(); setPlaying(!playing); }
});

async function load() {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 15000);
  try {
    const url = new URL('./data/sequence.json', import.meta.url);
    const response = await fetch(url, { signal: controller.signal });
    if (!response.ok) throw new Error(`Sequence request returned HTTP ${response.status}.`);
    sequence = await response.json();
    validateSequence(sequence);
    states = trackSequence(sequence.frames, sequence.parameters);
    baselineTrails = baselineHistory(sequence.frames, sequence.parameters.trail_seconds);
    atlas = new Image();
    const imageReady = new Promise((resolve, reject) => {
      atlas.onload = resolve;
      atlas.onerror = () => reject(new Error('The recorded depth atlas could not be loaded.'));
    });
    atlas.src = new URL(sequence.atlas.path, url).href;
    await Promise.race([imageReady, new Promise((_, reject) => {
      const aborted = () => reject(new Error('Loading timed out. Check that both local data files are available.'));
      if (controller.signal.aborted) aborted();
      else controller.signal.addEventListener('abort', aborted, { once: true });
    })]);
    const requiredRows = Math.ceil(sequence.frames.length / sequence.atlas.columns);
    if (atlas.naturalWidth !== sequence.atlas.columns * 320 || atlas.naturalHeight < requiredRows * 240) throw new Error('Depth atlas dimensions do not match the recording.');
    $('clip-meta').textContent = `${sequence.frames.length} frames · ${sequence.frames.at(-1).time_s.toFixed(2)} s · 320 × 240`;
    $('scrub').max = sequence.frames.length - 1;
    $('scrub').disabled = false;
    $('play').disabled = false;
    for (let c = 0; c < 5; c += 1) {
      const box = document.createElement('div');
      box.className = 'finger-state'; box.style.setProperty('--finger', COLORS[c]);
      const name = document.createElement('strong'); name.textContent = NAMES[c];
      const state = document.createElement('span'); state.id = `finger-state-${c}`;
      box.append(name, state); $('finger-states').append(box);
    }
    const differences = sequence.frames.slice(1).map((f, i) => f.time_s - sequence.frames[i].time_s).sort((a, b) => a - b);
    const hold = differences[Math.floor(differences.length / 2)] || .13;
    endTime = sequence.frames.at(-1).time_s + hold;
    $('loading').hidden = true;
    $('player').setAttribute('aria-busy', 'false');
    document.documentElement.dataset.appState = 'ready';
    $('computation-label').textContent = 'Causal states computed locally';
    draw();
    announce('Recording ready. Play or scrub to compare tracking.');
  } catch (error) {
    document.documentElement.dataset.appState = 'error';
    sequence = null;
    $('error').hidden = false;
    $('error').textContent = `Unable to load the recording. ${error.message} Serve the web folder over HTTP, keeping data/ alongside this page, then reload.`;
    $('loading').hidden = true;
    $('viewports').hidden = true;
    $('player').setAttribute('aria-busy', 'false');
    $('clip-meta').textContent = 'Recording unavailable';
  } finally { clearTimeout(timeout); }
}

load();
