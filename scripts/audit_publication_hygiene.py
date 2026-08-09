#!/usr/bin/env python3
"""Static audit of the publication path (plan P0.3, protocol-audit CI job).

Guards the properties that are easy to reintroduce by accident:

  1. No `|| true` in a publication script, except for cleanup (killall/kill/pkill/rm)
     and clearly-marked log-copy convenience.
  2. No silent `continue`/`return` on an ATE threshold: a diverged run must be
     RECORDED as diverged, never dropped.
  3. No oracle default: sem_policy_dynamic_level must be -1 in every paper config.
  4. The asset generator must gate on the expected-matrix validator.
  5. Manifests must record a full git SHA and the dirty state.

    python3 scripts/audit_publication_hygiene.py

Exit codes: 0 = PASS, 1 = FAIL.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Scripts whose failure would corrupt a published number.
PUBLICATION_SCRIPTS = [
    "scripts/run_sem_geodf_ablation.sh",
    "scripts/run_sem_geodf_full_rerun.sh",
    "scripts/run_full_ablation_once.sh",
    "scripts/run_paper_n5_postfix_matrix.sh",
    "scripts/run_paper_n5_sem_policy_matrix.sh",
    "scripts/run_paper_postfix_n3_resume.sh",
    "scripts/run_sem_policy_protocol.sh",
    "scripts/queue_euroc_after_viode.sh",
]

# `|| true` is legitimate only when the command being tolerated is cleanup.
CLEANUP_COMMANDS = ("killall", "kill ", "pkill", "rm ", "rmdir", "unlink", "shift")
CLEANUP_MARKER = "convenience only"

ASSET_SCRIPTS = [
    "scripts/make_sem_geodf_paper_assets.py",
]

PAPER_CONFIG_DIR = REPO / "src/config/paper"


def check_no_publication_or_true(failures: list[str]) -> None:
    for rel in PUBLICATION_SCRIPTS:
        path = REPO / rel
        if not path.is_file():
            continue
        lines = path.read_text().splitlines()
        for number, line in enumerate(lines, start=1):
            stripped = line.strip()
            if "|| true" not in stripped or stripped.startswith("#"):
                continue
            if any(cmd in stripped for cmd in CLEANUP_COMMANDS):
                continue
            previous = lines[number - 2].strip() if number >= 2 else ""
            if CLEANUP_MARKER in previous or CLEANUP_MARKER in stripped:
                continue
            failures.append(
                f"{rel}:{number}: `|| true` on the publication path masks a real failure "
                f"-> {stripped}\n      Allowed only for cleanup (killall/kill/pkill/rm) or "
                f"a line marked '{CLEANUP_MARKER}'.")


def check_no_silent_ate_filter(failures: list[str]) -> None:
    # Any `continue`/`return`/`pass` guarded by an ATE magnitude comparison.
    pattern = re.compile(r"(ate|ATE)\w*\s*[<>]=?\s*[\d.]+")
    for path in sorted((REPO / "scripts").rglob("*.py")):
        if "tests" in path.parts:
            continue
        lines = path.read_text().splitlines()
        for number, line in enumerate(lines, start=1):
            if not pattern.search(line) or line.strip().startswith("#"):
                continue
            window = " ".join(lines[number - 1: number + 3])
            if re.search(r"\b(continue|return\s+None|pass)\b", window) and \
                    "excluded[" not in window and "diverged" not in window.lower():
                rel = path.relative_to(REPO)
                failures.append(
                    f"{rel}:{number}: a run appears to be dropped on an ATE threshold "
                    f"without being recorded -> {line.strip()}\n      Record it as "
                    f"status 'diverged' with failure_reason 'ate_threshold_exceeded' and "
                    f"report the success rate instead.")


def check_no_oracle_default(failures: list[str]) -> None:
    if not PAPER_CONFIG_DIR.is_dir():
        failures.append("src/config/paper is missing; the fair-config framework is required")
        return
    for path in sorted(PAPER_CONFIG_DIR.glob("*_common.yaml")):
        found = False
        for line in path.read_text().splitlines():
            match = re.match(r"^sem_policy_dynamic_level\s*:\s*(-?\d+)", line.strip())
            if not match:
                continue
            found = True
            if match.group(1) != "-1":
                failures.append(
                    f"{path.relative_to(REPO)}: sem_policy_dynamic_level="
                    f"{match.group(1)} feeds the scene's ground-truth dynamic level into "
                    f"the online estimator. It must be -1.")
        if not found:
            failures.append(f"{path.relative_to(REPO)}: does not declare "
                            f"sem_policy_dynamic_level; the audit cannot verify it")


def check_asset_scripts_are_gated(failures: list[str]) -> None:
    for rel in ASSET_SCRIPTS:
        path = REPO / rel
        if not path.is_file():
            failures.append(f"{rel} is missing")
            continue
        text = path.read_text()
        if "validation.json" not in text:
            failures.append(
                f"{rel}: does not check for validation.json, so publication assets could "
                f"be built from a run tree that never passed "
                f"scripts/validate_experiment_matrix.py.")
        if "validation_receipt" not in text and "require-receipt" not in text and \
                "require_receipt" not in text:
            failures.append(
                f"{rel}: does not require validation_receipt.json; paper numbers could be "
                f"emitted without a hashed matrix receipt.")


def check_manifest_provenance(failures: list[str]) -> None:
    path = REPO / "scripts/write_run_manifest.py"
    if not path.is_file():
        failures.append("scripts/write_run_manifest.py is missing")
        return
    text = path.read_text()
    if "rev-parse\", \"HEAD\"" not in text and "'rev-parse', 'HEAD'" not in text:
        failures.append("scripts/write_run_manifest.py: must record the FULL git SHA "
                        "(git rev-parse HEAD), not --short")
    if "--short" in text:
        failures.append("scripts/write_run_manifest.py: still uses `git rev-parse --short`; "
                        "a short SHA cannot identify a commit unambiguously")
    for field in ("git_dirty", "resolved_config_sha256", "bag_sha256", "model_sha256"):
        if field not in text:
            failures.append(f"scripts/write_run_manifest.py: does not record {field}")


def check_mask_freshness_is_honest(failures: list[str]) -> None:
    """Plan P1.8: a reused mask must keep its original stamp."""
    path = REPO / "src/yolo_dynamic_mask/yolo_dynamic_mask/mask_node.py"
    if not path.is_file():
        failures.append("src/yolo_dynamic_mask/.../mask_node.py is missing")
        return
    text = path.read_text()

    if "self.last_mask_msg" not in text:
        failures.append(
            "mask_node.py: does not store the mask MESSAGE (last_mask_msg). Storing only "
            "the array forces the reuse path to build a new header, which is how a stale "
            "mask acquired a fresh stamp and defeated sem_mask_max_age_ms.")

    match = re.search(r"def _republish_last_mask\(self.*?\n(?=\n    def |\nclass |\Z)",
                      text, re.S)
    if not match:
        failures.append("mask_node.py: no _republish_last_mask(); the reuse path must be "
                        "explicit so it can be audited")
    else:
        body = match.group(0)
        if re.search(r"header\.stamp\s*=", body) or re.search(r"\.header\s*=\s*msg\.header", body):
            failures.append(
                "mask_node.py:_republish_last_mask: assigns a header/stamp. A reused mask "
                "must be republished UNCHANGED, or the estimator's freshness check is "
                "meaningless.")
        if "self.last_mask_msg" not in body:
            failures.append("mask_node.py:_republish_last_mask: does not publish the stored "
                            "message")

    for topic in ("source_stamp", "inference_finish_stamp", "reused", "model_latency_ms"):
        if topic not in text:
            failures.append(f"mask_node.py: missing the /{topic} diagnostic the plan requires "
                            f"for measuring mask age and reuse from a bag")

    if "worker_thread" not in text or "latest_lock" not in text:
        failures.append("mask_node.py: no latest-frame worker (worker_thread/latest_lock). A "
                        "`busy` flag in the callback drops frames but does not implement "
                        "latest-only.")
    if re.search(r"self\.busy\s*=", text):
        failures.append("mask_node.py: still uses a `busy` flag; replace it with the "
                        "latest-frame worker handoff")
    if "mean_model_latency_ms" not in text and "inference_count" not in text:
        failures.append("mask_node.py: latency must be averaged over inference_count, not "
                        "frame_count (including reused frames)")


def check_dependencies_are_locked(failures: list[str]) -> None:
    """Plan P1.10: lower bounds mean the same repo produces different results."""
    lock = REPO / "requirements-lock.txt"
    if not lock.is_file():
        failures.append("requirements-lock.txt is missing; ultralytics>=8.3.0 lets the same "
                        "repo produce different results depending on install date")
        return
    for line in lock.read_text().splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if "==" not in stripped:
            failures.append(f"requirements-lock.txt: '{stripped}' is not pinned with ==")

    test_req = REPO / "scripts/requirements-test.txt"
    if not test_req.is_file():
        failures.append("scripts/requirements-test.txt is missing; CI Python tests must pin deps")
    else:
        for line in test_req.read_text().splitlines():
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            if "==" not in stripped:
                failures.append(f"scripts/requirements-test.txt: '{stripped}' is not pinned")

    manifest = REPO / "models/model_manifest.json"
    if not manifest.is_file():
        failures.append("models/model_manifest.json is missing; the segmentation model would "
                        "be unidentified in every semantic run")
        return
    import json as _json
    try:
        data = _json.loads(manifest.read_text())
    except (OSError, ValueError) as exc:
        failures.append(f"models/model_manifest.json is unreadable: {exc}")
        return
    for field in ("model_name", "sha256", "ultralytics_version", "torch_version",
                  "dynamic_classes", "confidence_threshold", "image_size"):
        if not data.get(field):
            failures.append(f"models/model_manifest.json: missing {field}")
    if data.get("sha256") and len(str(data["sha256"])) != 64:
        failures.append("models/model_manifest.json: sha256 is not a full 64-char digest")


def check_fair_config_is_wired(failures: list[str]) -> None:
    """P0.1: the live ablation runner must generate resolved configs, not copy legacy YAML."""
    path = REPO / "scripts/run_sem_geodf_ablation.sh"
    if not path.is_file():
        failures.append("scripts/run_sem_geodf_ablation.sh is missing")
        return
    text = path.read_text()
    if "generate_paper_config.py" not in text:
        failures.append("run_sem_geodf_ablation.sh never calls generate_paper_config.py")
    if "resolved_config.yaml" not in text:
        failures.append("run_sem_geodf_ablation.sh does not write resolved_config.yaml")
    if re.search(r"cp\s+.*\$\{?(EUROC|VIODE)_CFG\}?/[a-z]+_\$\{?mode\}?", text) or \
            re.search(r"cp\s+\".*/(euroc|viode)_\$\{mode\}_config\.yaml\"", text) or \
            "euroc_${mode}_config.yaml" in text or "viode_${mode}_config.yaml" in text:
        failures.append(
            "run_sem_geodf_ablation.sh still copies legacy per-method YAMLs from "
            "src/config/{euroc,viode}/; publication must use base+overlay only")
    if 'METHODS="${METHODS:-baseline adaptive sad_sem sem_geodf}"' in text or \
            "METHODS:-baseline adaptive" in text:
        failures.append(
            "run_sem_geodf_ablation.sh still defaults to the confounded method list "
            "(adaptive/sad_sem/sem_geodf); expected baseline geodf semantic "
            "union_noweight union_weight")
    if "validate_reusable_run.py" not in text:
        failures.append("run_sem_geodf_ablation.sh does not call validate_reusable_run.py "
                        "before skipping an existing cell")
    if "PUBLICATION_MODE" not in text:
        failures.append("run_sem_geodf_ablation.sh missing PUBLICATION_MODE fail-closed switch")


def check_no_runtime_gt_leak(failures: list[str]) -> None:
    """Phase 3.6 §30: production code must not branch on GT / sequence name."""
    prod_roots = [
        REPO / "src/pht_vio/src",
        REPO / "src/pht_vio_ros/src",
    ]
    # Patterns that indicate online use of ground truth or scene identity.
    leak_re = re.compile(
        r"(ground[_ ]?truth|gt_pose|oracle_pose|read_gt|"
        r"sequence[_-]?name|difficulty[_-]?branch|"
        r"if\s*\(.*MH_0[0-9]|if\s*\(.*city_(day|night))",
        re.IGNORECASE,
    )
    allow_fragments = (
        "test/",
        "docs/",
        "scripts/",
        "eval",
        "evaluate",
        # Comments documenting the prohibition are fine.
    )
    for root in prod_roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if path.suffix not in {".cpp", ".h", ".hpp", ".cc"}:
                continue
            rel = str(path.relative_to(REPO))
            if any(frag in rel for frag in ("test/", "docs/")):
                continue
            text = path.read_text(errors="ignore")
            for number, line in enumerate(text.splitlines(), start=1):
                stripped = line.strip()
                if stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*"):
                    continue
                if leak_re.search(stripped):
                    # Allow string literals that only appear in log messages about
                    # forbidding GT — still flag actual identifiers.
                    if "must not" in stripped.lower() or "no gt" in stripped.lower():
                        continue
                    failures.append(
                        f"{rel}:{number}: possible runtime GT/sequence leak -> {stripped}")


def check_hold_frames_not_publication_primary(failures: list[str]) -> None:
    for path in sorted((REPO / "src/config/paper").glob("*_common.yaml")):
        text = path.read_text()
        if "sem_policy_assist_hold_s:" not in text:
            failures.append(f"{path.relative_to(REPO)}: missing timestamp hold "
                            f"sem_policy_assist_hold_s")
        # hold_frames may exist for compatibility, but publication must declare hold_s > 0.
        for line in text.splitlines():
            if line.startswith("sem_policy_assist_hold_s:"):
                value = line.split(":", 1)[1].split("#", 1)[0].strip()
                try:
                    if float(value) <= 0:
                        failures.append(f"{path.relative_to(REPO)}: assist_hold_s must be > 0 "
                                        f"for publication")
                except ValueError:
                    failures.append(f"{path.relative_to(REPO)}: unreadable assist_hold_s")


def check_validator_exists(failures: list[str]) -> None:
    for rel in ("scripts/validate_experiment_matrix.py",
                "scripts/audit_method_config_diff.py",
                "scripts/generate_paper_config.py",
                "scripts/validate_reusable_run.py",
                "src/config/paper/expected_matrix.json",
                "experiments/sem_geodf_expected_matrix.json",
                "src/config/paper/allowed_differences.yaml"):
        if not (REPO / rel).exists():
            failures.append(f"{rel} is missing; the fail-closed protocol depends on it")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.parse_args()

    failures: list[str] = []
    check_validator_exists(failures)
    check_no_publication_or_true(failures)
    check_no_silent_ate_filter(failures)
    check_no_oracle_default(failures)
    check_asset_scripts_are_gated(failures)
    check_manifest_provenance(failures)
    check_mask_freshness_is_honest(failures)
    check_dependencies_are_locked(failures)
    check_fair_config_is_wired(failures)
    check_hold_frames_not_publication_primary(failures)
    check_no_runtime_gt_leak(failures)

    if failures:
        print(f"FAIL: {len(failures)} publication-hygiene violation(s)\n")
        for message in failures:
            print(f"  - {message}")
        return 1

    print("PASS: publication path is fail-closed, no oracle default, assets are gated, "
          "manifests carry full provenance, mask reuse keeps its original stamp, "
          "dependencies and model are pinned, fair configs drive the live runner.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
