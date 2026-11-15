# Restored full-depth detector baseline

This optional OpenCV4 C++ executable preserves the offline ZScanV02 depth→foreground→fingertip→original-tracker path used to produce the recorded observations. Camera SDK, serial/robot commands, GUI and gesture code are excluded. It remains separate from the improved portable tracker and the seven-class ten-split FPGA example.

```
cmake -S legacy/zscan -B build-zscan -DCMAKE_BUILD_TYPE=Release
cmake --build build-zscan -j
python3 tools/unpack_sample.py --output artifacts/depth.u16
build-zscan/depthtrack_zscan artifacts/depth.u16 130 \
  /path/to/HandClassifier/Tree_0000.tree \
  /path/to/TipCLassifier/Tree_0000.tree artifacts/zscan
```

Requires a C++17 compiler and OpenCV4 development headers/libraries (core,imgproc,video,features2d). On macOS, pass `-DOpenCV_DIR=/path/to/opencv/lib/cmake/opencv4` if CMake does not find the installation. On Debian/Ubuntu, the development package is `libopencv-dev`. The executable reads contiguous row-major uint16 millimetre depth,320×240. It uses the sample's recorded camera matrix and must be adapted for another depth camera; this is not a universal calibration.

Two archived trained models are required externally and are **not bundled**: foreground184,549,358bytes and parts243,269,607bytes. Expected SHA256s and binary headers are in `model-hashes.json`. Their original training dataset and overlap with HandNet/this recording are unverified. `Tree_0000.tree` is a historical tree format, not a neural-network checkpoint. Inclusive maxDepth20/21 means21/22 splits. Centre-depth-zero labels2/9 follow these two-class/nine-class models; the seven-class FPGA example separately returns background7.

The original Forest evaluator is isolated in `source-audit/forest_adapter.cpp`. The generated compatibility header adds its missing return only; feature arithmetic/histograms/branches remain unchanged. MeanShifter/Flooder/nanoflann/BinaryHeap and TrackedPoint headers retain original bytes/notices. Clock and unused Windows/OpenMP include shims are portability adapters. Offline caller restoration changes: all five classes enabled, strongest mode index0 accepted, invalid/missing seed guards, and aging with empty observations. Original Kalman initialization/self-measurement defects are retained here as the comparison baseline; use the portable improved tracker for new filtered output.

The recorded demo ran this path on macOS before extraction. Packaging additionally checks the optional target compiles where OpenCV4 is available. The portable sample replay and mobile viewer need no archived model or OpenCV installation.
