# Synthetic Tea forest interoperability fixture

Three deterministic, untrained two-decision native Tea trees, seven anonymous
classes, one 4×3 uint16 millimetre frame. `forest.json` is the public manifest
contract. Coordinates are float32 row/column offsets; model nodes contain only
feature index and threshold. These tiny files test software arithmetic and
serialization, not recognition accuracy or training provenance.

Expected files use little-endian row-major labels, pixel-major HWC float32
probabilities, and `[pixel,tree]` uint32 heap indices. Invalid centre pixels have
background 7, zero probabilities and `UINT32_MAX` heaps. Tree weights are 1, 1, 2.
Tree 0 has a deliberately much larger compact maximum; normalized posterior
averaging must cancel this support scale. An empty terminal histogram uses that
tree's root prior. Missing/OOB neighbors and fractional offsets are included.

`tests/native/test_tea_forest.py` contains an independent standard-library
float32 oracle and checks these exact bytes. It also generates malformed models,
calibrated missing-observation/tracking cases and a complete 20-decision model
used as a three-tree forest. None are trained models.

`observations.depth.u16` and `observations.probabilities.f32` are a separate
hand-constructed 4×3 posterior fixture. At threshold .25 and support 3, the first
five requested classes have slots `[true,true,false,true,false]`.
`expected.observations.json` records calibrated XYZ. Varied depth within one
component distinguishes weighted pixel XY/mean Z projection from averaging
individual XYZ points. Native and Python compare within 1e-10 mm; this is
synthetic algorithm parity, not anatomical accuracy.
