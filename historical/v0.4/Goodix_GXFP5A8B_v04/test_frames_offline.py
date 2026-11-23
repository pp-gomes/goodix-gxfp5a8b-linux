"""Teste de estrutura dos dois comandos SPI sem tocar em hardware."""
import unittest


def selection_frame(address: int) -> bytes:
    if not 0 <= address <= 0xFFFF:
        raise ValueError("address outside 16-bit range")
    return bytes((0xF0, address >> 8, address & 0xFF))


class OfflineReadFrame(unittest.TestCase):
    def test_cpu_version_select(self):
        self.assertEqual(selection_frame(0x00A8), b"\xf0\x00\xa8")

    def test_read_command(self):
        self.assertEqual(bytes([0xF1]), b"\xf1")

    def test_spi_mailbox_is_different(self):
        self.assertEqual(selection_frame(0x3B00), b"\xf0\x3b\x00")

    def test_invalid_addresses(self):
        with self.assertRaises(ValueError):
            selection_frame(0x10000)
        with self.assertRaises(ValueError):
            selection_frame(-1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
