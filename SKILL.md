# DepthTrack workflow

Read SUMMARY.md then README.md before changes. Keep these documents current before commits. Preserve existing baseline result provenance and distinguish seven-class ten-split FPGA inference from the separate full-depth two/nine-class detector used for recorded finger tracking.

The portable tracker consumes timestamped XYZ observations in millimetres/seconds. Keep missing/held/lost states explicit. Compare implementations on identical causal sequences and retain uncertainty: detector-relative residuals are not anatomical ground-truth accuracy.

Build/test commands are in README.md and docs/native-platforms.md. Before commits, review SUMMARY.md and this file and update both when behavior, workflows, outputs or operating assumptions change. Preserve original/header and golden-state provenance when porting arithmetic or lifecycle policies.

- Native: CMake/Ctest,650 CSV and full JSON states/history, plus lifecycle/C-header checks.
- Python: install editable package and replay bundled fixture with`--verify`; run tests/python/test_tracker.py.
- Browser: node --test web/tests/*.test.mjs; serve`web/`overHTTP, check phone layout/play/pause/scrub and honest held/lost rendering. Do not substitute cached expected states for executed JavaScript output.
- Inference: backends/validate.py --cpu, optionally--metal onMac; FPGA simulation via backends/fpga/sim/run.py --all-fixtures --uart. These commands do not open serial or program boards.

Native mobile builds are distinct from device execution and camera acquisition. The supplied Swift/JNI APIs consume XYZ observations; no complete mobile app or depth capture pipeline is bundled. Do not claim mobile runtime, live depth capture, model training, or a full tracker on FPGA without execution evidence. No board programming is needed for ordinary tests. Keep large archived models external and preserve existing component notices; do not silently assign HandNet training provenance or its dataset license to the historical recording.

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
