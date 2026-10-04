#!/usr/bin/env python3
"""Capture a bounded research child without shell redirection.

This helper is intended to run *inside* ``run_memory_bounded.py`` so the
capture process and its child share the monitored process group.
"""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stdout", type=Path, required=True)
    parser.add_argument("--stderr", type=Path, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a command is required after --")
    args.stdout.parent.mkdir(parents=True, exist_ok=True)
    args.stderr.parent.mkdir(parents=True, exist_ok=True)
    with args.stdout.open("wb") as stdout, args.stderr.open("wb") as stderr:
        return subprocess.run(command, stdout=stdout, stderr=stderr, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
