#!/usr/bin/env python3
"""Fail if two paper methods differ in anything but the keys they own (plan P0.1).

This is the guard against the confounded ablation: Sem-GeoDF configs previously
enabled visual_adaptive_quality and imu_adaptive_covariance while the baselines
did not, so no improvement could be attributed to Sem-GeoDF itself.

Two modes:

  # audit the checked-in overlays against their common base (CI default)
  python3 scripts/audit_method_config_diff.py --base src/config/paper/viode_common.yaml

  # audit the resolved configs actually used by a completed run tree
  python3 scripts/audit_method_config_diff.py --resolved results/<tag>

Exit codes: 0 = PASS, non-zero = FAIL. Nothing is warned-and-continued.
"""
from __future__ import annotations

import argparse
import sys
from itertools import combinations
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))

from paper_config import (  # noqa: E402
    apply_overlay,
    parse_matrix_blocks,
    parse_overlay,
    parse_scalars,
    values_equal,
)

REPO = Path(__file__).resolve().parent.parent
DEFAULT_ALLOWLIST = REPO / "src/config/paper/allowed_differences.yaml"
OVERLAY_DIR = REPO / "src/config/paper/overlays"

# The main comparison matrix, in paper order.
MAIN_MATRIX = ["baseline", "geodf", "semantic", "union_noweight", "union_weight"]


def load_allowlist(path: Path) -> dict[str, list[str]]:
    out: dict[str, list[str]] = {}
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


class Failure(list):
    def add(self, message: str) -> None:
        self.append(message)


def audit(configs: dict[str, str], allowlist: dict[str, list[str]],
          matrix: str) -> Failure:
    """configs: method name -> resolved config text."""
    failures = Failure()
    forbidden = set(allowlist.get("__forbidden_differences__", []))
    uniform_keys = set(allowlist.get("__uniform_adaptation_keys__", []))
    required = required_values(allowlist)

    scalars = {name: parse_scalars(text) for name, text in configs.items()}
    matrices = {name: parse_matrix_blocks(text) for name, text in configs.items()}

    # 1. Oracle / protocol keys must hold their required value everywhere.
    for name, keys in scalars.items():
        for key, expected in required.items():
            actual = keys.get(key)
            if actual is None:
                failures.add(f"{name}: missing required key '{key}' (expected {expected})")
            elif not values_equal(actual, expected):
                failures.add(f"{name}: {key}={actual} but the protocol requires {expected}")

    # 2. Every method must declare the same key set. A missing key silently falls
    #    back to a C++ default, which hides differences from this very audit.
    all_keys = set().union(*scalars.values()) if scalars else set()
    for name, keys in scalars.items():
        missing = sorted(all_keys - set(keys))
        if missing:
            failures.add(f"{name}: does not declare {len(missing)} key(s) other methods do "
                         f"(relying on C++ defaults hides confounds): {', '.join(missing[:12])}"
                         + (" ..." if len(missing) > 12 else ""))

    # 3. Visual/IMU factor adaptation is either off for the whole matrix, or on for
    #    the whole matrix. Never on for one method only -- that is precisely the
    #    confound this audit exists to prevent.
    if matrix == "full-adaptation":
        for key in sorted(uniform_keys):
            values = {name: keys.get(key) for name, keys in scalars.items()}
            if any(v != "1" for v in values.values()):
                off = sorted(n for n, v in values.items() if v != "1")
                failures.add(f"--matrix full-adaptation requires {key}=1 for every method; "
                             f"off for: {', '.join(off)}")
    else:
        for key in sorted(uniform_keys):
            on = sorted(name for name, keys in scalars.items() if keys.get(key) == "1")
            if on and len(on) != len(scalars):
                failures.add(
                    f"'{key}' is enabled for {', '.join(on)} but not for every method in the "
                    f"main matrix. Factor adaptation is a separate contribution: either run it "
                    f"for all methods (--matrix full-adaptation) or for none. Auditing the "
                    f"'full_adaptive' row together with the main matrix is exactly the "
                    f"confounded comparison this check blocks.")

    # 4. Pairwise diff against the ownership allowlist.
    for lhs, rhs in combinations(sorted(configs), 2):
        owned = set(allowlist.get(lhs, [])) | set(allowlist.get(rhs, []))
        for key in sorted(set(scalars[lhs]) | set(scalars[rhs])):
            a, b = scalars[lhs].get(key), scalars[rhs].get(key)
            if a is None or b is None or values_equal(a, b):
                continue
            if key in forbidden:
                failures.add(f"{lhs} vs {rhs}: '{key}' differs ({a} vs {b}) but is a shared "
                             f"protocol key that may never differ between methods")
            elif matrix == "full-adaptation" and key in uniform_keys:
                failures.add(f"{lhs} vs {rhs}: '{key}' differs ({a} vs {b}) in the "
                             f"full-adaptation matrix, where it must be on for all")
            elif key not in owned:
                failures.add(f"{lhs} vs {rhs}: '{key}' differs ({a} vs {b}) and is owned by "
                             f"neither method -- this is a confound")

        # Calibration blocks must match exactly within a dataset.
        for key in sorted(set(matrices[lhs]) | set(matrices[rhs])):
            if matrices[lhs].get(key) != matrices[rhs].get(key):
                failures.add(f"{lhs} vs {rhs}: opencv-matrix '{key}' differs")

    # 5. The headline claim: U and U+W must differ in backend weighting ALONE.
    if {"union_noweight", "union_weight"} <= set(scalars):
        u, uw = scalars["union_noweight"], scalars["union_weight"]
        diffs = sorted(k for k in set(u) | set(uw)
                       if not values_equal(u.get(k, "<absent>"), uw.get(k, "<absent>")))
        if diffs != ["sem_geodf_backend_weight"]:
            failures.add("U vs U+W must differ in sem_geodf_backend_weight and nothing else; "
                         f"actual difference set: {diffs or '(none - weighting ablation is a no-op)'}")

    return failures


def from_overlays(base: Path, allowlist: dict[str, list[str]],
                  methods: list[str]) -> dict[str, str]:
    base_text = base.read_text()
    out: dict[str, str] = {}
    for name in methods:
        path = OVERLAY_DIR / f"{name}.yaml"
        if not path.is_file():
            print(f"[fatal] no overlay {path}", file=sys.stderr)
            sys.exit(2)
        settings, _ = parse_overlay(path)
        out[name] = apply_overlay(base_text, settings, name)
    return out


def from_resolved(root: Path) -> dict[str, str]:
    """Collect resolved_config.yaml files from a run tree, keyed by method."""
    out: dict[str, str] = {}
    found = sorted(root.rglob("resolved_config.yaml"))
    if not found:
        print(f"[fatal] no resolved_config.yaml under {root}. Runs must record the exact "
              f"config they used.", file=sys.stderr)
        sys.exit(2)
    for path in found:
        provenance = path.with_suffix(path.suffix + ".provenance.json")
        method = None
        if provenance.is_file():
            import json
            method = json.loads(provenance.read_text()).get("overlay")
        if method is None:
            # dataset/scene/method/trial/resolved_config.yaml
            method = path.parent.parent.name
        text = path.read_text()
        if method in out and out[method] != text:
            print(f"[fatal] method '{method}' has two different resolved configs; "
                  f"conflicting copy: {path}", file=sys.stderr)
            sys.exit(3)
        out[method] = text
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--base", type=Path,
                    help="Audit the checked-in overlays against this common base")
    src.add_argument("--resolved", type=Path,
                    help="Audit resolved_config.yaml files found under this run tree")
    ap.add_argument("--allowlist", type=Path, default=DEFAULT_ALLOWLIST)
    ap.add_argument("--methods", default=",".join(MAIN_MATRIX),
                    help="Comma-separated overlay names to audit (--base mode)")
    ap.add_argument("--matrix", choices=["main", "full-adaptation"], default="main",
                    help="'full-adaptation' requires the adaptation keys on for ALL methods")
    args = ap.parse_args()

    if not args.allowlist.is_file():
        print(f"[fatal] missing allowlist {args.allowlist}", file=sys.stderr)
        return 2
    allowlist = load_allowlist(args.allowlist)

    if args.base:
        methods = [m.strip() for m in args.methods.split(",") if m.strip()]
        configs = from_overlays(args.base, allowlist, methods)
        scope = f"overlays {', '.join(methods)} on {args.base.name}"
    else:
        configs = from_resolved(args.resolved)
        scope = f"resolved configs under {args.resolved}"

    failures = audit(configs, allowlist, args.matrix)

    print(f"[audit] scope: {scope}")
    print(f"[audit] matrix: {args.matrix}")
    print(f"[audit] methods: {', '.join(sorted(configs))}")
    if failures:
        print(f"\nFAIL: {len(failures)} config-fairness violation(s)\n")
        for message in failures:
            print(f"  - {message}")
        print("\nFix the base/overlays or update src/config/paper/allowed_differences.yaml "
              "with an explicit justification. Do not run the paper matrix until this passes.")
        return 1

    print("\nPASS: methods differ only in the keys they own.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
