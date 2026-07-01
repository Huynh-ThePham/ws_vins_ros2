#!/usr/bin/env python3
"""Apply selected Semantic policy parameters to a VINS YAML config.

The config files in this repository are flat OpenCV/YAML-style key-value files.
This helper updates only the allowlisted semantic policy keys emitted by
scripts/tune_sem_policy.py. It intentionally does not touch oracle controls such
as sem_policy_dynamic_level.
"""
from __future__ import annotations

import argparse
from pathlib import Path


ALLOWED_KEYS = {
    "sem_policy_burst_ratio",
    "sem_policy_strong_ratio",
    "sem_policy_hold_frames",
    "sem_policy_overlap_ratio",
    "sem_policy_overlap_ema",
    "sem_policy_min_geo_candidates",
}


def load_params(path: Path) -> dict[str, str]:
    params: dict[str, str] = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        key, value = line.split(":", 1)
        key = key.strip()
        if key in ALLOWED_KEYS:
            params[key] = value.strip()
    return params


def apply_params(config: Path, params: dict[str, str]) -> list[str]:
    lines = config.read_text().splitlines()
    seen: set[str] = set()
    out: list[str] = []

    for line in lines:
        stripped = line.lstrip()
        indent = line[: len(line) - len(stripped)]
        if ":" in stripped:
            key = stripped.split(":", 1)[0].strip()
            if key in params:
                out.append(f"{indent}{key}: {params[key]}")
                seen.add(key)
                continue
        out.append(line)

    missing = [key for key in ALLOWED_KEYS if key in params and key not in seen]
    if missing:
        out.append("")
        out.append("# Applied train-selected semantic policy parameters")
        for key in sorted(missing):
            out.append(f"{key}: {params[key]}")

    config.write_text("\n".join(out) + "\n")
    return sorted(seen | set(missing))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--config", type=Path, required=True)
    ap.add_argument("--params", type=Path, required=True)
    args = ap.parse_args()

    params = load_params(args.params)
    if not params:
        raise SystemExit(f"No allowed semantic policy parameters in {args.params}")
    applied = apply_params(args.config, params)
    print(f"[sem_policy] applied {len(applied)} params to {args.config}")


if __name__ == "__main__":
    main()
