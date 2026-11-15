# C++17 CPU inference

`depth_tree.hpp` exposes `depthtrack::PackedTree(nodes_bytes, leaves_bytes, levels=10)` and `predict(uint16_t* depth, uint8_t* labels, uint16_t* optional_heap_trace)`. Buffers are caller-owned,128×128 row-major; `predict` allocates nothing. Host depth values are uint16 millimetres. Files use little-endian encoding. The file adapter decodes explicitly; the benchmark's recovered fixed-ABI records require a little-endian host (tested ARM64/x86-64).

```sh
python3 backends/cpu/run_benchmark.py --validate-only
backends/_build/tree_cpu backends/model/depth10/nodes.bin backends/model/depth10/leaves.bin backends/fixtures/historical_000/depth.bin labels.u8 heapids.u16
```

`reference.py` provides the same packed integer semantics using Python's standard library. The C++ file CLI and both benchmark variants validate labels and heap indices on all98,304 fixture pixels.

`benchmark.cpp` preserves two implementations: literal float32 coordinate/feature arithmetic and seven-class reconstructed histogram voting; exact integer normalized coordinates with exported raw cuts and precomputed terminal labels. Optional timing:

```sh
python3 backends/cpu/run_benchmark.py --no-build --output backends/_build/cpu-benchmark
```

Each variant runs30 excluded single-frame warmups then100 measured frames per fixture, one thread, no batching. Timed calls write labels only; heap validation is untimed. Model/file loading, allocation, comparison, hashing, output writes, camera and USB are excluded. Compiler flags are C++17,-O3,-ffp-contract=off,-fno-fast-math; auto-vectorization is permitted. Coordinate equivalence was checked against recovered literal arithmetic on these fixtures; this is not recognition accuracy or a universal floating-point equivalence claim on arbitrary inputs.
