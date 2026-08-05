#!/usr/bin/env python3
"""Tests for the fair-config framework and its audit (plan P0.1, PR-02/PR-07).

Run with:  python3 -m unittest discover -s scripts/tests -v
No third-party dependencies: CI must be able to run these without a ROS image.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "scripts" / "lib"))

import paper_config  # noqa: E402

VIODE_BASE = REPO / "src/config/paper/viode_common.yaml"
EUROC_BASE = REPO / "src/config/paper/euroc_common.yaml"
OVERLAYS = REPO / "src/config/paper/overlays"
ALLOWLIST = REPO / "src/config/paper/allowed_differences.yaml"
AUDIT = REPO / "scripts/audit_method_config_diff.py"
GENERATE = REPO / "scripts/generate_paper_config.py"

MAIN_MATRIX = "baseline,geodf,semantic,union_noweight,union_weight"


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, *cmd], capture_output=True, text=True, cwd=REPO)


class TestOpenCvYamlParsing(unittest.TestCase):
    def test_scalars_skip_matrix_blocks_and_comments(self):
        text = (
            "%YAML:1.0\n"
            "# a comment\n"
            "imu: 1\n"
            "sem_enable: 0   # trailing comment\n"
            'cam0_calib: "cam0_pinhole.yaml"\n'
            "body_T_cam0: !!opencv-matrix\n"
            "   rows: 4\n"
            "   cols: 4\n"
            "   data: [1.0, 0.0]\n"
            "gyr_w: 4.0e-5\n"
        )
        scalars = paper_config.parse_scalars(text)
        self.assertEqual(scalars["imu"], "1")
        self.assertEqual(scalars["sem_enable"], "0")
        self.assertEqual(scalars["cam0_calib"], '"cam0_pinhole.yaml"')
        self.assertEqual(scalars["gyr_w"], "4.0e-5")
        # The matrix key and its indented body must not leak into the scalar set.
        self.assertNotIn("body_T_cam0", scalars)
        self.assertNotIn("rows", scalars)
        self.assertNotIn("data", scalars)

    def test_matrix_blocks_are_captured_whole(self):
        text = VIODE_BASE.read_text()
        blocks = paper_config.parse_matrix_blocks(text)
        self.assertEqual(sorted(blocks), ["body_T_cam0", "body_T_cam1"])
        for block in blocks.values():
            self.assertIn("!!opencv-matrix", block)
            self.assertIn("data:", block)

    def test_values_equal_is_numeric_when_possible(self):
        self.assertTrue(paper_config.values_equal("0.20", "0.2"))
        self.assertTrue(paper_config.values_equal("1", "1.0"))
        self.assertFalse(paper_config.values_equal("0.2", "0.3"))
        self.assertTrue(paper_config.values_equal("dice", "dice"))
        self.assertFalse(paper_config.values_equal("dice", "jaccard"))

    def test_apply_overlay_replaces_in_place_and_preserves_matrices(self):
        base = VIODE_BASE.read_text()
        out = paper_config.apply_overlay(base, {"sem_enable": "1"}, "t")
        self.assertIn("\nsem_enable: 1\n", out)
        self.assertNotIn("\nsem_enable: 0\n", out)
        # No key appended: the base already declared it.
        self.assertNotIn("absent from base", out)
        self.assertEqual(paper_config.parse_matrix_blocks(base),
                         paper_config.parse_matrix_blocks(out))
        # Key count is unchanged; only a value moved.
        self.assertEqual(set(paper_config.parse_scalars(base)),
                         set(paper_config.parse_scalars(out)))


class TestBaseConfigs(unittest.TestCase):
    def test_baseline_has_every_mechanism_off(self):
        for base in (VIODE_BASE, EUROC_BASE):
            keys = paper_config.parse_scalars(base.read_text())
            for key in ("sem_enable", "geodf_enable", "geodf_hard_reject",
                        "sem_geodf_fusion", "sem_adaptive_policy",
                        "sem_geodf_backend_weight", "visual_adaptive_quality",
                        "visual_adaptive_huber", "imu_adaptive_covariance"):
                self.assertEqual(keys.get(key), "0",
                                 f"{base.name}: {key} must be 0 in the common backbone")

    def test_no_oracle_and_failure_detection_on(self):
        for base in (VIODE_BASE, EUROC_BASE):
            keys = paper_config.parse_scalars(base.read_text())
            self.assertEqual(keys.get("sem_policy_dynamic_level"), "-1",
                             f"{base.name}: ground-truth dynamic level must not be fed online")
            self.assertEqual(keys.get("failure_detection_enable"), "1")

    def test_bases_differ_only_in_dataset_intrinsics(self):
        viode = paper_config.parse_scalars(VIODE_BASE.read_text())
        euroc = paper_config.parse_scalars(EUROC_BASE.read_text())
        self.assertEqual(set(viode), set(euroc),
                         "both dataset backbones must declare the same key set")
        allowed = {"cam0_calib", "cam1_calib", "max_cnt",
                   "acc_n", "gyr_n", "acc_w", "gyr_w"}
        differing = {k for k in viode
                     if not paper_config.values_equal(viode[k], euroc[k])}
        self.assertTrue(differing <= allowed,
                        f"unexpected VIODE/EuRoC differences: {sorted(differing - allowed)}")

    def test_every_overlay_key_exists_in_both_bases(self):
        viode = paper_config.parse_scalars(VIODE_BASE.read_text())
        euroc = paper_config.parse_scalars(EUROC_BASE.read_text())
        for path in sorted(OVERLAYS.glob("*.yaml")):
            settings, _ = paper_config.parse_overlay(path)
            for key in settings:
                self.assertIn(key, viode, f"{path.name}: {key} missing from viode_common")
                self.assertIn(key, euroc, f"{path.name}: {key} missing from euroc_common")

    def test_every_overlay_is_declared_in_the_allowlist(self):
        declared = set()
        for raw in ALLOWLIST.read_text().splitlines():
            line = raw.strip()
            if line and not line.startswith("#") and ":" in line:
                declared.add(line.split(":", 1)[0].strip())
        for path in sorted(OVERLAYS.glob("*.yaml")):
            self.assertIn(path.stem, declared,
                          f"{path.name} has no ownership entry in allowed_differences.yaml")


class TestAudit(unittest.TestCase):
    def test_main_matrix_passes_on_both_bases(self):
        for base in (VIODE_BASE, EUROC_BASE):
            result = run([str(AUDIT), "--base", str(base), "--methods", MAIN_MATRIX])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS", result.stdout)

    def test_union_pair_differs_in_backend_weight_only(self):
        base = VIODE_BASE.read_text()
        u, _ = paper_config.parse_overlay(OVERLAYS / "union_noweight.yaml")
        uw, _ = paper_config.parse_overlay(OVERLAYS / "union_weight.yaml")
        left = paper_config.parse_scalars(paper_config.apply_overlay(base, u, "u"))
        right = paper_config.parse_scalars(paper_config.apply_overlay(base, uw, "uw"))
        diffs = sorted(k for k in set(left) | set(right)
                       if not paper_config.values_equal(left.get(k, ""), right.get(k, "")))
        self.assertEqual(diffs, ["sem_geodf_backend_weight"])

    def test_factor_adaptation_on_for_one_method_only_fails(self):
        result = run([str(AUDIT), "--base", str(VIODE_BASE),
                      "--methods", MAIN_MATRIX + ",full_adaptive"])
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("visual_adaptive_quality", result.stdout)

    def test_full_adaptation_matrix_requires_all_methods_on(self):
        # The main-matrix overlays leave adaptation off, so demanding the
        # full-adaptation matrix from them must fail rather than silently pass.
        result = run([str(AUDIT), "--base", str(VIODE_BASE),
                      "--methods", MAIN_MATRIX, "--matrix", "full-adaptation"])
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("requires visual_adaptive_quality=1", result.stdout)

    def test_confound_injected_into_the_base_is_caught(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            # An overlay that flips a shared protocol key it does not own.
            evil = tmp / "overlays"
            evil.mkdir()
            (evil / "union_weight.yaml").write_text(
                "sem_enable: 1\ngeodf_enable: 1\ngeodf_hard_reject: 1\n"
                "sem_geodf_fusion: 1\nsem_adaptive_policy: 1\n"
                "sem_geodf_backend_weight: 1\nmax_cnt: 200\n")
            base = VIODE_BASE.read_text()
            settings, _ = paper_config.parse_overlay(evil / "union_weight.yaml")
            configs = {
                "baseline": base,
                "union_weight": paper_config.apply_overlay(base, settings, "union_weight"),
            }
            sys.path.insert(0, str(REPO / "scripts"))
            import audit_method_config_diff as audit_mod
            allow = audit_mod.load_allowlist(ALLOWLIST)
            failures = audit_mod.audit(configs, allow, "main")
            self.assertTrue(any("max_cnt" in f for f in failures), failures)

    def test_missing_key_declaration_is_caught(self):
        # A method that omits a key relies on a C++ default, which hides confounds.
        base = VIODE_BASE.read_text()
        stripped = "\n".join(line for line in base.splitlines()
                             if not line.startswith("imu_adaptive_covariance:"))
        sys.path.insert(0, str(REPO / "scripts"))
        import audit_method_config_diff as audit_mod
        allow = audit_mod.load_allowlist(ALLOWLIST)
        failures = audit_mod.audit({"baseline": base, "other": stripped}, allow, "main")
        self.assertTrue(any("imu_adaptive_covariance" in f for f in failures), failures)


class TestGenerator(unittest.TestCase):
    def test_generates_resolved_config_and_provenance(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "run" / "resolved_config.yaml"
            result = run([str(GENERATE), "--base", str(VIODE_BASE),
                          "--overlay", str(OVERLAYS / "union_weight.yaml"),
                          "--out", str(out)])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(out.is_file())
            keys = paper_config.parse_scalars(out.read_text())
            self.assertEqual(keys["sem_geodf_backend_weight"], "1")
            self.assertEqual(keys["visual_adaptive_quality"], "0")
            provenance = out.with_suffix(out.suffix + ".provenance.json")
            self.assertTrue(provenance.is_file())
            import json
            data = json.loads(provenance.read_text())
            self.assertEqual(data["method"], "U+W")
            self.assertEqual(data["resolved_sha256"],
                             paper_config.sha256_text(out.read_text()))

    def test_rejects_overlay_touching_keys_it_does_not_own(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            overlay = tmp / "geodf.yaml"
            overlay.write_text("geodf_enable: 1\nmax_cnt: 999\n")
            result = run([str(GENERATE), "--base", str(VIODE_BASE),
                          "--overlay", str(overlay),
                          "--out", str(tmp / "resolved_config.yaml")])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("does not own", result.stderr)

    def test_rejects_set_of_protected_keys(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "resolved_config.yaml"
            result = run([str(GENERATE), "--base", str(VIODE_BASE),
                          "--overlay", str(OVERLAYS / "baseline.yaml"),
                          "--out", str(out), "--set", "max_cnt=999"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("protected key", result.stderr)

    def test_rejects_undeclared_overlay(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            overlay = tmp / "sneaky_method.yaml"
            overlay.write_text("sem_enable: 1\n")
            result = run([str(GENERATE), "--base", str(VIODE_BASE),
                          "--overlay", str(overlay),
                          "--out", str(tmp / "resolved_config.yaml")])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not declared", result.stderr)

    def test_all_six_overlays_resolve(self):
        with tempfile.TemporaryDirectory() as tmp:
            for base in (VIODE_BASE, EUROC_BASE):
                for overlay in sorted(OVERLAYS.glob("*.yaml")):
                    out = Path(tmp) / base.stem / overlay.stem / "resolved_config.yaml"
                    result = run([str(GENERATE), "--base", str(base),
                                  "--overlay", str(overlay), "--out", str(out)])
                    self.assertEqual(result.returncode, 0,
                                     f"{base.name}/{overlay.name}: {result.stderr}")


if __name__ == "__main__":
    unittest.main()
