from concurrent.futures import ThreadPoolExecutor
from dataclasses import replace
from errno import EIO, ETIMEDOUT
from threading import Event
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from unittest.mock import patch

from diagnostic import CpuDiagnostic, Outcome
from simulated import SimulatedBackend
from transport import LG_GRAM_GOODIX, Message, Receipt, Transfer


SELECT = Message((Transfer(tx=bytes.fromhex("f0 00 a8")),))
READ = Message((Transfer(tx=bytes.fromhex("f1")), Transfer(rx_length=2)))
SELECTED = Receipt(0, 3)
REFERENCE = Receipt(0, 3, bytes.fromhex("00 a1"))


class DiagnosticTests(unittest.TestCase):
    def test_identity_rejects_mismatched_resources_and_profile(self):
        for changes in (
            {"hid": "OTHER"},
            {"acpi_path": r"\_SB.PCI0.SPI2.FPNT"},
            {"resource_source": r"\_SB.PCI0.SPI2"},
            {"controller_pci_id": "8086:ffff"},
            {"chip_select": 1},
            {"mode": 1},
            {"bits_per_word": 16},
            {"max_speed_hz": 5_000_000},
            {"active_low": False},
            {"four_wire": False},
        ):
            with self.subTest(changes=changes):
                identity = replace(LG_GRAM_GOODIX, **changes)
                backend = SimulatedBackend([], identity=identity)
                with self.assertRaises(ValueError):
                    CpuDiagnostic(backend)
                self.assertEqual(backend.messages, [])

    def test_reference_and_message_boundaries(self):
        backend = SimulatedBackend([SELECTED, REFERENCE])
        diagnostic = CpuDiagnostic(backend)
        self.assertEqual(backend.messages, [])
        result = diagnostic.read(timeout=5)
        self.assertEqual(result.outcome, Outcome.VALID_REFERENCE)
        self.assertEqual(result.receipts, (SELECTED, REFERENCE))
        # Each backend invocation is one CS window. F1 and RX share a window.
        self.assertEqual(backend.messages, [SELECT, READ])
        self.assertEqual([m.wire_length for m in backend.messages], [3, 3])

    def test_invalid_response_preserves_success_and_raw_bytes(self):
        for rx in (b"\xff\xff", b"\x00\xa2"):
            with self.subTest(rx=rx):
                receipt = Receipt(0, 3, rx)
                backend = SimulatedBackend([SELECTED, receipt])
                result = CpuDiagnostic(backend).read(timeout=5)
                self.assertEqual(result.outcome, Outcome.INVALID_RESPONSE)
                self.assertEqual(result.receipts, (SELECTED, receipt))

    def test_first_message_error_stops_before_read(self):
        failed = Receipt(-EIO, 1)
        backend = SimulatedBackend([failed])
        result = CpuDiagnostic(backend).read(timeout=5)
        self.assertEqual(result.outcome, Outcome.TRANSPORT_ERROR)
        self.assertEqual(result.receipts, (failed,))
        self.assertEqual(backend.messages, [SELECT])

    def test_second_message_error_preserves_phase_and_partial_rx(self):
        failed = Receipt(-EIO, 2, b"\x00")
        backend = SimulatedBackend([SELECTED, failed])
        result = CpuDiagnostic(backend).read(timeout=5)
        self.assertEqual(result.outcome, Outcome.TRANSPORT_ERROR)
        self.assertEqual(result.receipts, (SELECTED, failed))
        self.assertEqual(backend.messages, [SELECT, READ])

    def test_length_mismatch_in_either_phase(self):
        for script in (
            [Receipt(0, 2)],
            [SELECTED, Receipt(0, 2, b"\x00")],
            [SELECTED, Receipt(0, 3, b"\x00")],
            [SELECTED, Receipt(0, 4, b"\x00\xa1\x00")],
        ):
            with self.subTest(script=script):
                backend = SimulatedBackend(script)
                result = CpuDiagnostic(backend).read(timeout=5)
                self.assertEqual(result.outcome, Outcome.LENGTH_MISMATCH)
                self.assertEqual(result.receipts, tuple(script))
                self.assertEqual(backend.messages, [SELECT, READ][:len(script)])

    def test_backend_timeout(self):
        failed = Receipt(-ETIMEDOUT, 0)
        backend = SimulatedBackend([SELECTED, failed])
        result = CpuDiagnostic(backend).read(timeout=5)
        self.assertEqual(result.outcome, Outcome.TIMEOUT)
        self.assertEqual(result.receipts, (SELECTED, failed))

    def test_both_phases_share_deadline(self):
        deadlines = []
        backend = SimulatedBackend(
            [SELECTED, REFERENCE],
            before_reply=lambda phase, deadline, stopped: deadlines.append(deadline),
        )
        self.assertEqual(CpuDiagnostic(backend).read(timeout=5).outcome,
                         Outcome.VALID_REFERENCE)
        self.assertEqual(len(deadlines), 2)
        self.assertEqual(deadlines[0], deadlines[1])

    def test_timeout_between_phases(self):
        backend = SimulatedBackend([SELECTED])
        # Start, pre-select check, pre-read check, final check.
        with (
            patch("diagnostic.monotonic", side_effect=[10, 10, 11, 11]),
            patch("simulated.monotonic", return_value=10),
        ):
            result = CpuDiagnostic(backend).read(timeout=1)
        self.assertEqual(result.outcome, Outcome.TIMEOUT)
        self.assertEqual(backend.messages, [SELECT])

    def test_concurrent_reads_are_serialized_as_pairs(self):
        entered, release, competing = Event(), Event(), Event()

        def hold_first(phase, deadline, stopped):
            if phase == 1:
                entered.set()
                self.assertTrue(release.wait(3))

        backend = SimulatedBackend([SELECTED, REFERENCE] * 2, before_reply=hold_first)
        diagnostic = CpuDiagnostic(backend)

        def second_read():
            competing.set()
            return diagnostic.read(timeout=5)

        with ThreadPoolExecutor(max_workers=2) as pool:
            first = pool.submit(diagnostic.read, timeout=5)
            try:
                self.assertTrue(entered.wait(3))
                second = pool.submit(second_read)
                self.assertTrue(competing.wait(3))
                self.assertEqual(backend.messages, [SELECT])
            finally:
                release.set()
            self.assertEqual(first.result(3).outcome, Outcome.VALID_REFERENCE)
            self.assertEqual(second.result(3).outcome, Outcome.VALID_REFERENCE)
        self.assertEqual(backend.messages, [SELECT, READ, SELECT, READ])

    def test_cancel_active_is_not_sticky(self):
        entered = Event()

        def wait_for_cancel(phase, deadline, stopped):
            if phase == 1:
                entered.set()
                self.assertTrue(stopped.wait(3))

        backend = SimulatedBackend([SELECTED, SELECTED, REFERENCE],
                                   before_reply=wait_for_cancel)
        diagnostic = CpuDiagnostic(backend)
        self.assertFalse(diagnostic.cancel())
        with ThreadPoolExecutor(max_workers=1) as pool:
            pending = pool.submit(diagnostic.read, timeout=5)
            self.assertTrue(entered.wait(3))
            self.assertTrue(diagnostic.cancel())
            cancelled = pending.result(3)
        self.assertEqual(cancelled.outcome, Outcome.CANCELLED)
        self.assertEqual(cancelled.receipts, (SELECTED,))
        self.assertEqual(backend.messages, [SELECT])
        self.assertFalse(diagnostic.cancel())
        self.assertEqual(diagnostic.read(timeout=5).outcome, Outcome.VALID_REFERENCE)

    def test_remove_drains_and_rejects_queued_and_future_operations(self):
        entered, draining, release, competing = Event(), Event(), Event(), Event()

        def hold_until_released(phase, deadline, stopped):
            entered.set()
            self.assertTrue(stopped.wait(3))
            draining.set()
            self.assertTrue(release.wait(3))

        backend = SimulatedBackend([SELECTED], before_reply=hold_until_released)
        diagnostic = CpuDiagnostic(backend)

        def queued_read():
            competing.set()
            return diagnostic.read(timeout=5)

        with ThreadPoolExecutor(max_workers=3) as pool:
            active = pool.submit(diagnostic.read, timeout=5)
            try:
                self.assertTrue(entered.wait(3))
                queued = pool.submit(queued_read)
                self.assertTrue(competing.wait(3))
                removal = pool.submit(diagnostic.remove)
                self.assertTrue(draining.wait(3))
                self.assertFalse(removal.done())
            finally:
                release.set()
            removal.result(3)
            self.assertEqual(active.result(3).outcome, Outcome.REMOVED)
            self.assertEqual(queued.result(3).receipts, ())
            self.assertEqual(queued.result().outcome, Outcome.REMOVED)
        diagnostic.remove()
        self.assertEqual(diagnostic.read(timeout=5).outcome, Outcome.REMOVED)
        self.assertEqual(backend.messages, [SELECT])


if __name__ == "__main__":
    unittest.main()
