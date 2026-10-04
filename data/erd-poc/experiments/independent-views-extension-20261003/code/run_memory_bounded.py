#!/usr/bin/env python3
"""Run one ERD research command under a fail-closed RSS budget.

This wrapper is intentionally conservative. It serializes all research runs,
limits math-library thread pools, monitors the complete child process group,
and kills that group before a single optimizer can pressure the workstation
into OOM. If RSS cannot be measured, the command is stopped instead of being
allowed to continue without a guard.
"""

from __future__ import annotations

import argparse
import fcntl
import os
import resource
import signal
import subprocess
import sys
import time
from pathlib import Path


LOCK_PATH = Path("/private/tmp/django-erd-research-optimizer.lock")
DEFAULT_LIMIT_MIB = 256
MAX_LIMIT_MIB = 512


def process_group_rss_mib(process_group_id: int) -> float:
    # macOS can select a process group directly. This avoids repeatedly
    # enumerating every process on a memory-pressured workstation, which can
    # itself stall long enough to fail the guard. Keep the full scan only as a
    # portable fallback and still fail closed if neither measurement works.
    result = subprocess.run(
        ["/bin/ps", "-g", str(process_group_id), "-o", "rss="],
        check=False,
        capture_output=True,
        text=True,
        timeout=5.0,
    )
    direct_group_query = result.returncode == 0
    if not direct_group_query:
        result = subprocess.run(
            ["/bin/ps", "-ax", "-o", "pgid=,rss="],
            check=False,
            capture_output=True,
            text=True,
            timeout=10.0,
        )
        if result.returncode != 0:
            raise RuntimeError(f"cannot measure process RSS: {result.stderr.strip()}")
    resident_kib = 0
    for line in result.stdout.splitlines():
        parts = line.split()
        try:
            if direct_group_query:
                if len(parts) < 1:
                    continue
                rss_kib = int(parts[0])
                pgid = process_group_id
            else:
                if len(parts) < 2:
                    continue
                pgid = int(parts[0])
                rss_kib = int(parts[1])
        except ValueError:
            continue
        if pgid == process_group_id:
            resident_kib += max(0, rss_kib)
    return resident_kib / 1024.0


def child_resource_limits(limit_bytes: int) -> None:
    for name in ("RLIMIT_DATA", "RLIMIT_RSS"):
        resource_id = getattr(resource, name, None)
        if resource_id is None:
            continue
        try:
            _soft, hard = resource.getrlimit(resource_id)
            bounded = limit_bytes if hard == resource.RLIM_INFINITY else min(limit_bytes, hard)
            resource.setrlimit(resource_id, (bounded, bounded))
        except (OSError, ValueError):
            # The external RSS monitor remains mandatory and fail-closed.
            pass


def kill_process_group(child: subprocess.Popen[bytes]) -> None:
    if child.poll() is not None:
        return
    try:
        os.killpg(child.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--limit-mib", type=int, default=DEFAULT_LIMIT_MIB)
    parser.add_argument("--poll-ms", type=int, default=100)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a command is required after --")

    limit_mib = min(MAX_LIMIT_MIB, max(64, args.limit_mib))
    poll_seconds = min(1.0, max(0.05, args.poll_ms / 1000.0))
    LOCK_PATH.parent.mkdir(parents=True, exist_ok=True)
    with LOCK_PATH.open("a+") as lock_file:
        try:
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("refusing to start: another ERD research optimizer is running", file=sys.stderr)
            return 75

        environment = os.environ.copy()
        for name in (
            "OMP_NUM_THREADS",
            "OPENBLAS_NUM_THREADS",
            "MKL_NUM_THREADS",
            "VECLIB_MAXIMUM_THREADS",
            "NUMEXPR_NUM_THREADS",
        ):
            environment[name] = "1"
        environment["MALLOC_ARENA_MAX"] = "2"
        limit_bytes = limit_mib * 1024 * 1024
        child = subprocess.Popen(
            command,
            env=environment,
            start_new_session=True,
            preexec_fn=lambda: child_resource_limits(limit_bytes),
        )
        peak_mib = 0.0
        try:
            while child.poll() is None:
                try:
                    resident_mib = process_group_rss_mib(child.pid)
                except Exception as error:
                    kill_process_group(child)
                    print(f"memory guard failed closed: {error}", file=sys.stderr)
                    return 70
                peak_mib = max(peak_mib, resident_mib)
                if resident_mib > limit_mib:
                    kill_process_group(child)
                    print(
                        f"memory limit exceeded: rss={resident_mib:.1f} MiB "
                        f"limit={limit_mib} MiB; process group killed",
                        file=sys.stderr,
                    )
                    return 137
                time.sleep(poll_seconds)
            return_code = child.wait()
            print(
                f"memory guard: peak={peak_mib:.1f} MiB limit={limit_mib} MiB",
                file=sys.stderr,
            )
            return return_code
        except BaseException:
            kill_process_group(child)
            raise


if __name__ == "__main__":
    raise SystemExit(main())
