# DepthTrack workflow

Read SUMMARY.md then README.md before changes. Keep these documents current before commits. Preserve existing baseline result provenance and distinguish seven-class ten-split FPGA inference from the separate full-depth two/nine-class detector used for recorded finger tracking.

The portable tracker consumes timestamped XYZ observations in millimetres/seconds. Keep missing/held/lost states explicit. Compare implementations on identical causal sequences and retain uncertainty: detector-relative residuals are not anatomical ground-truth accuracy.

Build/test commands are in README.md and docs/native-platforms.md. Before commits, review SUMMARY.md and this file and update both when behavior, workflows, outputs or operating assumptions change. Preserve original/header and golden-state provenance when porting arithmetic or lifecycle policies.

- Native: CMake/Ctest,650 CSV and full JSON states/history, plus lifecycle/C-header checks.
- Python: install editable package and replay bundled fixture with`--verify`; run tests/python/test_tracker.py.
- Browser: node --test web/tests/*.test.mjs; serve`web/`overHTTP, check phone layout/play/pause/scrub and honest held/lost rendering. Do not substitute cached expected states for executed JavaScript output.
- Inference: backends/validate.py --cpu, optionally--metal onMac; FPGA simulation via backends/fpga/sim/run.py --all-fixtures --uart. These commands do not open serial or program boards.

Native mobile builds are distinct from device execution and camera acquisition. The supplied Swift/JNI APIs consume XYZ observations; no complete mobile app or depth capture pipeline is bundled. Do not claim mobile runtime, live depth capture, model training, or a full tracker on FPGA without execution evidence. No board programming is needed for ordinary tests. Keep large archived models external and preserve existing component notices; do not silently assign HandNet training provenance or its dataset license to the historical recording.

Before publishing updates, check the clean-checkout CI jobs (Mac/Linux, iOS static builds, Android JNI build) and Pages deployment. Record the checked source commit/job URLs when platform status changes; preserve previous immutable evidence records. Public browser demo is https://twerdster.github.io/depthtrack/. HandNet integration uses a pinned submodule gitlink; coordinate its update after upstream changes and verify recursive checkout.
