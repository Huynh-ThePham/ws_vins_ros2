#!/usr/bin/env python3
"""Unit tests for trajectory coverage used by publication manifests."""
from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MODULE_PATH = REPO / "scripts/evaluate_trajectory.py"
SPEC = importlib.util.spec_from_file_location("evaluate_trajectory", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
EVALUATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EVALUATE)


class TrajectoryCoverageCase(unittest.TestCase):
    def test_full_temporal_overlap(self) -> None:
        self.assertEqual(EVALUATE._trajectory_coverage((10.0, 20.0), (10.0, 20.0)), 1.0)

    def test_partial_temporal_overlap(self) -> None:
        self.assertAlmostEqual(
            EVALUATE._trajectory_coverage((12.0, 18.0), (10.0, 20.0)), 0.6)

    def test_disjoint_trajectory(self) -> None:
        self.assertEqual(EVALUATE._trajectory_coverage((21.0, 30.0), (10.0, 20.0)), 0.0)

    def test_time_bounds_are_order_independent(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            trajectory = Path(temp_dir) / "est.tum"
            trajectory.write_text("3.0 0 0 0 0 0 0 1\n1.0 0 0 0 0 0 0 1\n")
            self.assertEqual(EVALUATE._time_bounds(trajectory), (1.0, 3.0))


if __name__ == "__main__":
    unittest.main()
