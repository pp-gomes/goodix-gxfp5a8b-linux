"""Synthetic evidence fixtures, not QEMU/kernel validation results."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_evidence import PARTS
from kcsan_qemu_checks import check_instrumentation, runtime_evidence, collect_runtime
from run import config_mismatches, effective_values, requested_values
from run_kcsan_qemu import classify_failure

BOOT = '''[    0.000000] Linux version 6.12.0-gx-v07-kcsan (synthetic fixture)
[    0.001000] kcsan: enabled early
[    0.001001] kcsan: strict mode configured
[    0.100000] smp: Brought up 1 node, 2 CPUs
'''


class KcsanQemu(unittest.TestCase):
    def test_runtime_confirmation(self):
        self.assertEqual(runtime_evidence(BOOT)['online_cpus'], 2)

    def test_kernel_arguments_are_not_boot_evidence(self):
        with self.assertRaises(ValueError):
            runtime_evidence('COMMAND: kcsan.early_enable=1 CONFIG_KCSAN_STRICT=y\n')

    def test_no_runtime_enable(self):
        with self.assertRaises(ValueError):
            runtime_evidence(BOOT.replace('kcsan: enabled early', 'kcsan: disabled'))

    def test_nonstrict_rejected(self):
        with self.assertRaises(ValueError):
            runtime_evidence(BOOT.replace('kcsan: strict mode configured', 'kcsan: non-strict mode configured'))

    def test_single_cpu_rejected(self):
        with self.assertRaises(ValueError):
            runtime_evidence(BOOT.replace('2 CPUs', '1 CPUs'))

    def test_report_after_boot_rejected(self):
        with self.assertRaises(ValueError):
            runtime_evidence(BOOT + 'BUG: KCSAN: data-race in synthetic_test\n')

    def test_wrong_kernel_rejected(self):
        with self.assertRaises(ValueError):
            runtime_evidence(BOOT.replace('6.12.0-gx-v07-kcsan', '6.11.0-gx-v07-kcsan'))

    def test_instrumentation_needs_flags_hooks_and_runtime(self):
        commands = {p: 'gcc -fsanitize=thread -c fixture.c' for p in PARTS}
        imports = {p: '                 U __tsan_read8\n' for p in PARTS}
        defined = '\n'.join('000001 T ' + p for p in ('kcsan_init', '__tsan_read8', '__tsan_write8'))
        check_instrumentation(commands, imports, defined)
        with self.assertRaises(ValueError):
            check_instrumentation({**commands, 'gx_rx_staging': 'gcc -c fixture.c'}, imports, defined)
        with self.assertRaises(ValueError):
            check_instrumentation(commands, {**imports, 'gx_exchange': ' U memcpy'}, defined)
        with self.assertRaises(ValueError):
            check_instrumentation(commands, imports, '')

    def test_config_rejects_disabled_kcsan_and_strict(self):
        text = (Path(__file__).resolve().parents[1] / 'profiles/kcsan-x86_64.config').read_text()
        req = requested_values(text)
        mismatch = config_mismatches(req, effective_values(text.replace('CONFIG_KCSAN=y', '# CONFIG_KCSAN is not set')
                                                         .replace('CONFIG_KCSAN_STRICT=y', '')))
        self.assertIn('CONFIG_KCSAN', mismatch)
        self.assertIn('CONFIG_KCSAN_STRICT', mismatch)

    def test_partial_failure_logs_are_preserved(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            raw = BOOT + 'KTAP version 1\n1..1\nBUG: KCSAN: synthetic race\n'
            (root / 'qemu.log').write_text(raw)
            collect_runtime(root)
            self.assertEqual((root / 'kernel-runtime.log').read_text(), raw)
            self.assertIn('BUG: KCSAN:', (root / 'kcsan-diagnostics.log').read_text())
            self.assertTrue((root / 'runtime-stream.ktap').read_text().startswith('KTAP'))

    def test_infrastructure_failures_not_classified_as_goodix_races(self):
        self.assertEqual(classify_failure('config'), 'configuration')
        self.assertEqual(classify_failure('build'), 'build')
        self.assertEqual(classify_failure('execute'), 'qemu_execution_or_timeout')


if __name__ == '__main__':
    unittest.main()
