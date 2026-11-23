"""Script safety/selection tests on temporary metadata trees; no live sysfs."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parent / 'support/linux-readonly-checks.sh'


class ReadonlyChecksTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='goodix-readonly-test-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.target = self.root / 'sys/devices/pci0000:00/0000:00:1e.3/pxa2xx-spi.3/spi_master/spi0/spi-GXFP5A8B:00'
        self.target.mkdir(parents=True)
        self.link = self.root / 'sys/bus/spi/devices/spi-GXFP5A8B:00'
        self.link.parent.mkdir(parents=True)
        self.link.symlink_to(self.target)
        self.write(self.target / 'modalias', 'acpi:GXFP5A8B:GXFP5A8B:\n')
        self.write(self.target / 'power/runtime_status', 'suspended\n')
        self.write(self.target.parent / 'power/runtime_status', 'active\n')
        self.write(self.target / 'statistics/messages', '2\n')
        self.write(self.root / 'proc/version', 'fixture kernel, not live\n')
        self.write(self.root / 'proc/sys/kernel/osrelease', 'fixture-release\n')
        self.write(self.root / 'proc/interrupts', ' CPU0\n176: 3 intel-gpio gxfp-observer\n99: 1 other\n')
        self.write(self.root / 'proc/modules', 'spi_pxa2xx_platform 1 0 - Live 0\nunrelated 1 0 - Live 0\n')
        # Reading any old active sysfs diagnostic or sibling would hang the test.
        os.mkfifo(self.target / 'cpu_version')
        os.mkfifo(self.target / 'wakeup_cpu_version')
        self.flash = self.target.parent / 'spi1.0'
        self.flash.mkdir()
        os.mkfifo(self.flash / 'modalias')

    @staticmethod
    def write(path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)

    def run_script(self, *args):
        return subprocess.run(['bash', str(SCRIPT), '--snapshot-root', str(self.root), *args],
                              capture_output=True, text=True, timeout=8)

    def test_metadata_and_only_target_ancestors(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('fixture-release', result.stdout)
        self.assertIn('runtime_status: suspended', result.stdout)
        self.assertIn('runtime_status: active', result.stdout)
        self.assertIn('statistics/messages: 2', result.stdout)
        self.assertIn('176: 3', result.stdout)
        self.assertNotIn('99: 1 other', result.stdout)
        self.assertNotIn('cpu_version:', result.stdout)
        self.assertNotIn('NODE ' + str(self.flash), result.stdout)

    def test_rejects_flash_name(self):
        result = self.run_script('--device', 'spi1.0')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('forbidden', result.stderr)

    def test_rejects_target_symlink_to_flash_before_identity_read(self):
        self.link.unlink()
        self.link.symlink_to(self.flash)
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('spi1.0', result.stderr)

    def test_rejects_unconfirmed_identity(self):
        self.write(self.target / 'modalias', 'spi:unrelated\n')
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('NODE ', result.stdout)

    def test_missing_target_does_not_scan(self):
        self.link.unlink()
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('no bus scan', result.stderr)

    def test_missing_attributes_are_tolerated(self):
        (self.target / 'power/runtime_status').unlink()
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('unavailable or excluded', result.stdout)

    def test_metadata_symlink_to_flash_is_not_read(self):
        counter = self.target / 'statistics/messages'
        counter.unlink()
        counter.symlink_to(self.flash / 'modalias')
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('statistics/messages: unavailable or excluded', result.stdout)

    def test_sysfs_and_proc_cannot_escape_snapshot(self):
        outside = self.root / 'external'
        outside.write_text('DO-NOT-READ-OUTSIDE\n')
        counter = self.target / 'statistics/messages'
        counter.unlink()
        counter.symlink_to(outside)
        (self.root / 'proc/version').unlink()
        (self.root / 'proc/version').symlink_to(outside)
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('DO-NOT-READ-OUTSIDE', result.stdout)

    def test_irq_metadata_requires_operator_number(self):
        self.write(self.root / 'sys/kernel/irq/176/type', 'edge\n')
        self.write(self.root / 'sys/kernel/irq/176/hwirq', '259\n')
        self.write(self.root / 'proc/irq/176/spurious', 'count 0\n')
        result = self.run_script('--irq', '176')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('/176/type: edge', result.stdout)
        self.assertIn('not derived from GPIO259', result.stdout)

    def test_snapshot_never_runs_live_dmesg(self):
        result = self.run_script('--logs')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unavailable with --snapshot-root', result.stderr)

    def test_rejects_path_injection_and_unknown_options(self):
        for args in (('--device', '../../dev/spidev1.0'), ('--irq', '../../dev'),
                     ('--write',), ('--device',)):
            with self.subTest(args=args):
                self.assertNotEqual(self.run_script(*args).returncode, 0)


if __name__ == '__main__':
    unittest.main()
