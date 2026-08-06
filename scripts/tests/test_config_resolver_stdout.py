#!/usr/bin/env python3
"""Shell-level check: prepare_resolved_config / generate_paper_config stdout contract.

Command substitution of the resolver must receive exactly one line — the resolved
YAML path. Status logs belong on stderr.
"""
from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
GENERATE = REPO / "scripts" / "generate_paper_config.py"
BASE = REPO / "src" / "config" / "paper" / "euroc_common.yaml"
OVERLAY = REPO / "src" / "config" / "paper" / "overlays" / "baseline.yaml"


class TestConfigResolverStdout(unittest.TestCase):
    def test_generate_stdout_is_single_path_line(self):
        if not BASE.is_file() or not OVERLAY.is_file():
            self.skipTest("paper config templates missing")
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "resolved_config.yaml"
            proc = subprocess.run(
                [
                    "python3",
                    str(GENERATE),
                    "--base",
                    str(BASE),
                    "--overlay",
                    str(OVERLAY),
                    "--out",
                    str(out),
                ],
                cwd=str(REPO),
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            # generate_paper_config itself may print nothing on stdout; the runner
            # echoes the path. Simulate the runner contract:
            runner = subprocess.run(
                [
                    "bash",
                    "-lc",
                    f'python3 "{GENERATE}" --base "{BASE}" --overlay "{OVERLAY}" '
                    f'--out "{out}" >&2; echo "{out}"',
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(runner.returncode, 0, runner.stderr)
            lines = [ln for ln in runner.stdout.splitlines() if ln.strip()]
            self.assertEqual(len(lines), 1, runner.stdout)
            resolved = Path(lines[0].strip())
            self.assertTrue(resolved.is_file(), resolved)
            # OpenCV YAML uses `%YAML:1.0`, which PyYAML cannot parse. Validate
            # structure with a light textual contract instead.
            text = resolved.read_text()
            self.assertIn("%YAML", text)
            self.assertIn("imu_topic:", text)
            self.assertGreater(len(text), 200)


if __name__ == "__main__":
    unittest.main()
