#!/usr/bin/env python3
"""Load a compiled Mojo .bin into volatile FPGA configuration RAM only.

Wire protocol follows the official embmicro/mojo-loader and mojo-arduino
sources. There is no flash erase/write operation in this client.
Only run after reviewing ISE resource/timing reports for this exact design.
"""
import argparse
import hashlib
import math
from pathlib import Path
import struct
import time


def expect(stream, value):
    got = stream.read(1)
    if got != value:
        raise RuntimeError(f"Loader acknowledgement: expected {value!r}, received {got!r}")


def load_ram(stream, data, pause=time.sleep, *, chunk_delay=0.001, progress=None):
    if not math.isfinite(chunk_delay) or not 0 <= chunk_delay <= 0.1:
        raise ValueError("Chunk delay must be between 0 and 100 ms")
    if not 1 <= len(data) <= 512*1024:
        raise ValueError("Invalid bitstream size")
    # Discard unsent host bytes from a prior failed attempt before explicitly
    # restarting the loader. Never retry a timed-out chunk: the driver may have
    # accepted a partial write, and the firmware has no chunk acknowledgements.
    stream.reset_output_buffer()
    # Official loader entry sequence: five DTR falling events within250ms.
    stream.dtr = False
    pause(0.005)
    for _ in range(5):
        stream.dtr = False; pause(0.005)
        stream.dtr = True; pause(0.005)
    stream.reset_input_buffer()
    if stream.write(b"R") != 1:
        raise RuntimeError("Short loader command write")
    expect(stream, b"R")
    if progress:
        progress("loader_ready", 0, len(data))
    size = struct.pack("<I", len(data))
    if stream.write(size) != len(size):
        raise RuntimeError("Short loader size write")
    expect(stream, b"O")
    if progress:
        progress("size_accepted", 0, len(data))
    next_progress = 4096
    # The legacy Mojo USB_Recv never releases an empty OUT packet. Apple's CDC
    # driver can append such a ZLP after an exact multiple of its64-byte maximum
    # packet size. Short writes plus pacing avoid that trigger without changing
    # the firmware or inserting bytes into the bitstream.
    for offset in range(0, len(data), 63):
        chunk = data[offset:offset+63]
        try:
            if stream.write(chunk) != len(chunk):
                raise RuntimeError("Short bitstream write")
        except Exception as error:
            raise RuntimeError(
                f"Bitstream USB write failed at host offset {offset}/{len(data)}; "
                "the driver may have accepted part of this chunk; stopped without retry"
            ) from error
        # The firmware has no per-chunk ACK. Limit host queue growth instead of
        # inventing an acknowledgement or retransmitting ambiguous writes.
        if chunk_delay:
            pause(chunk_delay)
        completed = offset+len(chunk)
        if progress and (completed >= next_progress or completed == len(data)):
            progress("host_queued", completed, len(data))
            next_progress = (completed//4096+1)*4096
    # R/O/D are actual device acknowledgements and provide consumption barriers.
    # Avoid tcdrain()/flush(): its blocking time is not bounded by write_timeout.
    expect(stream, b"D")
    if progress:
        progress("device_acknowledged", len(data), len(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--sha256", required=True, help="Expected hash of the reviewed build artifact")
    parser.add_argument("--chunk-delay-ms", type=float, default=1.0,
                        help="Pause after each <=63-byte USB chunk (default 1 ms; 0–100 ms)")
    args = parser.parse_args()
    data = args.bin.read_bytes()
    if args.bin.suffix.lower() != ".bin" or not 1024 <= len(data) <= 512*1024:
        parser.error("Expected a compiled Mojo .bin, 1–512KiB")
    actual = hashlib.sha256(data).hexdigest()
    if actual.lower() != args.sha256.lower():
        parser.error("Bitstream hash mismatch")
    if not math.isfinite(args.chunk_delay_ms) or not 0 <= args.chunk_delay_ms <= 100:
        parser.error("Chunk delay must be between 0 and 100 ms")
    import serial
    def report(stage, offset, total):
        print(f"Loader {stage}: {offset}/{total} host bytes", flush=True)
    # Match the manufacturer loader's CDC line coding. The AVR ignores this baud
    # value: configuration bytes use USB -> parallel FPGA bus, not its UART.
    with serial.Serial(args.port, 115200, timeout=10, write_timeout=10) as stream:
        load_ram(stream, data, chunk_delay=args.chunk_delay_ms/1000, progress=report)
    print(f"Loader completed volatile-RAM transfer: {len(data)} bytes, SHA256 {actual}")
    print("Verify tree application identity with backends/fpga/host/client.py --port DEVICE --ping-only next.")


if __name__ == "__main__":
    main()
