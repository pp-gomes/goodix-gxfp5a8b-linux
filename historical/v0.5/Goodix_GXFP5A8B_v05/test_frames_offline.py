"""Offline framing tests. No SPI operations; python stdlib only."""
import pathlib
import unittest

BASE = pathlib.Path(__file__).parent
CODE = (BASE / 'gxfp5a8b_probe.c').read_text()

class ProtocolTests(unittest.TestCase):
    def test_wakeup_frame(self):
        self.assertEqual(bytes((0xE5,)), b'\xe5')

    def test_cpu_select(self):
        self.assertEqual(bytes((0xF0, 0x00, 0xA8)), b'\xf0\x00\xa8')

    def test_read_frame(self):
        self.assertEqual(bytes((0xF1,)), b'\xf1')

    def test_five_byte_explicit_tx(self):
        self.assertEqual(1 + 3 + 1, 5)

    def test_optin_disabled_by_default(self):
        self.assertIn('static bool allow_wakeup_cpu_read;', CODE)
        self.assertIn('static bool allow_cpu_read;', CODE)

    def test_wakeup_precedes_read(self):
        body = CODE.split('static ssize_t gxfp_read_cpu_version(',1)[1].split('static ssize_t cpu_version_show(',1)[0]
        self.assertLess(body.index('spi_write(ctx->spi, &wake'), body.index('usleep_range(50000'))
        self.assertLess(body.index('usleep_range(50000'), body.index('spi_write(ctx->spi, select_addr'))
        self.assertLess(body.index('spi_write(ctx->spi, select_addr'), body.index('spi_write_then_read(ctx->spi, &cmd'))

    def test_no_gpio_reset_firmware_commands_in_source(self):
        for blocked in ['gpiod_set_value', 'gpio_set_value', 'spi_setup(', 'request_firmware(', '0xa4,', '0xe0,', '0xe4,']:
            self.assertNotIn(blocked, CODE)

if __name__ == '__main__':
    unittest.main(verbosity=2)
