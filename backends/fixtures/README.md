# Six prepared inference fixtures

`manifest.json` fixes order, binary types, byte counts and SHA256s. Every frame is128×128 uint16 little-endian millimetres; expected labels are uint8 and reached heap indices are uint16 little-endian,65535 for zero centres.

`historical_000` through `_004` are five recovered labeled depth examples, not a proven chronological stream. `stress_dense` is a fixed-seed fully occupied synthetic input, including boundary depths1 and65535. The fixture outputs were checked against a literal original-model C++ oracle, CPU implementations, Metal and RTL before packaging; preserved physical labels are independently stored in `../evidence/physical_labels/`. Physical transport does not return heap paths.

These are classifier regression fixtures, not a fingertip-tracking accuracy dataset. No calibration, real-time capture cadence or source-model training provenance is inferred. Numeric original records remain unchanged. The package validation checks all hashes before inference.
