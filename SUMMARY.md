# DepthTrack

Portable depth-tree inference and timestamp-aware 3D point tracking, extracted from Aaron Wetzler's restored ZScanV02/Mojo research demo on2026-11-15. The original research workspace is preserved; this repository is a self-contained extraction.

Scope: dependency-free C++17/C ABI tracking core, NumPy and JavaScript reference implementations, CPU/Metal/Spartan-6 inference backends, recorded output evidence, mobile browser replay and Swift/Kotlin-JNI integration examples. Platform claims distinguish executed tests, cross-compilation and untested device integration. No training provenance connecting recovered trees to HandNet is established. The separately trained Tea HandNet models have explicit provenance in the selected-model section below; packaging did not train or modify the historical trees/hardware.

## Executed validation

- Macarm64 and Linuxx86-64: native lifecycle/C-header tests and650 full-state/history golden checks pass; all six CPU inference fixtures match labels/heaps,98,304pixels per variant.
- MacMetal: actual GPU matches six fixtures; no CPU fallback. FPGA package: all six core fixtures, CRC/transport/UART/control tests and host/loader tests pass in simulation/memory. Preserved physical50MHz Mojo result/image evidence is historical; no board was accessed for packaging.
- iOSdevice and simulatorarm64 static libraries cross-compile with Xcode16.1/SDK18.1; Swift wrapper type-checks. Androidarm64/API24 JNI shared library cross-compiled successfully in clean-checkoutCI; neither mobile platform has an executed device app or capture pipeline.
- JavaScript:650 recorded states plus separate failure/motion cases,11tests pass. Browser play/pause/scrub/comparison and390×844responsive layout checked in desktopChrome. No physical mobile-browser execution is claimed.
- Optional OpenCV4 historical full-detector target compiles onMac. Its large original models remain external; the portable replay uses the bundled lossless sample and saved detector observations.

## Run and maintenance

Read README.md for per-platform commands. `cmake -S . -B build/native` builds the portable core. `python -m depthtrack replay samples/handmesh/tracking_fixture.json --verify` reproduces saved states after package installation. `node --test web/tests/*.test.mjs web/live/tests/*.test.mjs` checks browser parity and live numerical/lifecycle helpers. `python3 backends/validate.py --cpu --metal` checks actual desktop inference; drop`--metal` onLinux. Webroot is`web/`; GitHubPages deploys after desktop/Android CI checks.

Clean-checkout CI run 43717830988 passed Mac/Linux desktop and Android JNI jobs on source commit de06719f1be7e44b6dffb3ea612edfba01050e0f, including the new synthetic Tea forest and observation tests, native tracking, Python/JS, Linux RTL and iOS static cross-builds. Pages was skipped for this feature-branch run. The earlier successful run 43712008750 and its Pages deployment/HTTP check remain recorded in docs/validation-ci-history/. Public code: https://github.com/twerdster/depthtrack. Exact latest job URLs and scope are in docs/validation-ci.json.

The130-frame HandMeshRecord demo contains635 learned observations;634 are accepted and one jump is explicitly held. Common-frame P95 separation from the same noisy detections improves116.82→15.37mm, or28.94→11.86mm on originally fresh matches. These are not anatomical accuracy or measured latency. Sample/model training-set membership and independence are unknown. The seven-class ten-split FPGA prefix differs from the full-depth two/nine-class detector used for recording.

No private email, licence credentials, account inventories, full proprietary SDK/model archives or original robot-control entrypoint are included. Preserve component attribution/notices. HandNet integration pins this repository as a submodule and links output/platform documentation while retaining its historical MATLAB sources and terms.

## Native Tea forest integration, 2026-11-15

The Tea integration adds a dependency-free C++17 native forest loader and
`depthtrack_tea` CLI. It loads 8-byte feature-index nodes plus per-tree float32
`Coords.co`, uses declared F_CHAR 250/127 features with complete float32 coordinate
sums, averages normalized leaf posteriors and explicitly falls back to each
tree's root prior. Empty roots/malformed layouts are rejected. All decisions of
deep trees are evaluated with uint32 heap traces; header 19 means 20 decisions.

Optional calibrated posterior-to-XYZ component centroids and the existing tracker are
connected through explicit class IDs and real timestamps. This does not infer
anatomical labels or validate recognition accuracy. Mac executed tests include
an independent float32 fixture oracle, corrupt-input cases, empty-leaf handling,
weighted posteriors, calibrated missing-observation tracking and a complete
20-decision three-tree traversal. All six native CTests pass on Mac arm64 and
Linux x86-64 (GNU 11.4.0).
The independent Tea Python reference also matches the portable fixture's labels,
HWC probabilities and reached heaps byte for byte. Deep Metal/mobile/FPGA ports are not included.
Existing historical outputs/fixtures/model/hardware are preserved. See
`docs/tea-forest.md`; no new training or model accuracy is claimed by these tests.

Native inference also supports an explicit caller-provided centre mask or a
fixed independent depth-only `near-component` ROI (100..1500 mm, exact second
percentile, near + 150 mm, largest 4-connected component with support 30). ROI masks
restrict centres while feature neighbors retain original raw depth. Tests cover
this distinction, deterministic ROI ties/minimum support and an optional eighth
modeled background class distinct from the invalid-depth label. Background and
anatomical class semantics remain caller-declared and require held-out evaluation.

The observation path matches the selected Python detector: largest 4-connected
confident component, probability-weighted pixel XY and mean depth, then pinhole
projection. A five-class synthetic fixture distinguishes this from averaging
individual XYZ points and preserves missing outputs. Optional manifest
`postprocessing` defaults freeze foreground, confidence, minimum support and
class IDs; CLI overrides are tested. The method identifier is
`largest4connected-probability-weighted-centroid`.

The reusable `tools/validate_tea_handnet.py` checks supplied train/validation MAT
depths against the matching frozen Tea Python reference, with exact labels/heaps/
ROI, declared float32 posterior tolerance, calibrated observations and complete
tracker states/history. A local macOS harness run passed on three actual training
frames using an untrained synthetic fixture forest and synthetic 0.1 s timestamps.
Observed posterior error was zero; observation/track differences stayed below
2.28e-12 mm. This is harness validation, not trained-model accuracy, chronology,
latency or a final model check. No test data was opened; labels and sensor poses were not loaded or used.
Safe aggregate evidence is in `docs/tea-handnet-harness-validation.json`.

## Selected HandNet model integration — 2026-11-16

README and `docs/tea-forest.md` link Tea's selected three-tree ensemble and single
tree (seed 202), immutable archive hashes, final-results report and safe aggregate.
The models use 1200 features, eight classes including learned background, 20
decisions and manifest defaults foreground none / cutoff 0.25 / minimum support 3.
Training remains attributed to frozen Tea revision
`27ae728e9cb012d2255337d9b3583bbe7f66b3cc`; this documentation does not retrain or
change the runtime. The original models, browser recording and harness evidence
retain their separate historical/synthetic scope.

Full 2773-frame validation reports mean fingertip F1@15mm 0.3833437004586795 for
the ensemble and 0.3370599042841775 for the single. These are post-selection
validation results. The same frozen choices on all 10,000 untouched test frames
report F1@15mm 0.37494755678731473 and 0.32798765233655836 respectively. An
independent audit checked raw identities/separation, ground-truth counts and
reported localization metric arithmetic; it did not rerun classifier predictions.
Official random-frame splits do not establish subject independence. The Tea
final-results report/evidence links retain full denominators and limitations.

`docs/tea-handnet-selected-model-validation.json` records actual selected-artifact
native/Python parity on Linux x86-64 and Mac arm64: three training depths, both
foreground modes, byte-exact labels/posteriors/heaps/masks, maximum observation
and tracker difference 3.41e-13 mm, and exact Linux/Mac outputs. Synthetic 0.1 s
cadence is numerical/API evidence, not temporal accuracy or measured latency.
The public JSON excludes raw frame identities, input fingerprints, machine paths
and private runtime records. Native code and all earlier fixtures are unchanged.

## Live monocular experiment (2026-11-15)

`web/live/` now adds local browser camera/photo → Depth Anything V2 Small at294² → two-anchor approximate inverse-depth calibration → gated128² depth → the exact compact seven-class ten-split tree → largest-class-region XYZ candidates → shared causal tracking. Neural loading/inference lives in a terminable worker with visible progress/errors, GPU→CPU fallback and bounded waits; camera scheduling has one frame in flight and releases tracks on stop/hide/errors. No server receives frames. The existing recorded viewer remains independent of neural/CDN dependencies. Live route: https://twerdster.github.io/depthtrack/live/ ; operational/interpretation details: docs/monocular-live.md.

Model/runtime pins: Transformers.js3.8.1 and `onnx-community/depth-anything-v2-small` revision4472b7362082ad9968fee890ca0f1e5aca36b93d. Raw floating relative inverse depth is used, never the normalized uint8 visualization. Two measured anchors fit inverse metres; distances remain approximate and scene drift can invalidate calibration. The compact tree has no learned nonzero background, so manual ROI/range masks cannot establish that retained pixels are hands. Component centroids are new experimental observations, not restored full-detector fingertip modes. FOV is assumed60°. No training, FPGA changes or physical phone execution occurred.

Actual browser smoke on Mac M3 Max: raw294² finite output from real archived RGB hand images; CPUq8 first neural inference785.4ms; GPUfp16 first1184.1ms (compilation included), then repeated-image network-only calls16–17ms after warmup. These are development smoke measurements, not phone FPS or controlled camera/accuracy benchmarks. Fresh physical FaceTime capture succeeded and released tracks but was nearly black (0–6 gray range), so it provides no useful hand-detection evidence. Private capture/test data stays in ignored `out/`; no new camera imagery is published. Native browser/model caches remain on native storage because Chrome profiles require filesystem semantics beyond exFAT. Existing AgentSSD remained mounted with~1TiB free; no large original model archive was copied or trained.

Browser integration with an actual neural WASMq8 model and Chrome synthetic camera passed start→3frames→stop/release→stable count→restart→6frames→stop/release without page errors. This is synthetic-source integration, distinct from physical capture. Relative-depth hand illustration and sanitized machine-readable smoke evidence are in docs/media/monocular_relative_hand.png and docs/monocular-smoke.json. No monocular-tree semantic accuracy or physical phone FPS is established.

Local browser verification:75 Node tests pass, including exact tree/asset parity, calibration/component math, model/client/camera lifecycle and9 actual-app integration regressions for cancellation/reload/stale frames, calibrated photo tracking and invalid-cutoff recovery. Development Chrome390px responsive layout has no horizontal overflow. CI/Pages must be checked for the new source commit after publication.

Pagehide unloads the model, invalidates pending work and disables camera/photo input until an explicit reload, including back/forward-cache restoration on phone browsers. The app lifecycle regression checks this recovery.

## Combined integration review — 2026-11-16

The integration combines upstream main `de334f7e2a89bcb607c8142cb00756136b46b91b`
(live monocular browser experiment) with the native Tea adapter and selected-model
documentation. The live page retains its exact compact seven-class ten-split
assets; it does not load the selected eight-class, 20-decision HandNet forests.
Those forests use the native CPU API/CLI and retain separate held-out recognition
and numerical-parity evidence. No live-camera accuracy result follows from the
HandNet evaluation. Preserve both paths and their independent model provenance.

Combined local Mac arm64 CPU validation passed on 2026-11-16: a fresh Release
build and all six CTests (including deep Tea contracts and observation components),
Python tracker cases and the 130-frame/635-output replay, all 75 browser tests,
and both packed CPU variants with exact labels/heaps over six fixtures (98,304
pixels per variant). Live source/assets and its smoke evidence remain identical
to upstream main; native Tea source and the selected-model parity JSON are
unchanged. This merge check did not access cameras, download neural models, run
GPU/Metal inference or repeat mobile cross-builds. Combined clean-checkout CI
remains to be checked on the eventual published commit.

## Optional seed204 model documentation — 2026-11-16

`docs/tea-forest.md` adds the optional
Tea `handnet-200m-256-seed204-2026-11-16` artifact and its exact ZIP SHA256. This
single tree uses 256 saved features, 200,216,884 balanced samples, 181,979 training
frames and 20 decisions, with foreground none / cutoff 0.25 / support 3. The
original ensemble and seed-202 single remain the default examples. The comparison
is post-exposure exploratory engineering against matched original seed201/tree0;
sample count, features and coordinate/pixel seeds change together.

`docs/tea-handnet-seed204-validation.json` is an aggregate-only record of actual
Linux/Mac native/Python parity: three training depths, both foreground policies,
exact labels/posteriors/heaps/masks, maximum observation difference
4.760636329592671e-13 mm, maximum tracker state/history numeric difference
4.760636329592671e-13 across mixed-unit fields, and exact Linux/Mac outputs. Synthetic 0.1 s timestamps
establish numerical agreement only, not recognition accuracy, measured chronology,
tracking accuracy or latency. Public evidence contains no raw identities, input
fingerprints or private runtime paths. Training revision remains Tea 27ae728.

This additive update preserves native/browser code and all prior model evidence.
JSON, claim/hash consistency, relative links, privacy and diff checks cover these
files; native inference, browser, camera, GPU and dataset evaluations are not rerun
for this documentation change.
