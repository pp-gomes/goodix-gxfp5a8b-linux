"""Tests of CI helpers only. KTAP strings below are SYNTHETIC unit fixtures.
Never publish these as evidence of kernel/KUnit execution.
"""
from pathlib import Path
import sys
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from verify_ktap import verify
from run import config_mismatches, effective_values, requested_values

SOURCE = (HERE.parents[1] / 'research/offline-kunit/gx_kunit.c').read_text()
import re
NAMES = re.findall(r'KUNIT_CASE\((\w+)\)', SOURCE)


def synthetic():
    return ('KTAP version 1\n1..1\n    KTAP version 1\n    # Subtest: gx-offline-v07\n    1..25\n'
            + ''.join(f'    ok {i} {name}\n' for i, name in enumerate(NAMES, 1))
            + '# Totals: pass:25 fail:0 skip:0 total:25\nok 1 gx-offline-v07\n')


class Verification(unittest.TestCase):
    def reject(self, raw):
        with self.assertRaises(ValueError):
            verify(SOURCE, raw)

    def test_synthetic_complete_stream(self):
        result, _ = verify(SOURCE, synthetic())
        self.assertEqual(result['passed'], 25)

    def test_compile_log(self):
        self.reject('CC gx_kunit.o\nLD gx_offline_test.o\n')

    def test_case_names_only(self):
        self.reject(SOURCE)

    def test_missing_suite_result(self):
        self.reject(synthetic().replace('ok 1 gx-offline-v07\n', ''))

    def test_missing_root_plan(self):
        self.reject(synthetic().replace('\n1..1\n', '\n'))

    def test_missing_case(self):
        self.reject(synthetic().replace(f'    ok 3 {NAMES[2]}\n', ''))

    def test_duplicate_case(self):
        self.reject(synthetic().replace(f'    ok 3 {NAMES[2]}\n', f'    ok 2 {NAMES[1]}\n'))

    def test_skip(self):
        self.reject(synthetic().replace(f'ok 1 {NAMES[0]}', f'ok 1 {NAMES[0]} # SKIP'))

    def test_failure(self):
        self.reject(synthetic().replace(f'ok 1 {NAMES[0]}', f'not ok 1 {NAMES[0]}'))

    def test_wrong_suite(self):
        self.reject(synthetic().replace('# Subtest: gx-offline-v07', '# Subtest: other'))

    def test_warning_after_success(self):
        self.reject(synthetic() + 'WARNING: possible use-after-free\n')

    def test_truncated_stream(self):
        self.reject(synthetic()[:150])

    def test_two_boots(self):
        self.reject(synthetic() + synthetic())

    def test_timestamp_prefix(self):
        raw = '\n'.join('[    0.010000] ' + line for line in synthetic().splitlines())
        self.assertEqual(verify(SOURCE, raw)[0]['passed'], 25)

    def test_optional_tap_hyphen(self):
        raw = re.sub(r'(ok \d+) ', r'\1 - ', synthetic())
        self.assertEqual(verify(SOURCE, raw)[0]['passed'], 25)

    def test_no_silent_config_fallback(self):
        req = requested_values('CONFIG_KUNIT=y\nCONFIG_PROVE_LOCKING=y\n')
        effective = effective_values('CONFIG_KUNIT=y\n# CONFIG_PROVE_LOCKING is not set\n')
        self.assertIn('CONFIG_PROVE_LOCKING', config_mismatches(req, effective))

    def test_explicit_disabled_config(self):
        self.assertEqual(config_mismatches({'CONFIG_MODULES': 'n'}, {}), {})


if __name__ == '__main__':
    unittest.main()
