# Recorded depth tracking fixture

130 ordered320×240 HandMeshRecord frames spanning17.583466s, with635 image-derived fingertip observations. Source recording: Aaron Wetzler's recovered historical HandMeshRecord. It is a separate recording from the published HandNet release; any relationship to the archived detector's training corpus or to HandNet training/test splits is **unverified**.

`tracking_fixture.json` stores actual seconds, original learned detections, original mature tracker endpoints, and expected improved states for five fixed classes: thumb/index/middle/ring/pinky. Native CSV has650 rows, including15 missing initial observations. `sequence.json` is the matching browser data copy. Coordinates are camera millimetres; projection uses recorded INFO.KK_right under a source-supported axial-Z convention, not independent recalibration. No magnetic sensor positions or labels were used to generate the observations or improved tracking.

`depth.u16.gz` is the lossless numeric source depth: decompress to19,968,000bytes, shape130×240×320, little-endian uint16 millimetres, row-major. Expected SHA256 is `eae21f0a8da80edc96cd55468a5cd634dc9746415b50995f56640c3675816317`. `depth-atlas.png` is an8-bit contrast-rendered display sprite,10columns×13rows; it must not be used as numeric depth. The atlas and camera frames reveal the whole recorded depth view; they are not sensor-derived crops.

`comparison.json` records detector-relative improvements and their limits. `synthetic_validation.json` is a separate known-motion/noise test, not anatomical accuracy on the recording. The recorded outputs and full GIFs are in `../../docs/media/`. Original ZScanV02 detector model hashes and inference conventions are in `../../legacy/zscan/README.md`.

The sample is included to reproduce this explicitly scoped tracking demo. No dataset-wide accuracy, live camera acquisition, new HandNet training or held-out-split claim follows from it. Preserve attribution to Aaron Wetzler and the source/project provenance when reusing the fixture; this repository does not assign the HandNet dataset license to unrelated recovered recordings.
