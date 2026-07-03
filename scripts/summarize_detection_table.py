#!/usr/bin/env python3
"""Aggregate VIODE detection_eval.json dumps into DETECTION_EVAL_VIODE.md."""
import glob
import json
from pathlib import Path


def parse_name(name: str) -> tuple[str, str]:
    for level in ("0_none", "1_low", "2_mid", "3_high"):
        suf = f"_{level}"
        if name.endswith(suf):
            return name[: -len(suf)], level
    return name, ""


def main() -> None:
    rows = []
    for pat in (
        "results/viode_detection/*_adaptive_dump/detection_eval.json",
        "results/viode_detection/*_geodf_dump/detection_eval.json",
    ):
        for p in sorted(glob.glob(pat)):
            d = json.loads(Path(p).read_text())
            base = Path(p).parent.name.replace("_adaptive_dump", "").replace("_geodf_dump", "")
            env, level = parse_name(base)
            rows.append((env, level, d))

    lines = [
        "# VIODE dynamic-feature detection (GeoDF-Adaptive, stereo 3D)",
        "",
        "Ground truth: VIODE moving-vehicle segmentation masks.",
        "",
        "| env | level | dyn base-rate | precision | lift | recall | static-FPR |",
        "|---|---|---:|---:|---:|---:|---:|",
    ]
    seen: set[tuple[str, str]] = set()
    for env, level, d in rows:
        key = (env, level)
        if key in seen:
            continue
        seen.add(key)

        def fmt(k: str, pct: bool = False, x: bool = False) -> str:
            v = d.get(k)
            if v is None or (isinstance(v, float) and v != v):
                return "n/a"
            if not isinstance(v, (int, float)):
                return "n/a"
            if x:
                return f"{v:.2f}x"
            return f"{v * 100:.2f}%" if pct else f"{v:.3f}"

        br = d.get("dynamic_base_rate")
        brs = f"{br * 100:.2f}%" if isinstance(br, (int, float)) else "n/a"
        lines.append(
            f"| {env} | {level} | {brs} | {fmt('precision', pct=True)} | "
            f"{fmt('precision_lift', x=True)} | {fmt('recall', pct=True)} | "
            f"{fmt('static_fpr', pct=True)} |"
        )

    out = Path("results/geodf_evaluation/DETECTION_EVAL_VIODE.md")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")
    print(f"[ok] {out} ({len(seen)} cells)")


if __name__ == "__main__":
    main()
