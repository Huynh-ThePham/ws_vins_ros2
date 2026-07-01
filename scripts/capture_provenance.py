#!/usr/bin/env python3
"""Capture reproducibility provenance for a benchmark study.

Records the exact software/hardware state so results are traceable in a
top-tier submission: git commit + dirty state, ROS distro, tool versions,
host/CPU, and a hash of every config file used. Written to
<study_dir>/provenance.json (append-safe: keeps history of runs).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path

WS = Path(__file__).resolve().parents[1]


def _run(cmd: list[str], cwd: Path | None = None) -> str:
    try:
        return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=15).stdout.strip()
    except Exception:
        return ""


def _git() -> dict:
    return {
        "commit": _run(["git", "rev-parse", "HEAD"], WS),
        "branch": _run(["git", "rev-parse", "--abbrev-ref", "HEAD"], WS),
        "describe": _run(["git", "describe", "--always", "--dirty", "--tags"], WS),
        "dirty": bool(_run(["git", "status", "--porcelain"], WS)),
        "subject": _run(["git", "log", "-1", "--format=%s"], WS),
    }


def _tool_version(binary: str, args: list[str]) -> str:
    if not shutil.which(binary):
        return "not found"
    return _run([binary] + args).splitlines()[0] if _run([binary] + args) else ""


def _cpu_model() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    except Exception:
        pass
    return platform.processor()


def _mem_gb() -> float:
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemTotal"):
                return round(int(line.split()[1]) / 1024 / 1024, 1)
    except Exception:
        pass
    return 0.0


def _pip_version(mod: str) -> str:
    try:
        m = __import__(mod)
        return getattr(m, "__version__", "")
    except Exception:
        return ""


def _hash_configs(config_globs: list[str]) -> dict:
    out = {}
    for pattern in config_globs:
        for p in sorted(WS.glob(pattern)):
            if p.is_file():
                h = hashlib.sha256(p.read_bytes()).hexdigest()[:16]
                out[str(p.relative_to(WS))] = h
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--study-dir", type=Path, required=True, help="e.g. results/viode_repeat")
    ap.add_argument("--study", default="", help="study label, e.g. viode_n5")
    ap.add_argument(
        "--configs",
        nargs="*",
        default=["src/config/viode/*.yaml", "src/config/euroc/*.yaml"],
        help="glob(s) of config files to hash",
    )
    args = ap.parse_args()

    record = {
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "study": args.study,
        "git": _git(),
        "host": {
            "hostname": platform.node(),
            "os": platform.platform(),
            "kernel": platform.release(),
            "cpu": _cpu_model(),
            "cpu_count": __import__("os").cpu_count(),
            "mem_gb": _mem_gb(),
        },
        "software": {
            "python": platform.python_version(),
            "ros_distro": __import__("os").environ.get("ROS_DISTRO", ""),
            "numpy": _pip_version("numpy"),
            "scipy": _pip_version("scipy"),
            "pandas": _pip_version("pandas"),
            "matplotlib": _pip_version("matplotlib"),
            "opencv": _pip_version("cv2"),
            "evo": _pip_version("evo") or _run(["evo", "pkg", "--pyversion"]),
        },
        "config_sha256_16": _hash_configs(args.configs),
    }

    args.study_dir.mkdir(parents=True, exist_ok=True)
    out = args.study_dir / "provenance.json"
    history = []
    if out.is_file():
        try:
            prev = json.loads(out.read_text())
            history = prev if isinstance(prev, list) else [prev]
        except Exception:
            history = []
    history.append(record)
    out.write_text(json.dumps(history, indent=2) + "\n")
    print(f"[provenance] {record['git']['describe']} "
          f"({'dirty' if record['git']['dirty'] else 'clean'}) -> {out}")


if __name__ == "__main__":
    main()
