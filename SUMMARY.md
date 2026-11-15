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

Read README.md for per-platform commands. `cmake -S . -B build/native` builds the portable core. `python -m depthtrack replay samples/handmesh/tracking_fixture.json --verify` reproduces saved states after package installation. `node --test web/tests/*.test.mjs` checks browser parity. `python3 backends/validate.py --cpu --metal` checks actual desktop inference; drop`--metal` onLinux. Webroot is`web/`; GitHubPages deploys after desktop/Android CI checks.

Clean-checkout CI run43712008750 passed alljobs on sourcecommitb3b17e6219a93f0d915cb67565e710ee8278eda3, including Mac/Linux native/CPU, JS/Python, LinuxRTL, iOScross-builds and AndroidJNI. The run deployed https://twerdster.github.io/depthtrack/ (HTTP200 verified). Public code: https://github.com/twerdster/depthtrack. Exact job URLs/evidence scope are in docs/validation-ci.json; later documentation commits may have newer CI runs without changing the checked source bytes.

The130-frame HandMeshRecord demo contains635 learned observations;634 are accepted and one jump is explicitly held. Common-frame P95 separation from the same noisy detections improves116.82→15.37mm, or28.94→11.86mm on originally fresh matches. These are not anatomical accuracy or measured latency. Sample/model training-set membership and independence are unknown. The seven-class ten-split FPGA prefix differs from the full-depth two/nine-class detector used for recording.

No private email, licence credentials, account inventories, full proprietary SDK/model archives or original robot-control entrypoint are included. Preserve component attribution/notices. HandNet integration pins this repository as a submodule and links output/platform documentation while retaining its historical MATLAB sources and terms.

## Native Tea forest integration, 2026-11-15

Worktree `/Users/aaron/Downloads/depthtrack-handnet-training-2026-11-15`, branch
`tea-handnet-forest`, adds a dependency-free C++17 native Tea forest loader and
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
Linux x86-64 (`aaron-MS-7E48`, user `aaron`, isolated source/build
`/home/aaron/depthtrack-handnet-native-validation-2026-11-15`, GNU 11.4.0).
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
