# CPU, Metal and Mojo depth-tree backends

These implementations classify prepared128×128 millimetre-depth frames using a recovered seven-class tree's **complete ten-split prefix**:1,023 internal nodes and1,024 terminal histograms. The original tree has depth21; this is not that entire tree, a forest, or the separate two/nine-class detector used by the recorded finger-tracking demo. Training and anatomical recognition accuracy are not established here.

Zero centre depth returns background7. Learned labels0..6 are thumb,index,middle,ring,pinky,palm,wrist;255 remains unknown/no positive vote. Coordinate casts occur after the complete base+offset/depth sum. Missing/OOB neighbors and terminal vote indexing retain the recovered implementation's behavior. The compact node/label representation is13,300B; an additional39,928B of bounded original records supports literal CPU validation. Large source trees, private recovery records, installers and credentials are excluded.

Run from the repository root:

```sh
python3 backends/validate.py --cpu
# macOS with Xcode command-line tools and a Metal GPU:
python3 backends/validate.py --cpu --metal
python3 -m unittest discover -s backends/fpga/host -p 'test_*.py'
python3 -m unittest discover -s backends/fpga/board -p 'test_*.py'
# Icarus Verilog on PATH; actual RTL simulation, no board required:
python3 backends/fpga/sim/run.py --all-fixtures --uart
```

Python3.10+ standard library and a C++17 compiler suffice for scalar/CPU tests; set `CXX` if needed. NumPy is optional for the client's .npy loader/test. Serial hardware commands require pyserial. Metal validation requires macOS/Xcode and fails if no GPU exists; it has no CPU fallback. Icarus tests require `iverilog` and `vvp`. Tcl enables the build-report gate unit test but is optional for ordinary inference. No installation occurs automatically.

Validation checks fixture hashes, scalar reference labels/heaps, preserved prior physical labels, both compiled CPU variants and optional actual Metal output. Six excluded warmup dispatches and12 bounded GPU stability samples are a packaging check, **not a new controlled performance comparison**. Generated executables/reports stay under ignored `_build/`; no prebuilt host executables are distributed.

- [CPU](cpu/README.md): reusable C++ header, file CLI and optional resident-buffer benchmark.
- [Metal](gpu/README.md): native GPU command-line runner and timing boundaries.
- [FPGA](fpga/README.md): RTL, simulation, optional ISE build, explicit physical commands.
- [Model](model/README.md), [fixtures](fixtures/README.md), [evidence](evidence/README.md), [notices](NOTICE.md).

All commands resolve assets relative to these files; the old restoration directory is unnecessary. The prior physical Mojo run and current package validation are recorded separately. No camera, board, account or training service is touched by ordinary validation.
