#!/usr/bin/env python3
"""Independently verify a reproducibility bundle (plan P2.3).

    python3 scripts/check_reproducibility_bundle.py --root results/sem_geodf_ablation/<tag>
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REQUIRED = [
    "validation.json",
    "validation_receipt.json",
    "ABLATION_ANALYSIS.csv",
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path, required=True)
    args = ap.parse_args()
    root = args.root
    errors = []
    if not root.is_dir():
        print(f"[fatal] missing root {root}", file=sys.stderr)
        return 2
    for name in REQUIRED:
        if not (root / name).is_file():
            errors.append(f"missing {name}")
    manifests = list(root.rglob("run_manifest.json"))
    if not manifests:
        errors.append("no run_manifest.json")
    for mpath in manifests:
        run_dir = mpath.parent
        for artifact in ("resolved_config.yaml",):
            if not (run_dir / artifact).is_file():
                errors.append(f"{run_dir.relative_to(root)}: missing {artifact}")
        try:
            data = json.loads(mpath.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"{mpath}: {exc}")
            continue
        for field in ("git_sha", "resolved_config_sha256", "bag_sha256", "status",
                      "protocol_version", "method", "dataset", "scene", "trial", "seed"):
            if data.get(field) in (None, ""):
                errors.append(f"{mpath.relative_to(root)}: missing {field}")
        if (run_dir / "failure_status.json").is_file() and data.get("status") == "ok":
            errors.append(f"{mpath.relative_to(root)}: failure_status present but status=ok")
    receipt = root / "validation_receipt.json"
    if receipt.is_file():
        payload = json.loads(receipt.read_text())
        if payload.get("result") != "PASS":
            errors.append("validation_receipt is not PASS")
        if not payload.get("manifests_sha256"):
            errors.append("validation_receipt missing manifests_sha256")

    if errors:
        print(f"FAIL: {len(errors)} bundle problem(s)")
        for e in errors:
            print(f"  - {e}")
        return 1
    print(f"PASS: reproducibility bundle under {root} ({len(manifests)} manifests)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
