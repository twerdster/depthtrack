# DepthTrack workflow

Read SUMMARY.md then README.md before changes. Keep these documents current before commits. Preserve existing baseline result provenance and distinguish seven-class ten-split FPGA inference from the separate full-depth two/nine-class detector used for recorded finger tracking.

The portable tracker consumes timestamped XYZ observations in millimetres/seconds. Keep missing/held/lost states explicit. Compare implementations on identical causal sequences and retain uncertainty: detector-relative residuals are not anatomical ground-truth accuracy.

Build/test commands are in README.md and docs/native-platforms.md. Before commits, review SUMMARY.md and this file and update both when behavior, workflows, outputs or operating assumptions change. Preserve original/header and golden-state provenance when porting arithmetic or lifecycle policies.

- Native: CMake/Ctest,650 CSV and full JSON states/history, plus lifecycle/C-header checks.
- Python: install editable package and replay bundled fixture with`--verify`; run tests/python/test_tracker.py.
- Browser: node --test web/tests/*.test.mjs web/live/tests/*.test.mjs; serve`web/`overHTTP, check phone layout/play/pause/scrub and honest held/lost rendering. Do not substitute cached expected states for executed JavaScript output.
- Inference: backends/validate.py --cpu, optionally--metal onMac; FPGA simulation via backends/fpga/sim/run.py --all-fixtures --uart. These commands do not open serial or program boards.

Native mobile builds are distinct from device execution and camera acquisition. The supplied Swift/JNI APIs consume XYZ observations; no complete mobile app or depth capture pipeline is bundled. Do not claim mobile runtime, live depth capture, model training, or a full tracker on FPGA without execution evidence. No board programming is needed for ordinary tests. Keep large archived models external and preserve existing component notices; do not silently assign HandNet training provenance or its dataset license to the historical recording.

Before publishing updates, check the clean-checkout CI jobs (Mac/Linux, iOS static builds, Android JNI build) and Pages deployment. Record the checked source commit/job URLs when platform status changes; preserve previous immutable evidence records. Public browser demo is https://twerdster.github.io/depthtrack/. HandNet integration uses a pinned submodule gitlink; coordinate its update after upstream changes and verify recursive checkout.

Live monocular work: read docs/monocular-live.md. `web/live/` is an experimental browser-only input path, distinct from recorded metric sensor depth and physical FPGA execution. Preserve pinned library/model revisions and assert actual `[1,3,294,294]` network input. Consume floating relative inverse depth; two-anchor inverse-depth calibration remains approximate. Never independently min/max-normalize frames into mm or silently reuse a previous camera session's anchors. Keep candidate class regions/approximate FOV and unverified anatomical accuracy explicit. Range masks are not hand segmentation. Camera/model failure paths must remain visible and cancellable; one in-flight frame, capture timestamps, stop/hide/late-permission cleanup and stale-result guards are required. Run `node --test web/tests/*.test.mjs web/live/tests/*.test.mjs`; maintain exact labels AND heap paths on all six packed-tree fixtures. Keep web/live/model assets identical to backends/model/depth10. Test real model execution separately from mocked lifecycle tests; distinguish repeated-image warm network timing, full camera throughput and physical phone runtime. Do not publish private webcam imagery or silently claim a dark/failed capture as useful detection evidence.

Preserve the evidence boundaries in docs/monocular-smoke.json and the relative-only hand illustration. Synthetic browser camera tests validate wiring/release, not a physical hand scene. App-level cancellation regression checks must keep cancelled asset loads/photo decodes from reviving or overwriting a new model session.

Before publishing live changes, run all75 browser tests (including9 app integration tests), preserve asset identity, and check hosted /live/ modules plus actual model inference. Recorded replay and platform build checks must continue to pass in CI.

Pagehide unloads the model, invalidates pending work and disables camera/photo input until an explicit reload, including back/forward-cache restoration on phone browsers. The app lifecycle regression checks this recovery.
