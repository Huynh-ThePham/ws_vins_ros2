#!/usr/bin/env python3
"""Shared helpers for the paper's fair-configuration framework (plan P0.1).

The estimator reads OpenCV-flavoured YAML (`%YAML:1.0`, `!!opencv-matrix`), which
PyYAML cannot parse. Every helper here is therefore line-oriented: scalar
`key: value` lines are understood, and `!!opencv-matrix` blocks are carried
through byte-identically so calibration is never rewritten by accident.

The framework exists so that two methods in the paper differ *only* in the keys
that method owns. Any other difference is a confound, and
scripts/audit_method_config_diff.py is expected to fail on it.
"""
from __future__ import annotations

import hashlib
import re
from pathlib import Path

# `key: value` at column zero. Indented lines belong to an opencv-matrix block.
_SCALAR_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.*?)\s*$")


def strip_comment(value: str) -> str:
    """Drop a trailing ` # ...` comment. Not applied inside quoted strings."""
    if value.startswith('"') or value.startswith("'"):
        return value
    head = value.split("#", 1)[0]
    return head.strip()


def parse_scalars(text: str) -> dict[str, str]:
    """Top-level scalar keys of an OpenCV YAML config, in file order.

    Keys whose value is an `!!opencv-matrix` (or any other tag) are skipped: they
    are structural calibration, not tunable knobs, and the audit compares them by
    raw text instead.
    """
    out: dict[str, str] = {}
    for raw in text.splitlines():
        if not raw or raw[0].isspace() or raw.lstrip().startswith("#"):
            continue
        match = _SCALAR_RE.match(raw)
        if not match:
            continue
        key, value = match.group(1), strip_comment(match.group(2))
        if value.startswith("!!") or value == "":
            continue
        out[key] = value
    return out


def parse_matrix_blocks(text: str) -> dict[str, str]:
    """`key: !!opencv-matrix` blocks, keyed by name, value = raw block text."""
    out: dict[str, str] = {}
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        match = _SCALAR_RE.match(lines[i]) if lines[i] and not lines[i][0].isspace() else None
        if match and match.group(2).startswith("!!"):
            key = match.group(1)
            block = [lines[i]]
            i += 1
            while i < len(lines) and (not lines[i].strip() or lines[i][0].isspace()):
                if lines[i].strip():
                    block.append(lines[i])
                i += 1
            out[key] = "\n".join(block)
            continue
        i += 1
    return out


def parse_overlay(path: Path) -> tuple[dict[str, str], dict[str, str]]:
    """Read an overlay file.

    Returns (settings, meta). `meta` holds the `# @key: value` annotation lines
    (method id, description) so the generator can record provenance without those
    keys leaking into the estimator config.
    """
    settings: dict[str, str] = {}
    meta: dict[str, str] = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if line.startswith("# @"):
            body = line[3:]
            if ":" in body:
                key, value = body.split(":", 1)
                meta[key.strip()] = value.strip()
            continue
        if not line or line.startswith("#") or ":" not in line:
            continue
        key, value = line.split(":", 1)
        settings[key.strip()] = strip_comment(value.strip())
    return settings, meta


def apply_overlay(base_text: str, overlay: dict[str, str], overlay_name: str) -> str:
    """Return base_text with every overlay key set to its overlay value.

    A key already present is replaced in place, preserving surrounding comments and
    ordering. A key absent from the base is appended in a clearly marked block --
    but the audit treats that as suspicious, because a fair base should declare
    every knob explicitly (including the off values).
    """
    remaining = dict(overlay)
    out: list[str] = []
    for raw in base_text.splitlines():
        match = _SCALAR_RE.match(raw) if raw and not raw[0].isspace() else None
        if match and match.group(1) in remaining:
            key = match.group(1)
            out.append(f"{key}: {remaining.pop(key)}")
            continue
        out.append(raw)

    if remaining:
        out.append("")
        out.append(f"# Keys introduced by overlay '{overlay_name}' (absent from base)")
        for key in sorted(remaining):
            out.append(f"{key}: {remaining[key]}")
    return "\n".join(out) + "\n"


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def values_equal(lhs: str, rhs: str) -> bool:
    """Compare config values numerically when both sides parse as numbers.

    `0.20` and `0.2` are the same setting; a textual diff would call them
    different and make the audit useless.
    """
    if lhs == rhs:
        return True
    try:
        return float(lhs) == float(rhs)
    except ValueError:
        return False
