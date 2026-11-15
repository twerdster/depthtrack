# DepthTrack

Portable depth-tree inference and timestamp-aware 3D point tracking, extracted from Aaron Wetzler's restored ZScanV02/Mojo research demo on2026-11-15. The original research workspace is preserved; this repository is a self-contained extraction.

Scope: dependency-free C++17/C ABI tracking core, NumPy and JavaScript reference implementations, CPU/Metal/Spartan-6 inference backends, recorded output evidence, mobile browser replay and Swift/Kotlin-JNI integration examples. Platform claims distinguish executed tests, cross-compilation and untested device integration. No training provenance connecting recovered trees to HandNet is established. No training or hardware modifications occurred during packaging.

## Executed validation

- Macarm64 and Linuxx86-64: native lifecycle/C-header tests and650 full-state/history golden checks pass; all six CPU inference fixtures match labels/heaps,98,304pixels per variant.
- MacMetal: actual GPU matches six fixtures; no CPU fallback. FPGA package: all six core fixtures, CRC/transport/UART/control tests and host/loader tests pass in simulation/memory. Preserved physical50MHz Mojo result/image evidence is historical; no board was accessed for packaging.
- iOSdevice and simulatorarm64 static libraries cross-compile with Xcode16.1/SDK18.1; Swift wrapper type-checks. Native Android build is configured for CI; neither mobile platform has an executed device app or capture pipeline.
- JavaScript:650 recorded states plus separate failure/motion cases,11tests pass. Browser play/pause/scrub/comparison and390×844responsive layout checked in desktopChrome. No physical mobile-browser execution is claimed.
- Optional OpenCV4 historical full-detector target compiles onMac. Its large original models remain external; the portable replay uses the bundled lossless sample and saved detector observations.

## Run and maintenance

Read README.md for per-platform commands. `cmake -S . -B build/native` builds the portable core. `python -m depthtrack replay samples/handmesh/tracking_fixture.json --verify` reproduces saved states after package installation. `node --test web/tests/*.test.mjs` checks browser parity. `python3 backends/validate.py --cpu --metal` checks actual desktop inference; drop`--metal` onLinux. Webroot is`web/`; GitHubPages deployment is configured after desktop/Android CI checks.

The130-frame HandMeshRecord demo contains635 learned observations;634 are accepted and one jump is explicitly held. Common-frame P95 separation from the same noisy detections improves116.82→15.37mm, or28.94→11.86mm on originally fresh matches. These are not anatomical accuracy or measured latency. Sample/model training-set membership and independence are unknown. The seven-class ten-split FPGA prefix differs from the full-depth two/nine-class detector used for recording.

No private email, licence credentials, account inventories, full proprietary SDK/model archives or original robot-control entrypoint are included. Preserve component attribution/notices. HandNet integration pins this repository as a submodule and links output/platform documentation while retaining its historical MATLAB sources and terms.
