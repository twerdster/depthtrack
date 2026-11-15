"""Codec and stop/wait integration tests; no serial port or pyserial required."""
import binascii
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from types import SimpleNamespace
from unittest import mock
import tempfile
import unittest

try:
    from .client import (CodecError, ProtocolError, TransportError, TreeClient,
                         crc16_ccitt_false, decode_packet, encode_packet,
                         load_depth_file, load_model_files)
except ImportError:
    from client import (CodecError, ProtocolError, TransportError, TreeClient,
                        crc16_ccitt_false, decode_packet, encode_packet,
                        load_depth_file, load_model_files)


class MockBoard:
    """Byte-fragmented in-memory board; validates each actual wire packet."""

    def __init__(self, *, width=8, height=8, levels=10, read_fragment=2, write_fragment=None):
        self.width, self.height, self.levels = width, height, levels
        self.words = [0] * (width * height)
        self.loaded = self.nodes = self.leaves = 0
        self.node_data = [None] * ((1 << levels) - 1)
        self.leaf_data = [None] * (1 << levels)
        self.labels = None
        self.incoming = bytearray()
        self.outgoing = bytearray()
        self.writes, self.requests = [], []
        self.read_fragment, self.write_fragment = read_fragment, write_fragment
        self.transform = lambda packet: packet
        self.closed = False

    def close(self):
        self.closed = True

    def write(self, data):
        self.writes.append(bytes(data))
        count = min(len(data), self.write_fragment or len(data))
        self.incoming.extend(data[:count])
        if len(self.incoming) >= 4 and len(self.incoming) >= self.incoming[3] + 6:
            size = self.incoming[3] + 6
            command, seq, payload = decode_packet(self.incoming[:size], reply=False)
            del self.incoming[:size]
            self.requests.append((command, seq, payload))
            reply = encode_packet(command | 0x80, seq, self.handle(command, payload), reply=True)
            self.outgoing.extend(self.transform(reply))
        return count

    def read(self, count):
        count = min(count, self.read_fragment, len(self.outgoing))
        data = bytes(self.outgoing[:count])
        del self.outgoing[:count]
        return data

    def handle(self, command, payload):
        if command == 1:
            return b"\0" + struct.pack("<BBHHHHHBB", 4, 0, self.width, self.height,
                                        len(self.words), self.nodes, self.leaves, self.levels, 0)
        if command == 2:
            self.loaded = 0
            self.labels = None
            return b"\0"
        if command == 3:
            address = struct.unpack_from("<H", payload)[0]
            count = (len(payload) - 2) // 2
            if address > self.loaded or address + count > len(self.words) or (address < self.loaded < address + count):
                return b"\4"
            self.words[address:address + count] = struct.unpack(f"<{count}H", payload[2:])
            self.loaded = max(self.loaded, address + count)
            self.labels = None
            return b"\0" + struct.pack("<H", self.loaded)
        if command == 4:
            if len(payload) != 1 or not 1 <= payload[0] <= self.levels:
                return b"\3"
            if self.loaded != len(self.words):
                return b"\5"
            if self.nodes < (1 << payload[0]) - 1 or self.leaves < 1 << payload[0]:
                return b"\10"
            self.labels = bytes(1 if word else 7 for word in self.words)
            return b"\0" + struct.pack("<IIB", 123456, sum(word != 0 for word in self.words), 0)
        if command == 5:
            if self.labels is None:
                return b"\7"
            address, count = struct.unpack("<HB", payload)
            return b"\0" + struct.pack("<H", address) + self.labels[address:address + count]
        if command == 6:
            kind, address = struct.unpack_from("<BH", payload)
            count = (len(payload) - 3) // (12 if kind == 0 else 1)
            old = self.nodes if kind == 0 else self.leaves
            capacity = len(self.node_data) if kind == 0 else len(self.leaf_data)
            if address > old or address + count > capacity or (address < old < address + count):
                return b"\4"
            if kind == 0:
                self.nodes = max(old, address + count)
                self.node_data[address:address + count] = [payload[i:i + 12] for i in range(3, len(payload), 12)]
            else:
                self.leaves = max(old, address + count)
                self.leaf_data[address:address + count] = payload[3:]
            self.labels = None
            return b"\0" + struct.pack("<HH", self.nodes, self.leaves)
        if command == 7:
            self.nodes = self.leaves = 0
            self.labels = None
            return b"\0"
        return b"\2"


class CodecTests(unittest.TestCase):
    def test_actual_rtl_uart_capture_interoperability(self):
        fixture = json.loads((Path(__file__).parent / "fixtures" / "rtl_uart_v4.json").read_text())
        self.assertEqual(len(fixture["response_hex"]), 14)
        self.assertEqual(len(fixture["ping_cases"]), 4)
        for raw in fixture["response_hex"]:
            decode_packet(bytes.fromhex(raw))  # Every captured response CRC.
        for case in fixture["ping_cases"]:
            with self.subTest(sequence=case["sequence"]):
                packet = bytes.fromhex(fixture["response_hex"][case["response_index"]])
                board = MockBoard(width=128, height=128)
                board.transform = lambda _, packet=packet: packet
                client = TreeClient(transport=board, timeout=0.02, pace_seconds=0)
                client._sequence = case["sequence"]  # Replay alignment only.
                info = client.ping()
                self.assertEqual((info.major, info.minor, info.width, info.height,
                                  info.frame_words, info.node_count, info.leaf_count, info.max_levels),
                                 (4, 0, 128, 128, 16384, case["node_count"], case["leaf_count"], 10))

    def test_frozen_ping_wire_interoperability(self):
        fixture = json.loads((Path(__file__).parent / "fixtures" / "ping_v4.json").read_text())
        for case in fixture["replies"]:
            with self.subTest(levels=case["max_levels"]):
                board = MockBoard(width=128, height=128, levels=case["max_levels"])
                # The reply is a frozen independent transcript, not generated
                # by the codec under test. Check its complete request bytes too.
                board.transform = lambda _, hex_value=case["reply_hex"]: bytes.fromhex(hex_value)
                client = TreeClient(transport=board, timeout=0.02, pace_seconds=0)
                info = client.ping()
                self.assertEqual(b"".join(board.writes).hex(), fixture["request_hex"])
                self.assertEqual((info.width, info.height, info.frame_words, info.node_count,
                                  info.leaf_count, info.max_levels, info.caps),
                                 (128, 128, 16384, case["node_count"], case["leaf_count"], case["max_levels"], 1))

    def test_independent_crc_known_vector_and_all_byte_values(self):
        self.assertEqual(crc16_ccitt_false(b"123456789"), 0x29B1)
        data = bytes(range(256))
        self.assertEqual(crc16_ccitt_false(data), binascii.crc_hqx(data, 0xFFFF))

    def test_maximum_request_reply_sizes_and_crc_byte_order(self):
        packet = encode_packet(3, 255, bytes(range(66)))
        self.assertEqual(len(packet), 72)
        self.assertEqual(packet[-2:], struct.pack("<H", binascii.crc_hqx(packet[1:-2], 0xFFFF)))
        self.assertEqual(decode_packet(packet, reply=False), (3, 255, bytes(range(66))))
        reply = encode_packet(0x85, 0, bytes(range(67)), reply=True)
        self.assertEqual(len(reply), 73)
        self.assertEqual(decode_packet(reply)[2], bytes(range(67)))

    def test_rejects_corruption_magic_and_length(self):
        good = encode_packet(0x81, 0, b"\0", reply=True)
        for bad in (b"", good[:-1], b"\0" + good[1:], good[:-1] + bytes((good[-1] ^ 1,))):
            with self.subTest(bad=bad), self.assertRaises(CodecError):
                decode_packet(bad)
        with self.assertRaises(ValueError):
            encode_packet(3, 0, bytes(67))


class ClientTests(unittest.TestCase):
    def make_client(self, **board_options):
        board = MockBoard(**board_options)
        return board, TreeClient(transport=board, timeout=0.02, pace_seconds=0)

    def test_owned_port_asserts_dtr_once_before_open_and_preserves_rts(self):
        class SerialSpy:
            def __init__(self):
                self._dtr = self._rts = True  # pyserial defaults
                self.dtr_events, self.rts_events = [], []
                self.port, self.opened, self.closed = None, False, False
            @property
            def dtr(self): return self._dtr
            @dtr.setter
            def dtr(self, value):
                self.dtr_events.append(value)
                self._dtr = value
            @property
            def rts(self): return self._rts
            @rts.setter
            def rts(self, value):
                self.rts_events.append(value)
                self._rts = value
            def open(self):
                if not self.dtr or self.port != "/dev/mock-mojo":
                    raise AssertionError("DTR/port must be set before open")
                self.opened = True
            def close(self): self.closed = True
        port = SerialSpy()
        constructor = mock.Mock(return_value=port)
        with mock.patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=constructor)}):
            with TreeClient(port="/dev/mock-mojo"):
                self.assertTrue(port.opened)
                self.assertEqual(port.dtr_events, [True])
                self.assertEqual(port.rts_events, [])
                self.assertTrue(port.rts)
        self.assertTrue(port.closed)
        constructor.assert_called_once_with(port=None, baudrate=500000,
                                            timeout=0.1, write_timeout=3.0)

    def test_ping_and_sequence_wrap(self):
        board, client = self.make_client()
        for _ in range(258):
            info = client.ping()
            self.assertEqual((info.major, info.width, info.height, info.max_levels), (4, 8, 8, 10))
        self.assertEqual([request[1] for request in board.requests][-3:], [255, 0, 1])

    def test_full_upload_run_read_and_63_plus_9_split(self):
        board, client = self.make_client(width=16, height=8)
        nodes = [bytes((index,)) * 12 for index in range(31)]
        self.assertEqual(client.upload_model(nodes, bytes(range(32))), (31, 32))
        words = [0, 65535, 1, 5000] * 32
        self.assertEqual(client.upload_depth(words), 128)
        run = client.run(5)
        self.assertEqual((run.cycles, run.valid_pixels, run.core_error), (123456, 96, 0))
        self.assertEqual(client.read_labels(), bytes((7, 1, 1, 1)) * 32)
        self.assertTrue(all(len(part) <= 63 for part in board.writes))
        depth_writes = [p for c, _, p in board.requests if c == 3]
        self.assertEqual([len(p) for p in depth_writes], [66] * 4)
        split_sizes = [len(part) for part in board.writes]
        self.assertTrue(any(split_sizes[i:i + 2] == [63, 9] for i in range(len(split_sizes) - 1)))
        self.assertEqual(len([p for c, _, p in board.requests if c == 5]), 2)

    def test_partial_serial_writes_and_reads(self):
        board, client = self.make_client(write_fragment=7, read_fragment=1)
        self.assertEqual(client.upload_depth(bytes(range(128))), 64)
        self.assertFalse(board.incoming)

    def test_idempotent_overwrites_preserve_loaded_prefix(self):
        board, client = self.make_client()
        client.upload_depth(list(range(64)))
        self.assertEqual(client.write_depth(0, [65535]), 64)
        self.assertEqual(client.upload_depth([2] * 64, begin=False), 64)
        self.assertEqual(board.words, [2] * 64)

    def test_board_errors_and_result_invalidated(self):
        _, client = self.make_client()
        for operation, expected in ((lambda: client.run(4), 5),
                                    (lambda: client.read_labels(0, 1), 7),
                                    (lambda: client.write_depth(1, [1]), 4)):
            with self.subTest(expected=expected), self.assertRaises(ProtocolError) as caught:
                operation()
            self.assertEqual(caught.exception.status, expected)

    def test_rejects_bad_sequence_and_crc(self):
        board, client = self.make_client()
        board.transform = lambda packet: encode_packet(packet[1], (packet[2] + 1) & 255, packet[4:-2], reply=True)
        with self.assertRaisesRegex(CodecError, "sequence"):
            client.ping()
        board.transform = lambda packet: packet[:-1] + bytes((packet[-1] ^ 1,))
        with self.assertRaisesRegex(CodecError, "CRC"):
            client.ping()

    def test_idle_junk_before_magic(self):
        board, client = self.make_client()
        board.transform = lambda packet: b"\0\xffgarbage" + packet
        self.assertEqual(client.ping().major, 4)

    def test_timeout_and_failed_write_are_explicit(self):
        board, client = self.make_client()
        board.transform = lambda _: b""
        with self.assertRaises(TransportError):
            client.ping()
        board.write = lambda _: 0
        with self.assertRaises(TransportError):
            client.ping()

    def test_invalid_inputs_do_not_issue_writes(self):
        board, client = self.make_client()
        client.ping()
        previous = len(board.writes)
        for operation in (lambda: client.write_depth(0, b"x"),
                          lambda: client.write_depth(0, [-1]),
                          lambda: client.write_depth(0, [65536]),
                          lambda: client.upload_depth([1]),
                          lambda: client.upload_model([b"short"], b"\1"),
                          lambda: client.upload_model([12], b"\1"),
                          lambda: client.run(0),
                          lambda: client.run(11),
                          lambda: client.read_labels(64, 1)):
            with self.subTest(operation=operation), self.assertRaises(ValueError):
                operation()
        self.assertEqual(len(board.writes), previous)

    def test_lower_level_build_capacity_and_transport_ownership(self):
        board, client = self.make_client(levels=4)
        client.ping()
        with self.assertRaises(ValueError):
            client.run(5)
        with self.assertRaises(ValueError):
            client.upload_model([bytes(12)] * 31, bytes(32))
        client.close()
        self.assertFalse(board.closed)

    def test_ten_level_model_upload_and_wide_addresses(self):
        board, client = self.make_client()
        nodes = [struct.pack("<H", index) * 6 for index in range(1023)]
        leaves = bytes(index & 255 for index in range(1024))
        self.assertEqual(client.upload_model(nodes, leaves), (1023, 1024))
        self.assertEqual(board.node_data, nodes)
        self.assertEqual(bytes(board.leaf_data), leaves)
        info = client.ping()
        self.assertEqual((info.major, info.node_count, info.leaf_count, info.max_levels), (4, 1023, 1024, 10))
        for address in (255, 256):
            self.assertEqual(client.write_model(0, address, nodes[address]), (1023, 1024))
            self.assertEqual(client.write_model(1, address, leaves[address:address + 1]), (1023, 1024))
        self.assertEqual(client.write_model(1, 1023, b"\xff"), (1023, 1024))
        self.assertEqual(board.leaf_data[1023], 255)
        with self.assertRaises(ProtocolError) as caught:
            client.write_model(0, 1023, nodes[0])
        self.assertEqual(caught.exception.status, 4)
        model_payloads = [payload for command, _, payload in board.requests if command == 6]
        self.assertIn(b"\0\0\1" + nodes[256], model_payloads)
        self.assertIn(b"\1\xff\3\xff", model_payloads)
        self.assertTrue(all(len(p) in (15, 27, 39, 51, 63) for p in model_payloads if p[0] == 0))
        leaf_payloads = [p for p in model_payloads if p[0] == 1]
        self.assertEqual(max(map(len, leaf_payloads)), 66)
        self.assertTrue(all(len(part) <= 63 for part in board.writes))
        client.upload_depth([1000] * 64)
        self.assertEqual(client.run(10).valid_pixels, 64)
        self.assertEqual(client.read_labels(), b"\1" * 64)

    def test_model_prefix_order_straddles_and_overwrites(self):
        board, client = self.make_client()
        nodes = [bytes((i,)) * 12 for i in range(8)]
        self.assertEqual(client.write_model(0, 0, b"".join(nodes[:3])), (3, 0))
        with self.assertRaises(ProtocolError) as caught:
            client.write_model(0, 1, b"".join(nodes[1:6]))
        self.assertEqual(caught.exception.status, 4)
        self.assertEqual(board.nodes, 3)
        self.assertEqual(client.write_model(0, 1, nodes[1]), (3, 0))
        self.assertEqual(client.write_model(0, 3, b"".join(nodes[3:])), (8, 0))
        self.assertEqual(board.node_data[:8], nodes)
        for operation in (lambda: client.write_model(1, 0, bytes(64)),
                          lambda: client.write_model(0, 65536, bytes(12))):
            with self.assertRaises(ValueError):
                operation()

    def test_begin_and_model_begin_have_distinct_coverage(self):
        board, client = self.make_client()
        client.upload_model([bytes(12)] * 15, bytes(16))
        client.upload_depth([1000] * 64)
        client.run(4)
        client.begin()
        self.assertEqual((board.loaded, board.nodes, board.leaves), (0, 15, 16))
        with self.assertRaises(ProtocolError) as caught:
            client.read_labels(0, 1)
        self.assertEqual(caught.exception.status, 7)
        client.upload_depth([1000] * 64)
        client.model_begin()
        self.assertEqual((board.loaded, board.nodes, board.leaves), (64, 0, 0))
        with self.assertRaises(ProtocolError) as caught:
            client.run(4)
        self.assertEqual(caught.exception.status, 8)

    def test_every_supported_run_level_is_encoded_without_truncation(self):
        board, client = self.make_client()
        client.upload_model([bytes(12)] * 1023, bytes(1024))
        client.upload_depth([1] * 64)
        for levels in range(1, 11):
            self.assertEqual(client.run(levels).valid_pixels, 64)
        self.assertEqual([p for command, _, p in board.requests if command == 4],
                         [bytes((n,)) for n in range(1, 11)])


class FileInputTests(unittest.TestCase):
    def test_ten_level_exported_directory_and_13300_byte_binary(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            nodes_blob = b"".join(struct.pack("<H", i) * 6 for i in range(1023))
            leaves = bytes(i & 255 for i in range(1024))
            (root / "nodes.bin").write_bytes(nodes_blob)
            (root / "leaves.bin").write_bytes(leaves)
            (root / "model.json").write_text(json.dumps({"split_levels": 10}))
            compact = root / "compact.bin"
            compact.write_bytes(nodes_blob + leaves)
            self.assertEqual(compact.stat().st_size, 13300)
            from_directory = load_model_files(root)
            self.assertEqual(from_directory, load_model_files(compact))
            self.assertEqual((len(from_directory[0]), len(from_directory[1]), from_directory[2]), (1023, 1024, 10))
            for levels in (0, 11, -1):
                with self.assertRaises(ValueError):
                    load_model_files(root, levels)

    def test_compact_model_supports_all_levels_one_through_ten(self):
        with tempfile.TemporaryDirectory() as temporary:
            compact = Path(temporary) / "compact.bin"
            for levels in range(1, 11):
                compact.write_bytes(bytes(((1 << levels) - 1) * 12 + (1 << levels)))
                nodes, leaves, actual_levels = load_model_files(compact)
                self.assertEqual((len(nodes), len(leaves), actual_levels),
                                 ((1 << levels) - 1, 1 << levels, levels))

    def test_exported_directory_and_compact_model_agree(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            nodes = bytes(range(180))
            leaves = bytes(range(16))
            (root / "nodes.bin").write_bytes(nodes)
            (root / "leaves.bin").write_bytes(leaves)
            (root / "model.json").write_text(json.dumps({"split_levels": 4}))
            compact = root / "compact.bin"
            compact.write_bytes(nodes + leaves)
            self.assertEqual(load_model_files(root), load_model_files(compact))
            with self.assertRaises(ValueError):
                load_model_files(root, 5)
            compact.write_bytes(b"unrecognized historical format")
            with self.assertRaisesRegex(ValueError, "export original Tea"):
                load_model_files(compact)

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "NumPy optional")
    def test_numpy_uint16_endianness_and_invalid_values(self):
        import numpy as np
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "depth.npy"
            np.save(path, np.asarray([[1, 65535]], dtype=">u2"))
            self.assertEqual(load_depth_file(path), b"\1\0\xff\xff")
            for array in (np.asarray([1.5]), np.asarray([-1]), np.asarray([65536])):
                np.save(path, array)
                with self.assertRaises(ValueError):
                    load_depth_file(path)


@unittest.skipUnless(shutil.which("tclsh"), "Tcl optional; no ISE is invoked")
class BuildTimingGateTests(unittest.TestCase):
    def test_timing_summary_gate_fails_closed(self):
        build_script = Path(__file__).resolve().parents[1] / "board" / "build_ise.tcl"
        # Evaluate only definitions before the build's catch block. This cannot
        # run ISE commands, create build directories, or produce bitstreams.
        definitions = build_script.read_text().split("\nif {[catch {", 1)[0]
        cases = [
            ("0 timing errors detected.\nMinimum period is 20.000ns\n", True),
            ("0 timing errors detected.\nMinimum period: 19.500ns\n", True),
            ("1 timing error detected.\nMinimum period is 19.500ns\n", False),
            ("0 timing errors detected.\nMinimum period is 20.001ns\n", False),
            ("No parseable timing summary\n", False),
            ("0 timing errors detected.\n", False),
        ]
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / "gate.twr"
            quoted = str(report).replace("\\", "\\\\").replace("$", "\\$").replace("[", "\\[").replace('"', '\\"')
            script = definitions + '\nif {[catch {verify_timing_report "' + quoted + '"} failure]} {puts stderr $failure; exit 1}\nexit 0\n'
            for text, should_pass in cases:
                with self.subTest(text=text):
                    report.write_text(text)
                    result = subprocess.run([shutil.which("tclsh")], input=script,
                                            text=True, capture_output=True,
                                            env=dict(os.environ, MAX_LEVELS="10"))
                    self.assertEqual(result.returncode == 0, should_pass, result.stderr)


if __name__ == "__main__":
    unittest.main()
