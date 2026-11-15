# Browser replay

The page replays actual recorded HandMeshRecord depth and detector modes.
The new causal tracker runs in JavaScript from those raw XYZ observations and
camera timestamps. It does not request a camera or run depth-tree inference
inside the browser. The original pane displays saved recovered tracker output.

From the repository root on Mac or Linux:

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory web
```

Open **http://127.0.0.1:8000/** in a current browser. A Python interpreter is
needed only for this example static server. Any ordinary HTTP static host works;
there are no npm packages, build steps, CDNs or external fonts/scripts.
`file://` is not supported because browsers restrict ES-module and JSON loading.

For iOS Safari or Android Chrome, use the deployed static page supplied with
the repository, or serve this same folder from a computer reachable by the
phone. A phone's `localhost` refers to the phone, not to the computer. If
intentionally serving on a trusted local network, replace the bind address with
`0.0.0.0` and open `http://COMPUTER_LAN_ADDRESS:8000/` on the phone. This is a
local-network exposure chosen by the person running the command, not a tunnel
or Tailscale requirement. A normal HTTPS static host is the simplest portable
option. Actual iOS/Android hardware testing is still pending.

Once the page and two local data assets finish loading, play/pause, scrubbing,
speed changes and comparison perform no further network requests. This is
in-memory offline playback, not an installable/offline-reload service worker.
Pause happens automatically when the document becomes hidden. The page starts
paused and does not autoplay, including with reduced-motion preferences.

## Controls and computation

- Play/pause, previous/next frame and frame slider. Left/right arrows step when
  focus is on the page; Space toggles playback. Standard keyboard operation
  remains available on focused buttons, selects, checkboxes and the slider.
- New, original or comparison view; comparison stacks vertically on phones.
- Recorded timestamp spacing determines playback, with selectable 0.5×/1×/2×
  speed. Frames may be skipped visually if the browser is busy; tracker states
  still include every chronological observation.
- Toggle raw detections and 0.65-second trails. A double ring marks a held or
  unmatched output. The text readout reports the new tracker's current state.

`tracker.mjs` is DOM-free and can be imported in Node. On load, `trackSequence`
processes the recording from first frame to last using five separate tracker
instances. No update reads future observations. Precomputed causal states then
make scrubbing deterministic; seeking does not reinitialize or run backwards
through a filter. The optional Python `expected` states in the data are used by
tests only; the viewer never displays them instead of JavaScript results.

Input layout and projection are in `data/sequence.json`. The 10-column PNG atlas
contains 320×240 grayscale display tiles in chronological row-major order;
these are visualizations, not numeric depth values. The recording includes
130 frames spanning 17.583466 seconds. Depth/camera conventions and model
provenance limits are recorded in the sequence `source` metadata. Calibration
has not been independently revalidated, and a connection to HandNet training
has not been established. Detector agreement is not anatomical accuracy.

All URLs are relative, including the atlas relative to the JSON. The folder
works at a static-host subpath such as GitHub Pages' `/depthtrack/`.

## Executed checks

```sh
node --test web/tests/tracker.test.mjs
```

The tests compare every field of all 650 Python-reference finger states
(635 observations) and their histories. Seven additional Python-generated
failure/motion sequences contain 73 updates: nonzero stationary initialization,
missing/held/lost/reacquisition, one jump and return, confirmed relocation,
invalid depth, hold/reset boundaries and irregular movement. Additional tests
cover causal-prefix invariance, snapshot immutability, nonfinite points,
atomic timestamp rejection and parameter/input validation.

The recorded Python expected outputs are independent of the JavaScript port.
`tests/golden.json` records the Python reference source hash. To regenerate
the additional cases using that implementation:

```sh
python3 web/tests/generate_golden.py python/depthtrack/tracker.py
```

On the development Mac, Node tests pass with maximum recorded numeric deviation
zero. Chrome desktop and a **390×844 responsive emulation** were exercised with
play, pause, exact frame stepping and before/after switching. This does not
verify physical mobile devices, touch input, iOS WebKit, Android Chrome or
Linux graphical browsers. Those remain manual integration checks.

Source orientation used root `SUMMARY.md`, then root `SKILL.md`; no dependencies
were downloaded, device permissions changed, or live camera/serial opened.

## Live monocular experiment

The separate `/live/` page uses button-triggered camera/photo input, a pinned Depth Anything V2 Small model in a worker, approximate two-anchor inverse-depth calibration, the exact compact ten-split tree, and shared temporal tracking. It needs network access for runtime/model downloads and HTTPS/localhost for camera access. See [setup and interpretation](../docs/monocular-live.md). The recorded replay above remains independent of these dependencies.

Run all browser tests: `node --test web/tests/*.test.mjs web/live/tests/*.test.mjs`. Physical phone performance is unverified; the live page reports actual local inference time.
