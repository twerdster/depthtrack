# DepthTrack workflow

Read SUMMARY.md then README.md before changes. Keep these documents current before commits. Preserve existing baseline result provenance and distinguish seven-class ten-split FPGA inference from the separate full-depth two/nine-class detector used for recorded finger tracking.

The portable tracker consumes timestamped XYZ observations in millimetres/seconds. Keep missing/held/lost states explicit. Compare implementations on identical causal sequences and retain uncertainty: detector-relative residuals are not anatomical ground-truth accuracy.

Build/test commands are in README.md and docs/native-platforms.md. Before commits, review SUMMARY.md and this file and update both when behavior, workflows, outputs or operating assumptions change. Preserve original/header and golden-state provenance when porting arithmetic or lifecycle policies.

- Native: CMake/Ctest,650 CSV and full JSON states/history, plus lifecycle/C-header checks.
- Python: install editable package and replay bundled fixture with`--verify`; run tests/python/test_tracker.py.
- Browser: node --test web/tests/*.test.mjs web/live/tests/*.test.mjs; serve`web/`overHTTP, check phone layout/play/pause/scrub and honest held/lost rendering. Do not substitute cached expected states for executed JavaScript output.
- Inference: backends/validate.py --cpu, optionally--metal onMac; FPGA simulation via backends/fpga/sim/run.py --all-fixtures --uart. These commands do not open serial or program boards.

Native mobile builds are distinct from device execution and camera acquisition. The supplied Swift/JNI APIs consume XYZ observations; no complete native mobile app or metric sensor-depth capture pipeline is bundled. The separate browser monocular experiment uses approximate two-anchor calibration and does not establish native capture or metric sensor accuracy. Do not claim mobile runtime, live depth capture, model training, or a full tracker on FPGA without execution evidence. No board programming is needed for ordinary tests. Keep large archived models external and preserve existing component notices; do not silently assign HandNet training provenance or its dataset license to the historical recording.

For new native Tea forests, follow `docs/tea-forest.md`. Build/test with the normal
CMake commands; `tea_forest_contracts` verifies independent float32/heap parity,
normalized tree voting, malformed/empty models, calibrated XYZ/tracker missing
states and a complete 20-decision three-tree model. Preserve the manifest's
coordinate precision, feature profile, class map and per-tree Coords.co. Do not
substitute the historical 200/126 profile or 24-byte ZScan node format. Keep new
deep CPU forest claims separate from the old ten-decision Metal/FPGA fixture.
Observations require verified class IDs, actual camera intrinsics and actual
strictly increasing timestamps. Default component thresholds need held-out tuning.
For foreground experiments, distinguish independently generated `near-component`
ROIs from caller-provided/oracle masks and learned background classes. ROI masks
select queried centres only; preserve raw depth at all feature neighbors. Keep
modeled background separate from invalid depth and unknown labels.

Before publishing updates, check the clean-checkout CI jobs (Mac/Linux, iOS static builds, Android JNI build) and Pages deployment. Record the checked source commit/job URLs when platform status changes; preserve previous immutable evidence records. Public browser demo is https://twerdster.github.io/depthtrack/. HandNet integration uses a pinned submodule gitlink; coordinate its update after upstream changes and verify recursive checkout.

Deployment postprocessing must match held-out model selection: largest
4-connected confident component, weighted pixel XY/mean Z, then pinhole XYZ.
Preserve optional manifest defaults and their method identifier; explicit CLI
overrides change the operating point and must be reported. Run
`tea_observation_components` as well as `tea_forest_contracts` for native parity.

For actual model integration, run `tools/validate_tea_handnet.py` with the
immutable selected deployment manifest and matching frozen Tea reference. It
must compare identical raw depths with unmodified neighbors in both foreground
conditions. Keep labels/heaps/ROI exact; record posterior error/tolerance and
diagnose any label disagreement rather than relaxing it. Check observation and
tracker frame/class/time correspondence, finite outputs and complete histories.
Declare synthetic models/timestamps explicitly; random split frames cannot prove
temporal accuracy. Keep runtime artifacts/data outside version control, preserve
historical CI records, and publish generic platform claims without machine paths.

## Selected HandNet model documentation

`docs/tea-forest.md` links the Tea `handnet-forest-2026-11-16` model archives and
their SHA256 values. Preserve each archive's relative manifest/assets and selected
20-decision, cutoff 0.25, minimum support 3, foreground-none defaults. Trained-model
CLI examples should use these manifest defaults rather than overriding them with
the generic .5 / support-5 defaults. Training provenance stays at Tea
`27ae728e9cb012d2255337d9b3583bbe7f66b3cc` when docs/packaging commits advance.

Keep `docs/tea-handnet-selected-model-validation.json` as a safe aggregate of
actual selected-model Linux/Mac CPU parity: three training frames, both foreground
modes, asset hashes, exact labels/posteriors/heaps/masks and declared geometry/
tracker tolerances. Exclude per-frame identities, raw input hashes, private paths
and runtime ledgers. Preserve the earlier untrained-fixture harness record.
Synthetic 0.1 s parity timestamps are not measured chronology, latency or temporal
accuracy; deep Metal/mobile/FPGA execution remains unestablished.

Full-validation results are descriptive after frozen 300-frame selection. Add
test metrics only after the independent audit and root review; do not choose a
new model/settings from test. Keep class/domain/denominators and the official
random-frame versus subject-independent distinction explicit. Verify linked Tea
results and release availability before publishing this documentation, then
coordinate the reviewed commit with HandNet's deliberate submodule-pin update.
The current guide records the completed 10,000-frame original test audit and
unchanged 20/.25/min3 selection. Distinguish independently reconstructed metric
arithmetic from rerunning predictions, and keep three-training-frame native
parity separate from held-out recognition results.

## Live monocular workflow

Live monocular work: read docs/monocular-live.md. `web/live/` is an experimental browser-only input path, distinct from recorded metric sensor depth and physical FPGA execution. Preserve pinned library/model revisions and assert actual `[1,3,294,294]` network input. Consume floating relative inverse depth; two-anchor inverse-depth calibration remains approximate. Never independently min/max-normalize frames into mm or silently reuse a previous camera session's anchors. Keep candidate class regions/approximate FOV and unverified anatomical accuracy explicit. Range masks are not hand segmentation. Camera/model failure paths must remain visible and cancellable; one in-flight frame, capture timestamps, stop/hide/late-permission cleanup and stale-result guards are required. Run `node --test web/tests/*.test.mjs web/live/tests/*.test.mjs`; maintain exact labels AND heap paths on all six packed-tree fixtures. Keep web/live/model assets identical to backends/model/depth10. Test real model execution separately from mocked lifecycle tests; distinguish repeated-image warm network timing, full camera throughput and physical phone runtime. Do not publish private webcam imagery or silently claim a dark/failed capture as useful detection evidence.

Preserve the evidence boundaries in docs/monocular-smoke.json and the relative-only hand illustration. Synthetic browser camera tests validate wiring/release, not a physical hand scene. App-level cancellation regression checks must keep cancelled asset loads/photo decodes from reviving or overwriting a new model session.

Before publishing live changes, run all75 browser tests (including9 app integration tests), preserve asset identity, and check hosted /live/ modules plus actual model inference. Recorded replay and platform build checks must continue to pass in CI.

Pagehide unloads the model, invalidates pending work and disables camera/photo input until an explicit reload, including back/forward-cache restoration on phone browsers. The app lifecycle regression checks this recovery.

## Combined model boundaries

Keep the upstream live monocular browser path and native Tea forest path intact
when integrating branches. `web/live/model/` stays identical to the compact
seven-class ten-split backend assets. Selected HandNet release forests are
eight-class, 20-decision CPU models loaded through the Tea manifest/API/CLI; do not
substitute them into the browser experiment or transfer held-out recognition
claims to monocular camera input. Validate the combined tree with native CTests,
Python replay/tests, CPU backend fixture parity and all 75 browser tests. Real
neural/browser smoke, GPU checks and device execution remain separately scoped
evidence; ordinary merge validation does not require camera access or model
downloads.

The 2026-11-16 combined local integration passed all six native CTests, all 75
browser tests, Python tracker/replay checks and both CPU backend fixture variants.
Preserve upstream real-model smoke as historical evidence; do not describe these
CPU/mock-based merge checks as fresh neural, camera or phone execution. Recheck
clean-checkout CI on the reviewed integration commit before publishing/pinning.
