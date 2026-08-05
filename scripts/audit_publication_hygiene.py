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


def check_validator_exists(failures: list[str]) -> None:
    for rel in ("scripts/validate_experiment_matrix.py",
                "scripts/audit_method_config_diff.py",
                "scripts/generate_paper_config.py",
                "src/config/paper/expected_matrix.json",
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

    if failures:
        print(f"FAIL: {len(failures)} publication-hygiene violation(s)\n")
        for message in failures:
            print(f"  - {message}")
        return 1

    print("PASS: publication path is fail-closed, no oracle default, assets are gated, "
          "manifests carry full provenance.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
