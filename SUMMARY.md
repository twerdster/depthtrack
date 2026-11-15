# DepthTrack

Portable depth-tree inference and timestamp-aware 3D point tracking, extracted from Aaron Wetzler's restored ZScanV02/Mojo research demo on2026-11-15. The original research workspace is preserved; this repository is a self-contained extraction.

Scope: dependency-free C++17/C ABI tracking core, NumPy and JavaScript reference implementations, CPU/Metal/Spartan-6 inference backends, recorded output evidence, mobile browser replay and Swift/Kotlin-JNI integration examples. Platform claims distinguish executed tests, cross-compilation and untested device integration. No training provenance connecting recovered trees to HandNet is established. No training or hardware modifications occurred during packaging.

## Executed validation

- Macarm64 and Linuxx86-64: native lifecycle/C-header tests and650 full-state/history golden checks pass; all six CPU inference fixtures match labels/heaps,98,304pixels per variant.
- MacMetal: actual GPU matches six fixtures; no CPU fallback. FPGA package: all six core fixtures, CRC/transport/UART/control tests and host/loader tests pass in simulation/memory. Preserved physical50MHz Mojo result/image evidence is historical; no board was accessed for packaging.
- iOSdevice and simulatorarm64 static libraries cross-compile with Xcode16.1/SDK18.1; Swift wrapper type-checks. Androidarm64/API24 JNI shared library cross-compiled successfully in clean-checkoutCI; neither mobile platform has an executed device app or capture pipeline.
- JavaScript:650 recorded states plus separate failure/motion cases,11tests pass. Browser play/pause/scrub/comparison and390×844responsive layout checked in desktopChrome. No physical mobile-browser execution is claimed.
- Optional OpenCV4 historical full-detector target compiles onMac. Its large original models remain external; the portable replay uses the bundled lossless sample and saved detector observations.

## Run and maintenance

Read README.md for per-platform commands. `cmake -S . -B build/native` builds the portable core. `python -m depthtrack replay samples/handmesh/tracking_fixture.json --verify` reproduces saved states after package installation. `node --test web/tests/*.test.mjs web/live/tests/*.test.mjs` checks browser parity and live numerical/lifecycle helpers. `python3 backends/validate.py --cpu --metal` checks actual desktop inference; drop`--metal` onLinux. Webroot is`web/`; GitHubPages deploys after desktop/Android CI checks.

Clean-checkout CI run43712008750 passed alljobs on sourcecommitb3b17e6219a93f0d915cb67565e710ee8278eda3, including Mac/Linux native/CPU, JS/Python, LinuxRTL, iOScross-builds and AndroidJNI. The run deployed https://twerdster.github.io/depthtrack/ (HTTP200 verified). Public code: https://github.com/twerdster/depthtrack. Exact job URLs/evidence scope are in docs/validation-ci.json; later documentation commits may have newer CI runs without changing the checked source bytes.

The130-frame HandMeshRecord demo contains635 learned observations;634 are accepted and one jump is explicitly held. Common-frame P95 separation from the same noisy detections improves116.82→15.37mm, or28.94→11.86mm on originally fresh matches. These are not anatomical accuracy or measured latency. Sample/model training-set membership and independence are unknown. The seven-class ten-split FPGA prefix differs from the full-depth two/nine-class detector used for recording.

No private email, licence credentials, account inventories, full proprietary SDK/model archives or original robot-control entrypoint are included. Preserve component attribution/notices. HandNet integration pins this repository as a submodule and links output/platform documentation while retaining its historical MATLAB sources and terms.

## Live monocular experiment (2026-11-15)

`web/live/` now adds local browser camera/photo → Depth Anything V2 Small at294² → two-anchor approximate inverse-depth calibration → gated128² depth → the exact compact seven-class ten-split tree → largest-class-region XYZ candidates → shared causal tracking. Neural loading/inference lives in a terminable worker with visible progress/errors, GPU→CPU fallback and bounded waits; camera scheduling has one frame in flight and releases tracks on stop/hide/errors. No server receives frames. The existing recorded viewer remains independent of neural/CDN dependencies. Live route: https://twerdster.github.io/depthtrack/live/ ; operational/interpretation details: docs/monocular-live.md.

Model/runtime pins: Transformers.js3.8.1 and `onnx-community/depth-anything-v2-small` revision4472b7362082ad9968fee890ca0f1e5aca36b93d. Raw floating relative inverse depth is used, never the normalized uint8 visualization. Two measured anchors fit inverse metres; distances remain approximate and scene drift can invalidate calibration. The compact tree has no learned nonzero background, so manual ROI/range masks cannot establish that retained pixels are hands. Component centroids are new experimental observations, not restored full-detector fingertip modes. FOV is assumed60°. No training, FPGA changes or physical phone execution occurred.

Actual browser smoke on Mac M3 Max: raw294² finite output from real archived RGB hand images; CPUq8 first neural inference785.4ms; GPUfp16 first1184.1ms (compilation included), then repeated-image network-only calls16–17ms after warmup. These are development smoke measurements, not phone FPS or controlled camera/accuracy benchmarks. Fresh physical FaceTime capture succeeded and released tracks but was nearly black (0–6 gray range), so it provides no useful hand-detection evidence. Private capture/test data stays in ignored `out/`; no new camera imagery is published. Native browser/model caches remain on native storage because Chrome profiles require filesystem semantics beyond exFAT. Existing AgentSSD remained mounted with~1TiB free; no large original model archive was copied or trained.

Browser integration with an actual neural WASMq8 model and Chrome synthetic camera passed start→3frames→stop/release→stable count→restart→6frames→stop/release without page errors. This is synthetic-source integration, distinct from physical capture. Relative-depth hand illustration and sanitized machine-readable smoke evidence are in docs/media/monocular_relative_hand.png and docs/monocular-smoke.json. No monocular-tree semantic accuracy or physical phone FPS is established.

Local browser verification:75 Node tests pass, including exact tree/asset parity, calibration/component math, model/client/camera lifecycle and9 actual-app integration regressions for cancellation/reload/stale frames, calibrated photo tracking and invalid-cutoff recovery. Development Chrome390px responsive layout has no horizontal overflow. CI/Pages must be checked for the new source commit after publication.

Pagehide unloads the model, invalidates pending work and disables camera/photo input until an explicit reload, including back/forward-cache restoration on phone browsers. The app lifecycle regression checks this recovery.
