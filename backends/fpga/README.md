# Mojo V3 / XC6SLX9-2

The preserved image targets Mojo V3, XC6SLX9-2/TQG144, nominal50MHz, four interleaved pixel contexts and ten split decisions. This is per-pixel tree inference with a resident128×128 depth frame; it does not run the full detector/temporal finger tracker. Protocol4.0 provides model/frame upload, RUN counters and labels. See [core interface](rtl/INTERFACE.md) and [wire protocol](rtl/TRANSPORT.md).

Simulation requires Icarus Verilog, no board:

```sh
python3 backends/fpga/sim/run.py --all-fixtures --uart
```

Without flags it still checks one complete fixture and labels/heap paths, model/control guards, parser CRC/side effects, and actual-core transport. `--uart` additionally decodes real UART RTL and verifies14 captured responses using the production Python codec. All results are simulation evidence. Host tests and loader tests are entirely in-memory.

## Physical use: explicit commands only

No port discovery, auto-programming, reset or background serial polling occurs on import or validation. Substitute a verified Mojo application port for `DEVICE`. These commands perform actual device actions only when explicitly invoked:

```sh
python3 backends/fpga/host/client.py --port DEVICE --ping-only
# Upload model and one prepared frame to an already-running protocol4 application:
python3 backends/fpga/host/client.py --port DEVICE run-files --model backends/model/depth10 --depth backends/fixtures/historical_000/depth.bin --output labels.u8
# Optional volatile FPGA RAM programming of this reviewed image; no flash/AVR firmware write:
python3 backends/fpga/board/program_ram.py --port DEVICE --bin backends/fpga/images/mojo_depth10_50mhz.bin --sha256 ea23c4e4bf2cbde3a8e76b9678ddfef90315428adec3bf0e1992c1a998929c0f
```

pyserial is an optional explicit dependency. Loader uses manufacturer R/O/D acknowledgements, finite timeouts, paced63-byte writes and no automatic retries. Exact64-byte macOS writes previously exposed a legacy USB empty-packet stall; do not replace the pacing casually. Application uses500kbaud8N1 and asserts DTR once before opening. Never run concurrent owners on one port. Current packaging performed no physical device access.

The340,884B image and preserved physical labels were actually exercised before packaging on2026-11-14. Prior ISE14.7 routed timing: minimum17.055ns, setup2.945ns/hold0.298ns at20ns target, zero timing errors;4,284LUT,2,612FF,1,337slices,29RAMB16+2RAMB8,8DSP. Evidence includes source/image hashes. Nominal clock was not independently measured; no100/200MHz operation is claimed.

## Optional source rebuild

Provide your own licensed ISE14.7 environment; toolchains/licence files are not distributed. In a native filesystem, after sourcing its `settings64.sh`:

```sh
MAX_LEVELS=10 xtclsh backends/fpga/board/build_ise.tcl
```

Builds stay under ignored `fpga/build/levels10`. The script deletes only scoped disposable project/images and requires fresh successful process/timing reports with minimum period≤20ns and zero timing errors. Inspect resources, warnings and selected device/levels for every new build before programming. Current package source/simulation was validated; ISE and physical programming were not rerun merely for packaging. Exact vendor MIT notices are retained under `vendor/mojo-base-project/LICENSE.txt` and `board/LICENSE-EmbeddedMicro.txt`.
