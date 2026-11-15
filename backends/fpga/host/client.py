#!/usr/bin/env python3
"""Mojo tree protocol v4. Importing this module never opens a serial port.

Requests and replies use CRC-16/CCITT-FALSE over cmd, seq, length, payload;
the CRC is transmitted least-significant byte first. No programming commands
or automatic retries are implemented. Hardware use requires an explicit port.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
from pathlib import Path
import struct
import time
from typing import Iterable

REQUEST_MAGIC = 0xA5
REPLY_MAGIC = 0x5A
MAX_REQUEST = 66
MAX_REPLY = 67
MIN_LEVELS = 1
MAX_LEVELS = 10
ERRORS = {
    1: "CRC error", 2: "unknown command", 3: "invalid length",
    4: "invalid address or write order", 5: "incomplete frame",
    6: "core error", 7: "no result", 8: "incomplete model",
}


class ProtocolError(RuntimeError):
    """The board rejected a request."""

    def __init__(self, status: int, command: int, payload: bytes):
        self.status, self.command, self.payload = status, command, payload
        super().__init__(f"command 0x{command:02x}: {ERRORS.get(status, 'status ' + str(status))}")


class CodecError(RuntimeError):
    """Malformed, mismatched, or corrupt wire response."""


class TransportError(RuntimeError):
    """An incomplete serial read/write, including a timeout."""


def crc16_ccitt_false(data: bytes) -> int:
    value = 0xFFFF
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ (0x1021 if value & 0x8000 else 0)) & 0xFFFF
    return value


def encode_packet(command: int, sequence: int, payload: bytes = b"", *, reply: bool = False) -> bytes:
    payload = bytes(payload)
    limit = MAX_REPLY if reply else MAX_REQUEST
    if not 0 <= command <= 255 or not 0 <= sequence <= 255:
        raise ValueError("command and sequence must be unsigned bytes")
    if len(payload) > limit:
        raise ValueError(f"payload exceeds {limit} bytes")
    body = bytes((command, sequence, len(payload))) + payload
    return bytes((REPLY_MAGIC if reply else REQUEST_MAGIC,)) + body + struct.pack("<H", crc16_ccitt_false(body))


def decode_packet(packet: bytes, *, reply: bool = True) -> tuple[int, int, bytes]:
    packet = bytes(packet)
    if len(packet) < 6:
        raise CodecError("packet is shorter than its header and CRC")
    magic = REPLY_MAGIC if reply else REQUEST_MAGIC
    if packet[0] != magic:
        raise CodecError("wrong packet magic")
    limit = MAX_REPLY if reply else MAX_REQUEST
    if packet[3] > limit or len(packet) != packet[3] + 6:
        raise CodecError("invalid packet length")
    actual = struct.unpack_from("<H", packet, len(packet) - 2)[0]
    if actual != crc16_ccitt_false(packet[1:-2]):
        raise CodecError("response CRC mismatch")
    return packet[1], packet[2], packet[4:-2]


@dataclass(frozen=True)
class PingInfo:
    major: int
    minor: int
    width: int
    height: int
    frame_words: int
    node_count: int
    leaf_count: int
    max_levels: int
    caps: int


@dataclass(frozen=True)
class RunResult:
    cycles: int
    valid_pixels: int
    core_error: int


class TreeClient:
    """One stop/wait transaction at a time; instances are not thread-safe.

    Pass ``transport=`` for an already-open pyserial-compatible stream, or an
    explicit ``port=`` to open pyserial. The client owns only ports it opens.
    Writes are split into at most 63-byte chunks to avoid the historical
    64-byte USB packet/ZLP issue. Pacing can be disabled for mocks with 0.
    """

    def __init__(self, port: str | None = None, *, transport=None,
                 baudrate: int = 500000, timeout: float = 3.0,
                 chunk_size: int = 63, pace_seconds: float = 0.001):
        if (port is None) == (transport is None):
            raise ValueError("provide exactly one of explicit port or transport")
        if timeout <= 0 or not 1 <= chunk_size <= 63 or pace_seconds < 0:
            raise ValueError("require positive timeout, chunk_size 1..63, and nonnegative pacing")
        self.timeout = timeout
        self.chunk_size = chunk_size
        self.pace_seconds = pace_seconds
        self._sequence = 0
        self._owns_transport = port is not None
        self.info: PingInfo | None = None
        if port is not None:
            try:
                import serial  # Optional dependency; not needed by codec/mock tests.
            except ImportError as exc:
                raise RuntimeError("serial use requires pyserial (python -m pip install pyserial)") from exc
            # The stock AVR CDC bridge requires DTR asserted for replies.
            # Set it once before opening; never send a loader/reset sequence.
            transport = serial.Serial(port=None, baudrate=baudrate,
                                      timeout=min(timeout, 0.1), write_timeout=timeout)
            transport.dtr = True
            # Preserve pyserial's normal RTS default.
            transport.port = port
            transport.open()
        self.transport = transport

    def close(self):
        if self._owns_transport:
            self.transport.close()
            self._owns_transport = False

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def _write_packet(self, packet: bytes):
        deadline = time.monotonic() + self.timeout
        for offset in range(0, len(packet), self.chunk_size):
            chunk = packet[offset:offset + self.chunk_size]
            sent = 0
            while sent < len(chunk):
                if time.monotonic() >= deadline:
                    raise TransportError("serial write timed out")
                count = self.transport.write(chunk[sent:])
                if not isinstance(count, int) or count <= 0 or count > len(chunk) - sent:
                    raise TransportError("serial transport did not accept a valid byte count")
                sent += count
            if self.pace_seconds and offset + len(chunk) < len(packet):
                time.sleep(self.pace_seconds)

    def _read_exact(self, count: int, deadline: float) -> bytes:
        result = bytearray()
        while len(result) < count:
            if time.monotonic() >= deadline:
                raise TransportError(f"serial read timed out ({len(result)}/{count} bytes)")
            fragment = self.transport.read(count - len(result))
            if not fragment:
                time.sleep(0.0001)
                continue
            if len(fragment) > count - len(result):
                raise TransportError("transport returned more bytes than requested")
            result.extend(fragment)
        return bytes(result)

    def request(self, command: int, payload: bytes = b"") -> bytes:
        """Return the successful response including its leading zero status."""
        sequence = self._sequence
        packet = encode_packet(command, sequence, payload)
        self._sequence = (sequence + 1) & 255
        self._write_packet(packet)
        deadline = time.monotonic() + self.timeout
        # Ignore a bounded amount of idle-line junk before the response magic.
        for _ in range(256):
            if self._read_exact(1, deadline)[0] == REPLY_MAGIC:
                break
        else:
            raise CodecError("no reply magic within 256 bytes")
        header = self._read_exact(3, deadline)
        if header[2] > MAX_REPLY:
            raise CodecError("response payload exceeds protocol limit")
        packet = bytes((REPLY_MAGIC,)) + header + self._read_exact(header[2] + 2, deadline)
        response_command, response_sequence, response = decode_packet(packet)
        if response_command != command | 0x80 or response_sequence != sequence:
            raise CodecError("response command or sequence does not match request")
        if not response:
            raise CodecError("response is missing status")
        if response[0]:
            raise ProtocolError(response[0], command, response)
        return response

    @staticmethod
    def _length(payload: bytes, expected: int):
        if len(payload) != expected:
            raise CodecError(f"expected {expected} response bytes, got {len(payload)}")

    def ping(self) -> PingInfo:
        payload = self.request(0x01)
        self._length(payload, 15)
        info = PingInfo(*struct.unpack("<BBHHHHHBB", payload[1:]))
        if info.major != 4:
            raise CodecError(f"expected protocol major 4, got {info.major}")
        if info.width * info.height != info.frame_words or info.frame_words == 0:
            raise CodecError("inconsistent or empty frame geometry in PING")
        if not MIN_LEVELS <= info.max_levels <= MAX_LEVELS:
            raise CodecError("unsupported maximum tree levels in PING")
        if info.node_count > (1 << info.max_levels) - 1 or info.leaf_count > 1 << info.max_levels:
            raise CodecError("model prefix counts exceed the advertised tree capacity")
        self.info = info
        return info

    def begin(self):
        payload = self.request(0x02)
        self._length(payload, 1)

    def write_depth(self, address: int, words: bytes | Iterable[int]) -> int:
        data = _depth_bytes(words)
        if not 0 <= address <= 65535 or not 2 <= len(data) <= 64:
            raise ValueError("depth write requires address 0..65535 and 1..32 words")
        payload = self.request(0x03, struct.pack("<H", address) + data)
        self._length(payload, 3)
        return struct.unpack_from("<H", payload, 1)[0]

    def upload_depth(self, words: bytes | Iterable[int], *, begin: bool = True) -> int:
        data = _depth_bytes(words)
        info = self.info or self.ping()
        if len(data) != info.frame_words * 2:
            raise ValueError(f"expected {info.frame_words} depth words, got {len(data) // 2}")
        if begin:
            self.begin()
        loaded = 0
        for offset in range(0, len(data), 64):
            loaded = self.write_depth(offset // 2, data[offset:offset + 64])
            # Idempotent overwrites may report a larger already-loaded prefix.
            if not offset // 2 + len(data[offset:offset + 64]) // 2 <= loaded <= info.frame_words:
                raise CodecError("board depth prefix did not cover the accepted block")
        return loaded

    def model_begin(self):
        payload = self.request(0x07)
        self._length(payload, 1)

    def write_model(self, kind: int, address: int, data: bytes) -> tuple[int, int]:
        data = bytes(data)
        if kind not in (0, 1) or not 0 <= address <= 65535:
            raise ValueError("model kind must be 0/1 and address unsigned 16-bit")
        if kind == 0 and (not data or len(data) % 12 or len(data) > 60):
            raise ValueError("node write requires 1..5 nodes of exactly 12 bytes")
        if kind == 1 and not 1 <= len(data) <= 63:
            raise ValueError("leaf write requires 1..63 label bytes")
        payload = self.request(0x06, struct.pack("<BH", kind, address) + data)
        self._length(payload, 5)
        return struct.unpack_from("<HH", payload, 1)

    def upload_model(self, nodes: Iterable[bytes], leaves: bytes, *, begin: bool = True) -> tuple[int, int]:
        nodes = list(nodes)
        if any(not isinstance(node, (bytes, bytearray, memoryview)) for node in nodes):
            raise ValueError("each encoded node must be a bytes-like value")
        nodes = [bytes(node) for node in nodes]
        leaves = bytes(leaves)
        if any(len(node) != 12 for node in nodes):
            raise ValueError("each encoded node must contain exactly 12 bytes")
        info = self.info or self.ping()
        if not 1 <= len(nodes) <= (1 << info.max_levels) - 1 or not 1 <= len(leaves) <= 1 << info.max_levels:
            raise ValueError("model counts exceed the advertised tree capacity")
        if begin:
            self.model_begin()
        counts = (0, 0)
        for offset in range(0, len(nodes), 5):
            block = nodes[offset:offset + 5]
            counts = self.write_model(0, offset, b"".join(block))
            if counts[0] < offset + len(block):
                raise CodecError("board node prefix did not cover the accepted block")
        for offset in range(0, len(leaves), 63):
            block = leaves[offset:offset + 63]
            counts = self.write_model(1, offset, block)
            if counts[1] < offset + len(block):
                raise CodecError("board leaf prefix did not cover the accepted block")
        return counts

    def run(self, levels: int) -> RunResult:
        if not isinstance(levels, int) or not MIN_LEVELS <= levels <= MAX_LEVELS:
            raise ValueError("levels must be an integer in 1..10")
        if self.info is not None and levels > self.info.max_levels:
            raise ValueError("levels exceed this build's advertised capacity")
        payload = self.request(0x04, bytes((levels,)))
        self._length(payload, 10)
        cycles, valid_pixels, core_error = struct.unpack("<IIB", payload[1:])
        result = RunResult(cycles, valid_pixels, core_error)
        if core_error:
            raise ProtocolError(6, 0x04, payload)
        return result

    def read_labels(self, address: int = 0, count: int | None = None) -> bytes:
        if count is None:
            info = self.info or self.ping()
            count = info.frame_words - address
        if not 0 <= address <= 65535 or not isinstance(count, int) or count < 0 or address + count > 65536:
            raise ValueError("label range is outside unsigned 16-bit addressing")
        if self.info is not None and address + count > self.info.frame_words:
            raise ValueError("label range exceeds the frame")
        result = bytearray()
        for offset in range(0, count, 64):
            size = min(64, count - offset)
            requested_address = address + offset
            payload = self.request(0x05, struct.pack("<HB", requested_address, size))
            self._length(payload, size + 3)
            if struct.unpack_from("<H", payload, 1)[0] != requested_address:
                raise CodecError("label response returned a different address")
            result.extend(payload[3:])
        return bytes(result)


def _depth_bytes(words: bytes | Iterable[int]) -> bytes:
    if isinstance(words, (bytes, bytearray, memoryview)):
        data = bytes(words)
        if len(data) % 2:
            raise ValueError("depth bytes must contain complete little-endian uint16 words")
        return data
    values = list(words)
    if any(not isinstance(value, int) or not 0 <= value <= 65535 for value in values):
        raise ValueError("depth samples must be integers in 0..65535")
    return struct.pack(f"<{len(values)}H", *values)


def load_depth_file(path: Path) -> bytes:
    """Read raw LE uint16 or a 1D/2D NumPy integer array without touching USB."""
    path = Path(path)
    if path.suffix.lower() != ".npy":
        data = path.read_bytes()
        return _depth_bytes(data)
    try:
        import numpy as np
    except ImportError as exc:
        raise RuntimeError("reading .npy depth files requires NumPy") from exc
    array = np.load(path, allow_pickle=False)
    if array.ndim not in (1, 2) or array.dtype.kind not in "ui":
        raise ValueError("depth .npy must be a 1D or 2D integer array")
    if array.size == 0 or int(array.min()) < 0 or int(array.max()) > 65535:
        raise ValueError("depth .npy values must fit unsigned 16-bit millimetres")
    return array.astype("<u2", copy=False).tobytes(order="C")


def load_model_files(path: Path, levels: int | None = None) -> tuple[list[bytes], bytes, int]:
    """Read an exported model directory/JSON, nodes.bin, or compact binary.

    A compact binary is explicitly defined here as all 12-byte nodes followed
    by all leaf bytes (13,300 bytes for ten levels). Large original
    Tea .tree files must first pass through the project's historical exporter.
    """
    if levels is not None and (type(levels) is not int or not MIN_LEVELS <= levels <= MAX_LEVELS):
        raise ValueError("model levels must be an integer in 1..10")
    path = Path(path)
    directory = path if path.is_dir() else path.parent
    if path.is_dir() or path.name in ("model.json", "nodes.bin"):
        metadata = directory / "model.json"
        if metadata.is_file():
            declared = json.loads(metadata.read_text()).get("split_levels")
            if type(declared) is not int or not MIN_LEVELS <= declared <= MAX_LEVELS:
                raise ValueError("model metadata must declare split_levels in 1..10")
            if levels is not None and declared != levels:
                raise ValueError("--levels conflicts with exported model metadata")
            levels = declared
        nodes_blob = (directory / "nodes.bin").read_bytes()
        leaves = (directory / "leaves.bin").read_bytes()
    else:
        blob = path.read_bytes()
        choices = [n for n in range(MIN_LEVELS, MAX_LEVELS + 1) if len(blob) == ((1 << n) - 1) * 12 + (1 << n)]
        if not choices:
            raise ValueError("model binary is not an exported 1..10-level compact model; export original Tea trees first")
        if levels is not None and levels != choices[0]:
            raise ValueError("--levels conflicts with compact model length")
        levels = choices[0]
        split = ((1 << levels) - 1) * 12
        nodes_blob, leaves = blob[:split], blob[split:]
    if levels is None:
        choices = [n for n in range(MIN_LEVELS, MAX_LEVELS + 1) if len(nodes_blob) == ((1 << n) - 1) * 12 and len(leaves) == 1 << n]
        if not choices:
            raise ValueError("model file sizes do not describe a complete 1..10-level tree")
        levels = choices[0]
    if len(nodes_blob) != ((1 << levels) - 1) * 12 or len(leaves) != 1 << levels:
        raise ValueError("model file sizes do not match its split levels")
    return [nodes_blob[i:i + 12] for i in range(0, len(nodes_blob), 12)], leaves, levels


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="explicit serial device; no discovery or auto-programming")
    parser.add_argument("--baudrate", type=int, default=500000)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--pace-ms", type=float, default=1.0)
    parser.add_argument("action", nargs="?", choices=("ping", "run-files"),
                        help="default: ping unless --model/--nodes requests a file run")
    parser.add_argument("--ping-only", action="store_true", help="read PING only; perform no uploads")
    parser.add_argument("--model", type=Path, help="exported model directory/JSON/nodes.bin or compact native binary")
    parser.add_argument("--depth", type=Path, help="raw LE uint16 depth .bin or integer .npy")
    parser.add_argument("--nodes", type=Path, help="alternative explicit raw 12-byte node file")
    parser.add_argument("--leaves", type=Path, help="alternative explicit leaf byte file")
    parser.add_argument("--levels", type=int, choices=range(MIN_LEVELS, MAX_LEVELS + 1))
    parser.add_argument("--output", type=Path, help="label byte output; required when --depth is supplied")
    args = parser.parse_args(argv)
    wants_run = args.action == "run-files" or args.model is not None or args.nodes is not None
    if args.ping_only and (wants_run or args.depth is not None):
        parser.error("--ping-only cannot be combined with model/depth/run arguments")
    if args.action == "ping" and wants_run:
        parser.error("ping cannot be combined with a model upload")
    if args.depth is not None and not wants_run:
        parser.error("--depth requires --model or --nodes/--leaves/--levels")
    if args.depth is not None and args.output is None:
        parser.error("--depth requires --output")
    if args.model is not None and (args.nodes is not None or args.leaves is not None):
        parser.error("choose --model or explicit --nodes/--leaves")
    # Read and validate files before opening an explicitly requested port.
    try:
        if wants_run:
            if args.model is not None:
                nodes, leaves, levels = load_model_files(args.model, args.levels)
            else:
                if args.nodes is None or args.leaves is None or args.levels is None:
                    parser.error("explicit model files require --nodes, --leaves, and --levels")
                levels = args.levels
                nodes_blob, leaves = args.nodes.read_bytes(), args.leaves.read_bytes()
                if len(nodes_blob) != ((1 << levels) - 1) * 12 or len(leaves) != 1 << levels:
                    raise ValueError("raw model file sizes do not match --levels")
                nodes = [nodes_blob[i:i + 12] for i in range(0, len(nodes_blob), 12)]
            depth = load_depth_file(args.depth) if args.depth is not None else None
        with TreeClient(port=args.port, baudrate=args.baudrate, timeout=args.timeout,
                        pace_seconds=args.pace_ms / 1000) as client:
            info = client.ping()
            output = {"ping": asdict(info)}
            if wants_run:
                if levels > info.max_levels:
                    raise ValueError("requested levels exceed this build's capacity")
                output["model_counts"] = client.upload_model(nodes, leaves)
                if depth is not None:
                    output["loaded_words"] = client.upload_depth(depth)
                    output["run"] = asdict(client.run(levels))
                    labels = client.read_labels()
                    args.output.write_bytes(labels)
                    output["label_bytes"] = len(labels)
                    output["labels_file"] = str(args.output)
            print(json.dumps(output, indent=2))
        return 0
    except (RuntimeError, ValueError, OSError) as exc:
        parser.exit(1, f"error: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
