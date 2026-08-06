#!/usr/bin/env python3
"""Integration tests: publication runner uses resolved fair configs (P0.1).

These tests do not launch ROS. They prove the config path that the ablation runner
constructs is base+overlay -> resolved_config.yaml, audited, and distinct from legacy
per-method YAMLs.
"""
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
GENERATE = REPO / "scripts/generate_paper_config.py"
AUDIT = REPO / "scripts/audit_method_config_diff.py"
ABLATION = REPO / "scripts/run_sem_geodf_ablation.sh"
OVERLAYS = REPO / "src/config/paper/overlays"
VIODE_BASE = REPO / "src/config/paper/viode_common.yaml"
EUROC_BASE = REPO / "src/config/paper/euroc_common.yaml"
MAIN = ["baseline", "geodf", "semantic", "union_noweight", "union_weight"]


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, cwd=REPO)


class TestPublicationResolvedConfigPath(unittest.TestCase):
    def test_runner_does_not_reference_legacy_method_yamls(self):
        text = ABLATION.read_text()
        self.assertIn("generate_paper_config.py", text)
        self.assertIn("resolved_config.yaml", text)
        self.assertNotIn("euroc_${mode}_config.yaml", text)
        self.assertNotIn("viode_${mode}_config.yaml", text)
        self.assertIn('METHODS="${METHODS:-baseline geodf semantic union_noweight union_weight}"',
                      text)

    def test_main_matrix_shares_backbone_keys(self):
        for base in (VIODE_BASE, EUROC_BASE):
            result = run([sys.executable, str(AUDIT), "--base", str(base),
                          "--methods", ",".join(MAIN)])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_full_adaptive_rejected_from_main_matrix(self):
        result = run([sys.executable, str(AUDIT), "--base", str(VIODE_BASE),
                      "--methods", ",".join(MAIN + ["full_adaptive"])])
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("visual_adaptive_quality", result.stdout)

    def test_generate_writes_resolved_config_consumed_as_node_config(self):
        """Simulate the runner's prepare_resolved_config for every main method."""
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            for method in MAIN:
                out = tmp / "viode" / method
                out.mkdir(parents=True)
                resolved = out / "resolved_config.yaml"
                result = run([
                    sys.executable, str(GENERATE),
                    "--base", str(VIODE_BASE),
                    "--overlay", str(OVERLAYS / f"{method}.yaml"),
                    "--out", str(resolved),
                    "--set", f'output_path="{out}/"',
                    "--set", f'pose_graph_save_path="{out}/pose_graph/"',
                ])
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(resolved.is_file())
                # The file the node would receive is exactly this path.
                node_config = resolved
                self.assertEqual(node_config.name, "resolved_config.yaml")
                digest = hashlib.sha256(node_config.read_bytes()).hexdigest()
                provenance = json.loads(
                    (out / "resolved_config.yaml.provenance.json").read_text())
                self.assertEqual(provenance["resolved_sha256"], digest)
                self.assertEqual(provenance["overlay"], method)

    def test_union_weight_pair_backend_key_only(self):
        result = run([sys.executable, str(AUDIT), "--base", str(VIODE_BASE),
                      "--methods", "union_noweight,union_weight"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class TestReusableRunGate(unittest.TestCase):
    def test_metrics_alone_are_not_enough(self):
        script = REPO / "scripts/validate_reusable_run.py"
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = Path(tmp) / "run"
            (run_dir / "eval").mkdir(parents=True)
            (run_dir / "eval" / "metrics.json").write_text(
                json.dumps({"ate_rmse_m": 0.1, "trajectory_coverage": 0.99}))
            result = run([
                sys.executable, str(script),
                "--run-dir", str(run_dir),
                "--expected-method", "baseline",
                "--expected-dataset", "euroc",
                "--expected-scene", "MH_01_easy",
                "--expected-trial", "1",
                "--expected-seed", "1001",
            ])
            self.assertEqual(result.returncode, 1)
            self.assertIn("RERUN", result.stderr)


if __name__ == "__main__":
    unittest.main()
