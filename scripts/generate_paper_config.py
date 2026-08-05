#!/usr/bin/env python3
"""Resolve a paper method config from one common base plus one method overlay.

Plan P0.1: no per-method copy-pasted YAML. Every run directory holds the exact
resolved config the estimator was given, plus its sha256, so a manifest can prove
which configuration produced a trajectory.

    python3 scripts/generate_paper_config.py \
      --base src/config/paper/viode_common.yaml \
      --overlay src/config/paper/overlays/union_weight.yaml \
      --out <run_dir>/resolved_config.yaml

Exits non-zero on any problem: this sits on the publication path and must be
fail-closed.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))

from paper_config import (  # noqa: E402
    apply_overlay,
    parse_overlay,
    parse_scalars,
    sha256_text,
)

REPO = Path(__file__).resolve().parent.parent
ALLOWLIST = REPO / "src/config/paper/allowed_differences.yaml"


def load_allowlist(path: Path) -> dict[str, list[str]]:
    out: dict[str, list[str]] = {}
    if not path.exists():
        return out
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        key, value = line.split(":", 1)
        out[key.strip()] = [v.strip() for v in value.split(",") if v.strip()]
    return out


def required_values(allowlist: dict[str, list[str]]) -> dict[str, str]:
    out: dict[str, str] = {}
    for item in allowlist.get("__required_values__", []):
        if "=" in item:
            key, value = item.split("=", 1)
            out[key.strip()] = value.strip()
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True, type=Path,
                    help="Common backbone, e.g. src/config/paper/viode_common.yaml")
    ap.add_argument("--overlay", required=True, type=Path,
                    help="Method overlay from src/config/paper/overlays/")
    ap.add_argument("--out", required=True, type=Path,
                    help="Resolved config to write (usually <run_dir>/resolved_config.yaml)")
    ap.add_argument("--provenance", type=Path, default=None,
                    help="Optional JSON sidecar (defaults to <out>.provenance.json)")
    ap.add_argument("--allow-new-keys", action="store_true",
                    help="Permit overlay keys absent from the base. Off by default: a "
                         "fair base must declare every knob, including the off values.")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE",
                    help="Per-run non-algorithmic override (output_path, topics, stats "
                         "paths). Rejected for any key the allowlist calls protected.")
    args = ap.parse_args()

    for path in (args.base, args.overlay):
        if not path.is_file():
            print(f"[fatal] missing file: {path}", file=sys.stderr)
            return 2

    base_text = args.base.read_text()
    base_keys = parse_scalars(base_text)
    overlay, meta = parse_overlay(args.overlay)

    allowlist = load_allowlist(ALLOWLIST)
    overlay_name = args.overlay.stem
    permitted = allowlist.get(overlay_name)
    if permitted is None:
        print(f"[fatal] overlay '{overlay_name}' is not declared in {ALLOWLIST.name}. "
              f"Add it (with the keys it owns) before generating paper configs.",
              file=sys.stderr)
        return 3

    illegal = sorted(k for k in overlay if k not in permitted)
    if illegal:
        print(f"[fatal] overlay '{overlay_name}' changes keys it does not own: "
              f"{', '.join(illegal)}\n"
              f"        permitted: {', '.join(permitted) or '(none)'}", file=sys.stderr)
        return 4

    unknown = sorted(k for k in overlay if k not in base_keys)
    if unknown and not args.allow_new_keys:
        print(f"[fatal] overlay '{overlay_name}' sets keys absent from the base: "
              f"{', '.join(unknown)}\n"
              f"        declare them in {args.base.name} (at their off value) so the "
              f"config diff audit can see them.", file=sys.stderr)
        return 5

    # Per-run overrides: paths and topics only, never algorithm settings.
    protected = set(allowlist.get("__forbidden_differences__", []))
    protected.update(permitted)
    protected.update(required_values(allowlist))
    overrides: dict[str, str] = {}
    for item in args.set:
        if "=" not in item:
            print(f"[fatal] --set expects KEY=VALUE, got '{item}'", file=sys.stderr)
            return 6
        key, value = item.split("=", 1)
        key, value = key.strip(), value.strip()
        if key in protected:
            print(f"[fatal] --set may not touch protected key '{key}'", file=sys.stderr)
            return 7
        overrides[key] = value

    merged = dict(overlay)
    merged.update(overrides)
    resolved = apply_overlay(base_text, merged, overlay_name)

    # Post-condition: the resolved config really does carry the required values.
    resolved_keys = parse_scalars(resolved)
    for key, expected in required_values(allowlist).items():
        actual = resolved_keys.get(key)
        if actual is None:
            print(f"[fatal] resolved config is missing required key '{key}'", file=sys.stderr)
            return 8
        if actual != expected:
            print(f"[fatal] resolved config has {key}={actual}, protocol requires "
                  f"{expected}", file=sys.stderr)
            return 9

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(resolved)

    provenance = args.provenance or args.out.with_suffix(args.out.suffix + ".provenance.json")
    provenance.write_text(json.dumps({
        "method": meta.get("method", overlay_name),
        "label": meta.get("label", overlay_name),
        "overlay": overlay_name,
        "base": str(args.base.relative_to(REPO)) if args.base.is_absolute() else str(args.base),
        "base_sha256": sha256_text(base_text),
        "overlay_sha256": sha256_text(args.overlay.read_text()),
        "resolved_sha256": sha256_text(resolved),
        "overlay_settings": overlay,
        "run_overrides": overrides,
    }, indent=2, sort_keys=True) + "\n")

    print(f"[ok] {overlay_name} -> {args.out}")
    print(f"     resolved_sha256={sha256_text(resolved)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
