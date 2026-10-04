#!/usr/bin/env python3
"""Build only the release C++ target with a bounded, single-worker compiler.

The general research guard keeps its own lower default and maximum. This entry
point cannot run arbitrary commands and is reserved for release compilation.
"""
import argparse
import fcntl
import importlib.util
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "research_guard", ROOT / "scripts/erd-poc/run_memory_bounded.py"
)
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)

parser = argparse.ArgumentParser()
parser.add_argument("--limit-mib", type=int, choices=(256, 512, 1024), default=512)
args = parser.parse_args()

with guard.LOCK_PATH.open("a+") as lock:
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise SystemExit("Another guarded ERD task is running")
    command = ["cmake", "--build", str(ROOT / ".tmp/ogdf-build-lowmem/build"),
               "--config", "Release", "--target", "django-erd-ogdf-layout", "--parallel", "1"]
    environment = {**os.environ, "OMP_NUM_THREADS": "1", "CMAKE_BUILD_PARALLEL_LEVEL": "1"}
    child = subprocess.Popen(command, cwd=ROOT, env=environment, start_new_session=True,
                             preexec_fn=lambda: guard.child_resource_limits(args.limit_mib * 1024 * 1024))
    peak = 0.0
    try:
        while child.poll() is None:
            try:
                memory = guard.process_group_rss_mib(child.pid)
            except Exception as error:
                guard.kill_process_group(child)
                raise SystemExit(f"Native build guard failed closed: {error}")
            peak = max(peak, memory)
            if memory > args.limit_mib:
                guard.kill_process_group(child)
                raise SystemExit(f"Native build stopped: {memory:.1f} MiB exceeds {args.limit_mib} MiB")
            time.sleep(0.1)
        print(f"Native build guard: peak={peak:.1f} MiB limit={args.limit_mib} MiB", file=sys.stderr)
        raise SystemExit(child.wait())
    finally:
        guard.kill_process_group(child)
