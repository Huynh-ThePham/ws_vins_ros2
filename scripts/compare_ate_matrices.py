#!/usr/bin/env python3
"""Compare two frozen ATE matrices produced by summarize_before_ate.py.

Reads the two JSON summaries and reports, per (scene, method) cell, the change in
median ATE, success rate, coverage and failure-penalized ATE. A cell is only
called an improvement when the median drops AND neither the success rate nor the
trajectory coverage regresses, so a shorter or partly failed run cannot be
presented as a win.
"""
from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path

# A cell whose coverage drops by more than this fraction is not comparable: the
# trajectory got shorter, so its ATE is measured over different ground.
COVERAGE_TOLERANCE = 0.02


def load(path: Path) -> dict[tuple[str, str], dict]:
    rows = json.loads(path.read_text())
    return {(r["scene"], r["method"]): r for r in rows}


def verdict(before: dict, after: dict) -> str:
    if after["ok"] == 0:
        return "BROKEN"
    if before["median_ate"] is None or after["median_ate"] is None:
        return "n/a"
    cov_b = before["coverage_median_rows"] or 0.0
    cov_a = after["coverage_median_rows"] or 0.0
    if cov_b > 0 and cov_a < cov_b * (1.0 - COVERAGE_TOLERANCE):
        return "COVERAGE LOSS"
    if after["success_rate"] < before["success_rate"]:
        return "SR REGRESSION"
    delta = after["median_ate"] - before["median_ate"]
    rel = delta / before["median_ate"] if before["median_ate"] else 0.0
    if rel <= -0.02:
        return "better"
    if rel >= 0.02:
        return "worse"
    return "unchanged"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--before", type=Path, required=True, help="BEFORE *.json summary")
    ap.add_argument("--after", type=Path, required=True, help="AFTER *.json summary")
    ap.add_argument("--before-label", default="BEFORE")
    ap.add_argument("--after-label", default="AFTER")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    before = load(args.before)
    after = load(args.after)
    keys = sorted(set(before) | set(after))

    lines = [
        f"# {args.before_label} vs {args.after_label}",
        "",
        f"| scene | method | {args.before_label} median | {args.after_label} median | "
        "Δ | Δ% | SR | coverage | pen. ATE | verdict |",
        "|---|---|---:|---:|---:|---:|---|---|---|---|",
    ]
    rels: dict[str, list[float]] = {}
    for key in keys:
        scene, method = key
        b, a = before.get(key), after.get(key)
        if b is None or a is None:
            side = args.after_label if b is None else args.before_label
            lines.append(f"| {scene} | {method} | — | — | — | — | — | — | — | only in {side} |")
            continue
        bm, am = b["median_ate"], a["median_ate"]
        if bm is None or am is None:
            delta_s = rel_s = "—"
        else:
            delta = am - bm
            rel = delta / bm if bm else 0.0
            rels.setdefault(method, []).append(rel)
            delta_s = f"{delta:+.4f}"
            rel_s = f"{rel * 100:+.1f}%"
        lines.append(
            f"| {scene} | {method} | "
            f"{bm:.4f} | {am:.4f} | {delta_s} | {rel_s} | "
            f"{b['success_rate']:.2f}→{a['success_rate']:.2f} | "
            f"{b['coverage_median_rows']:.0f}→{a['coverage_median_rows']:.0f} | "
            f"{b['failure_penalized_ate']:.4f}→{a['failure_penalized_ate']:.4f} | "
            f"{verdict(b, a)} |"
        )

    lines += ["", "## Aggregate relative change in median ATE", ""]
    lines += ["| method | cells | mean Δ% | median Δ% |", "|---|---:|---:|---:|"]
    for method, values in sorted(rels.items()):
        lines.append(
            f"| {method} | {len(values)} | {statistics.mean(values) * 100:+.1f}% | "
            f"{statistics.median(values) * 100:+.1f}% |"
        )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
