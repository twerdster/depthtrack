import struct
import unittest
try:
    from .program_ram import load_ram
except ImportError:
    from program_ram import load_ram


class MockLoader:
    def __init__(self, replies=b"ROD"):
        self.dtr = True
        self.replies = bytearray(replies)
        self.writes = []
        self.output_cleared = False
        self.reads = []
    def reset_output_buffer(self):
        self.output_cleared = True
    def reset_input_buffer(self):
        pass
    def write(self, data):
        self.writes.append(data)
        return len(data)
    def flush(self):
        raise AssertionError("Loader must use device ACK barriers, not unbounded tcdrain")
    def read(self, count):
        self.reads.append(count)
        out = bytes(self.replies[:count]); del self.replies[:count]; return out


class FiniteUSBLoader:
    """Firmware-like R/size/raw/D transport with a finite asynchronous USB FIFO.

    No ACK exists during raw data. A virtual 64 KB/s receiver consumes only as
    simulated time advances; an unpaced sender eventually hits backpressure.
    """
    def __init__(self):
        self.dtr = True
        self.state = "command"
        self.replies = bytearray()
        self.queued = bytearray()
        self.received = bytearray()
        self.expected = None
        self.writes = []
        self.reads = []
        self.output_cleared = False
    def reset_output_buffer(self):
        self.output_cleared = True
        self.queued.clear()
    def reset_input_buffer(self):
        self.replies.clear()
    def write(self, data):
        self.writes.append(bytes(data))
        if self.state == "command":
            if data != b"R":
                raise AssertionError("Only the volatile RAM command is permitted")
            self.state = "size"
            self.replies.extend(b"R")
        elif self.state == "size":
            self.expected = struct.unpack("<I", data)[0]
            self.state = "data"
            self.replies.extend(b"O")
        else:
            if len(self.queued)+len(data) > 128:
                raise TimeoutError("Finite USB receive queue exhausted")
            self.queued.extend(data)
        return len(data)
    def pause(self, seconds):
        consume = min(len(self.queued), int(seconds*64000))
        self.received.extend(self.queued[:consume])
        del self.queued[:consume]
        if self.expected is not None and len(self.received) == self.expected and self.state == "data":
            self.state = "complete"
            self.replies.extend(b"D")
    def read(self, count):
        self.reads.append(count)
        result = bytes(self.replies[:count])
        del self.replies[:count]
        return result
    def flush(self):
        raise AssertionError("Unbounded flush must not be used")


class LegacyZLPLoader(FiniteUSBLoader):
    """Apple-CDC ZLP + old Mojo USB_Recv behavior, modelled from their sources.

    Exact64-byte writes receive an empty terminal packet. Once the preceding
    data drains, old USB_Recv sees zero bytes and never releases that bank.
    """
    def __init__(self):
        super().__init__()
        self.zlp_pending = False
        self.wedged = False
    def write(self, data):
        was_data = self.state == "data"
        result = super().write(data)
        if was_data and len(data) % 64 == 0:
            self.zlp_pending = True
        return result
    def pause(self, seconds):
        if self.wedged:
            return
        super().pause(seconds)
        if self.zlp_pending and not self.queued:
            self.wedged = True


class Tests(unittest.TestCase):
    def test_volatile_command_and_full_data(self):
        loader = MockLoader()
        data = bytes(range(255))
        load_ram(loader, data, pause=lambda _: None)
        self.assertEqual(loader.writes[:2], [b"R", struct.pack("<I", len(data))])
        self.assertEqual(b"".join(loader.writes[2:]), data)
        self.assertTrue(all(1 <= len(chunk) <= 63 for chunk in loader.writes[2:]))
        self.assertTrue(loader.output_cleared)
        self.assertEqual(loader.reads, [1, 1, 1])
    def test_rejected_loader_stops_before_payload(self):
        loader = MockLoader(b"X")
        with self.assertRaises(RuntimeError):
            load_ram(loader, b"test", pause=lambda _: None)
        self.assertEqual(loader.writes, [b"R"])
    def test_full_artifact_succeeds_with_finite_receiver_and_pacing(self):
        loader = FiniteUSBLoader()
        data = (bytes(range(256))*1332)[:340884]
        events = []
        load_ram(loader, data, pause=loader.pause,
                 progress=lambda stage, offset, total: events.append((stage, offset, total)))
        self.assertEqual(loader.received, data)
        self.assertFalse(loader.queued)
        self.assertTrue(loader.output_cleared)
        self.assertEqual(loader.reads, [1, 1, 1])
        self.assertEqual(events[0], ("loader_ready", 0, len(data)))
        self.assertEqual(events[-1], ("device_acknowledged", len(data), len(data)))
    def test_unpaced_backpressure_stops_at_known_offset_without_retry(self):
        loader = FiniteUSBLoader()
        with self.assertRaisesRegex(RuntimeError, "host offset 126/1024") as caught:
            load_ram(loader, bytes(1024), pause=loader.pause, chunk_delay=0)
        self.assertIsInstance(caught.exception.__cause__, TimeoutError)
        self.assertEqual(len(loader.writes), 5)  # R, size, two full chunks, one failed attempt.
        self.assertEqual(loader.reads, [1, 1])
    def test_source_based_zlp_bug_reproduced_by_exact64_writes(self):
        loader = LegacyZLPLoader()
        loader.write(b"R"); self.assertEqual(loader.read(1), b"R")
        loader.write(struct.pack("<I", 1024)); self.assertEqual(loader.read(1), b"O")
        loader.write(bytes(64)); loader.pause(0.001)
        self.assertTrue(loader.wedged)
        loader.write(bytes(64)); loader.write(bytes(64))
        with self.assertRaises(TimeoutError):
            loader.write(bytes(64))
        self.assertEqual(loader.read(1), b"")
    def test_full_artifact_avoids_legacy_zlp_and_preserves_every_byte(self):
        loader = LegacyZLPLoader()
        data = (bytes(range(256))*1332)[:340884]
        events = []
        load_ram(loader, data, pause=loader.pause,
                 progress=lambda *event: events.append(event))
        self.assertEqual(loader.received, data)
        self.assertFalse(loader.wedged)
        self.assertEqual(loader.state, "complete")
        self.assertEqual(loader.reads, [1, 1, 1])
        self.assertTrue(all(len(chunk) % 64 != 0 for chunk in loader.writes[2:]))
        self.assertGreater(len([event for event in events if event[0] == "host_queued"]), 80)
    def test_size_rejection_sends_no_configuration_payload(self):
        loader = MockLoader(b"RX")
        with self.assertRaisesRegex(RuntimeError, "expected b'O'"):
            load_ram(loader, bytes(1024), pause=lambda _: None)
        self.assertEqual(loader.writes, [b"R", struct.pack("<I", 1024)])
    def test_missing_final_ack_is_not_success(self):
        loader = MockLoader(b"RO")
        events = []
        with self.assertRaisesRegex(RuntimeError, "expected b'D'"):
            load_ram(loader, bytes(1024), pause=lambda _: None,
                     progress=lambda *event: events.append(event))
        self.assertFalse(any(event[0] == "device_acknowledged" for event in events))
    def test_bad_parameters_stop_before_loader_entry(self):
        for delay in (-1, 0.101, float("nan"), float("inf")):
            loader = MockLoader()
            with self.assertRaises(ValueError):
                load_ram(loader, b"test", pause=lambda _: None, chunk_delay=delay)
            self.assertFalse(loader.writes)
            self.assertFalse(loader.output_cleared)
        loader = MockLoader()
        with self.assertRaises(ValueError):
            load_ram(loader, b"", pause=lambda _: None)
        self.assertFalse(loader.output_cleared)


if __name__ == "__main__":
    unittest.main()
