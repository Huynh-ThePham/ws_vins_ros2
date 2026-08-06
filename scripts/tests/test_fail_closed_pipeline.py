#!/usr/bin/env python3
"""Tests for the fail-closed publication pipeline (plan P0.3, PR-03).

Each test builds a synthetic run tree and asserts the validator FAILS for exactly the
reason it should. These are the failure modes that used to pass silently.

    python3 -m unittest discover -s scripts/tests -v
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
VALIDATE = REPO / "scripts/validate_experiment_matrix.py"
ASSETS = REPO / "scripts/make_sem_geodf_paper_assets.py"
VIODE_BASE = REPO / "src/config/paper/viode_common.yaml"

FULL_SHA = "a" * 40


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, *cmd], capture_output=True, text=True, cwd=REPO)


# A deliberately tiny expected matrix so the fixtures stay readable.
def tiny_matrix(trials: int = 2, **requirement_overrides) -> dict:
    requirements = {
        "require_same_git_sha": True,
        "require_clean_worktree": True,
        "require_full_git_sha": True,
        "require_resolved_config_hash": True,
        "require_bag_hash": True,
        "require_model_hash_when_semantic": False,
        "min_trajectory_coverage": 0.90,
        "forbid_oracle_runs": True,
        "required_config_values": {"sem_policy_dynamic_level": "-1"},
        "diverged_ate_threshold_m": 50.0,
    }
    requirements.update(requirement_overrides)
    return {
        "datasets": {
            "viode": {
                "scenes": ["city_day_0_none", "city_day_1_low"],
                "methods": ["baseline", "union_weight"],
                "trials": trials,
            }
        },
        "requirements": requirements,
    }


class TreeBuilder:
    """Builds results/<dataset>/<scene>/<method>/t<N>/ run directories."""

    def __init__(self, root: Path):
        self.root = root
        self.config_text = VIODE_BASE.read_text()

    def add(self, dataset="viode", scene="city_day_0_none", method="baseline", trial=1,
            *, status="ok", ate=0.15, coverage=0.98, git_sha=FULL_SHA, git_dirty=False,
            oracle_level=-1, failure_reason=None, bag_hash="b" * 64, model_hash=None,
            config_text=None, omit=(), config_hash_override=None) -> Path:
        run_dir = self.root / dataset / scene / method / f"t{trial}"
        run_dir.mkdir(parents=True, exist_ok=True)

        text = config_text if config_text is not None else self.config_text
        (run_dir / "resolved_config.yaml").write_text(text)
        digest = config_hash_override or hashlib.sha256(text.encode()).hexdigest()

        (run_dir / "eval").mkdir(exist_ok=True)
        metrics = {"trajectory_coverage": coverage}
        if ate is not None:
            metrics["ate_rmse_m"] = ate
        (run_dir / "eval" / "metrics.json").write_text(json.dumps(metrics))

        manifest = {
            "dataset": dataset, "scene": scene, "method": method, "trial": trial,
            "seed": 1000 + int(trial),
            "bag_rate": 1.0,
            "status": status,
            "failure_reason": failure_reason,
            "git_sha": git_sha,
            "git_dirty": git_dirty,
            "resolved_config_sha256": digest,
            "bag_sha256": bag_hash,
            "model_sha256": model_hash,
            "yolo": method != "baseline",
            "oracle_ablation": oracle_level >= 0,
            "sem_policy_dynamic_level": oracle_level,
            "ate_rmse_m": ate,
            "trajectory_coverage": coverage,
            "artifacts": {"metrics": str(run_dir / "eval" / "metrics.json")},
        }
        for key in omit:
            manifest.pop(key, None)
        (run_dir / "run_manifest.json").write_text(json.dumps(manifest, indent=2))
        return run_dir

    def complete(self, trials=2, **kwargs) -> None:
        for scene in ("city_day_0_none", "city_day_1_low"):
            for method in ("baseline", "union_weight"):
                for trial in range(1, trials + 1):
                    self.add(scene=scene, method=method, trial=trial, **kwargs)


class ValidatorCase(unittest.TestCase):
    def validate(self, build, *, matrix=None, extra_args=()) -> subprocess.CompletedProcess:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name) / "results"
        root.mkdir()
        build(TreeBuilder(root))
        expected = Path(self.tmp.name) / "expected.json"
        expected.write_text(json.dumps(matrix or tiny_matrix()))
        return run([str(VALIDATE), "--root", str(root), "--expected", str(expected),
                    *extra_args])


class TestValidatorAcceptsAGoodTree(ValidatorCase):
    def test_complete_tree_passes(self):
        result = self.validate(lambda b: b.complete())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS", result.stdout)

    def test_writes_a_report_the_asset_gate_can_read(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name) / "results"
        root.mkdir()
        TreeBuilder(root).complete()
        expected = Path(self.tmp.name) / "expected.json"
        expected.write_text(json.dumps(tiny_matrix()))
        result = run([str(VALIDATE), "--root", str(root), "--expected", str(expected)])
        self.assertEqual(result.returncode, 0, result.stdout)
        payload = json.loads((root / "validation.json").read_text())
        self.assertEqual(payload["result"], "PASS")


class TestValidatorRejects(ValidatorCase):
    def assertFails(self, build, needle: str, *, matrix=None, extra_args=()):
        result = self.validate(build, matrix=matrix, extra_args=extra_args)
        self.assertEqual(result.returncode, 1,
                         f"expected FAIL, got {result.returncode}\n{result.stdout}")
        self.assertIn(needle, result.stdout, result.stdout)

    def test_empty_tree(self):
        self.assertFails(lambda b: None, "no run_manifest.json")

    def test_missing_cell(self):
        # The exact failure a `|| true` runner produced: one method never ran on one
        # scene, everything else completed, so the summary looked finished.
        def build(b):
            import shutil
            b.complete()
            shutil.rmtree(b.root / "viode/city_day_1_low/union_weight")
        self.assertFails(build, "no runs at all")

    def test_missing_trial_in_a_cell(self):
        def build(b):
            b.complete(trials=2)
            (b.root / "viode/city_day_0_none/baseline/t2/run_manifest.json").unlink()
        self.assertFails(build, "missing trial(s)")

    def test_duplicate_trial(self):
        def build(b):
            b.complete()
            dup = b.root / "viode/city_day_0_none/baseline/t1_copy"
            dup.mkdir()
            src = b.root / "viode/city_day_0_none/baseline/t1"
            for name in ("run_manifest.json", "resolved_config.yaml"):
                (dup / name).write_text((src / name).read_text())
        self.assertFails(build, "appears twice")

    def test_silently_diverged_run_marked_ok(self):
        # The defect: ATE > 50 m used to be dropped by the asset script with a bare
        # `continue`, so the run vanished from both the mean and the trial count.
        def build(b):
            b.complete()
            b.add(scene="city_day_0_none", method="union_weight", trial=1,
                  status="ok", ate=812.4)
        self.assertFails(build, "divergence threshold")

    def test_diverged_run_without_a_reason(self):
        def build(b):
            b.complete()
            b.add(scene="city_day_0_none", method="union_weight", trial=1,
                  status="diverged", ate=812.4, failure_reason=None)
        self.assertFails(build, "without a failure_reason")

    def test_properly_recorded_divergence_is_accepted_but_cell_must_have_a_survivor(self):
        # A recorded divergence is fine; a cell where EVERY trial diverged is not.
        def build(b):
            b.complete()
            for trial in (1, 2):
                b.add(scene="city_day_1_low", method="union_weight", trial=trial,
                      status="diverged", ate=900.0, failure_reason="ate_threshold_exceeded")
        self.assertFails(build, "no usable result")

    def test_short_git_sha(self):
        self.assertFails(lambda b: b.complete(git_sha="81b3dee"),
                         "not a full 40-character SHA")

    def test_dirty_worktree(self):
        self.assertFails(lambda b: b.complete(git_dirty=True), "dirty worktree")

    def test_dirty_worktree_can_be_allowed_explicitly(self):
        result = self.validate(lambda b: b.complete(git_dirty=True),
                               extra_args=("--allow-dirty",))
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_unknown_dirty_state_is_not_treated_as_clean(self):
        self.assertFails(lambda b: b.complete(omit=("git_dirty",)),
                         "does not record git_dirty")

    def test_mixed_commits(self):
        def build(b):
            b.complete()
            b.add(scene="city_day_0_none", method="baseline", trial=1, git_sha="c" * 40)
        self.assertFails(build, "different commits")

    def test_config_hash_mismatch(self):
        self.assertFails(lambda b: b.complete(config_hash_override="d" * 64),
                         "does not match the manifest")

    def test_missing_bag_hash(self):
        self.assertFails(lambda b: b.complete(bag_hash=None), "no bag_sha256")

    def test_low_trajectory_coverage(self):
        # ATE is not comparable across methods at different coverage.
        self.assertFails(lambda b: b.complete(coverage=0.42),
                         "below the required")

    def test_missing_trajectory_coverage(self):
        def build(b):
            b.complete()
            run_dir = b.root / "viode/city_day_0_none/baseline/t1"
            (run_dir / "eval" / "metrics.json").write_text(json.dumps({"ate_rmse_m": 0.2}))
            manifest = json.loads((run_dir / "run_manifest.json").read_text())
            manifest.pop("trajectory_coverage")
            (run_dir / "run_manifest.json").write_text(json.dumps(manifest))
        self.assertFails(build, "no trajectory_coverage")

    def test_oracle_run_in_a_main_tree(self):
        def build(b):
            b.complete()
            b.add(scene="city_day_0_none", method="union_weight", trial=1, oracle_level=2)
        self.assertFails(build, "oracle run")

    def test_oracle_value_in_the_resolved_config(self):
        # Even if the manifest claims otherwise, the config the estimator was given is
        # the authority.
        leaked = VIODE_BASE.read_text().replace("sem_policy_dynamic_level: -1",
                                                "sem_policy_dynamic_level: 3")
        self.assertFails(lambda b: b.complete(config_text=leaked),
                         "protocol requires -1")

    def test_semantic_run_without_a_model_hash(self):
        self.assertFails(lambda b: b.complete(model_hash=None),
                         "without model_sha256",
                         matrix=tiny_matrix(require_model_hash_when_semantic=True))

    def test_undeclared_cell_in_the_tree(self):
        def build(b):
            b.complete()
            b.add(scene="city_day_0_none", method="sneaky_method", trial=1)
        self.assertFails(build, "not declared in the expected matrix")

    def test_unrecognised_status(self):
        self.assertFails(lambda b: b.complete(status="probably_fine"),
                         "unrecognised status")

    def test_min_trials_override_is_reported_not_silent(self):
        result = self.validate(lambda b: b.complete(trials=1),
                               matrix=tiny_matrix(trials=10),
                               extra_args=("--min-trials", "1"))
        self.assertEqual(result.returncode, 0, result.stdout)
        # The shortfall must be visible.
        self.assertIn("checking 1 of the declared 10 trials", result.stdout)


class TestAssetGate(unittest.TestCase):
    def test_assets_refuse_an_unvalidated_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "results"
            root.mkdir()
            result = run([str(ASSETS), "--root", str(root), "--out", str(Path(tmp) / "paper")])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("validation.json not found", result.stderr + result.stdout)

    def test_assets_refuse_a_failed_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "results"
            root.mkdir()
            (root / "validation.json").write_text(json.dumps(
                {"result": "FAIL", "errors": ["city_day_0_none/baseline: missing trial(s) [2]"]}))
            result = run([str(ASSETS), "--root", str(root), "--out", str(Path(tmp) / "paper")])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing trial(s)", result.stderr + result.stdout)

    def test_assets_refuse_missing_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "results"
            root.mkdir()
            (root / "validation.json").write_text(json.dumps({"result": "PASS", "errors": []}))
            result = run([str(ASSETS), "--root", str(root), "--out", str(Path(tmp) / "paper")])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("validation_receipt", result.stderr + result.stdout)


class TestNoSilentContinueOnThePublicationPath(unittest.TestCase):
    def test_asset_script_reports_divergence_instead_of_dropping_it(self):
        sys.path.insert(0, str(REPO / "scripts"))
        import importlib
        module = importlib.import_module("make_sem_geodf_paper_assets")

        # Two successful runs and one divergence in the same cell.
        data = {
            "ate": {("s", "m"): [0.2, 0.3]},
            "excluded": {("s", "m"): {"diverged": 1, "failed": 0, "qc_failed": 0,
                                      "oracle": 0, "no_ate": 0}},
            "diverged_values": {("s", "m"): [900.0]},
        }
        ok, diverged, failed, total = module.cell_counts(data, "s", "m")
        self.assertEqual((ok, diverged, failed, total), (2, 1, 0, 3))
        # The divergence is counted, so the success rate is not 100%.
        self.assertAlmostEqual(module.success_rate(data, "s", "m"), 2 / 3)
        # The plain ATE is the survivor mean...
        mean, _, n = module.ate(data, "s", "m")
        self.assertAlmostEqual(mean, 0.25)
        self.assertEqual(n, 2)
        # ...and the penalized ATE charges the divergence instead of hiding it.
        penalized = module.failure_penalized_ate(data, "s", "m")
        self.assertGreater(penalized, mean)
        self.assertAlmostEqual(penalized, (0.2 + 0.3 + module.FAILURE_PENALTY_ATE_M) / 3)


if __name__ == "__main__":
    unittest.main()
