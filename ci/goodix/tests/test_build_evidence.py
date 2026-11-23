"""Synthetic integration guard fixtures; never kernel execution evidence."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_evidence import PARTS, check_members, check_image

SOURCE = (Path(__file__).resolve().parents[3] / 'research/offline-kunit/gx_kunit.c').read_text()


class BuildEvidence(unittest.TestCase):
    def test_flattened_builtin_without_composite_object(self):
        check_members(['drivers/misc/gx_offline/' + p + '.o' for p in PARTS])

    def test_missing_component(self):
        with self.assertRaises(ValueError):
            check_members([p + '.o' for p in PARTS[:-1]])

    def test_duplicate_component(self):
        with self.assertRaises(ValueError):
            check_members([p + '.o' for p in PARTS] + ['gx_kunit.o'])

    def test_unrelated_image_rejected(self):
        with self.assertRaises(ValueError):
            check_image('00000000 T unrelated\n', SOURCE)
