# Actual Metal GPU inference

```sh
bash backends/gpu/build.sh
# inputs.u16: one or more concatenated128x128 uint16 little-endian frames
backends/_build/tree_metal --input inputs.u16 --nodes backends/model/depth10/nodes.bin --leaves backends/model/depth10/leaves.bin --output-dir new-results --levels 10 --warmup 30 --iterations 600
```

Or use `python3 backends/validate.py --metal` to build and check all packaged fixtures. macOS/Xcode command-line tools and a Metal GPU are required. The Swift executable compiles the embedded Metal kernel, traverses all ten decisions on the actual GPU, and has no CPU inference fallback. Model bytes are uploaded once; buffers are preallocated/shared, one frame in flight and one command buffer per frame.

An untimed pass writes `.labels.bin` and `.leafids.bin`. Every timed dispatch's labels must match its untimed output. `gpu_ms` uses completed command-buffer GPU timestamps and covers the traversal/label kernel. `host_input_to_labels_ms` includes copying32KiB input to shared GPU storage, encoding/submitting/waiting, and copying16KiB labels to preallocated CPU storage. Trace output, file I/O, library compilation, allocation, model upload, hashes, comparisons, capture and rendering are excluded from timing. Raw timestamps/samples and exact boundaries are in `benchmark.json`.

Choose a new/empty output directory. Keep input fixture order explicit; the package uses `fixtures/manifest.json` order. The12 measured dispatches in package validation are a stability check, not a controlled speed claim. See prior controlled comparison separately under `evidence/`.
