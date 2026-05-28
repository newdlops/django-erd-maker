#!/usr/bin/env python3
"""log.txt + captain baseline 비교 — multistart 결과 평가용.

VS Code reload 후 log.txt 를 읽어:
  1. [multistart] 라인에서 run별 crossings + best seed 추출
  2. "OGDF layout completed" 최종 metrics 라인 파싱 (strategy=cluster_graph)
  3. baseline JSON 과 비교 — visualCross / edgeCross / quality / bbox / bundleEdgeIntersections
  4. PASS / WARN / FAIL 판정
     - FAIL: bundleEdgeIntersections 가 baseline +20 이상 (stress-style bundle 파괴)
     - WARN: visualCross 가 baseline 보다 악화
     - PASS: visualCross 가 baseline 보다 개선되면서 bundle 구조 보존

Usage:
  python scripts/erd-poc/compare_multistart_baseline.py \\
      --log log.txt \\
      --baseline data/erd-poc/baselines/captain-2026-05-24.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


MULTISTART_RUN = re.compile(
    r"\[multistart\] run (\d+)(?: \(initial\))?(?: seed=(-?\d+))? crossings=(\d+)"
)
MULTISTART_SELECTED = re.compile(
    r"\[multistart\] selected run with crossings=(\d+) \(seed=(-?\d+), runs=(\d+)\)"
)
OGDF_COMPLETED = re.compile(r"OGDF layout completed in \d+ms · (.+)$")


def parse_metrics_line(payload: str) -> dict:
    out: dict[str, str] = {}
    for part in payload.split(" · "):
        if "=" not in part:
            continue
        key, _, value = part.partition("=")
        out[key.strip()] = value.strip()
    return out


def parse_qsub(qsub: str) -> dict[str, float]:
    out: dict[str, float] = {}
    for part in qsub.split("/"):
        if ":" not in part:
            continue
        k, _, v = part.partition(":")
        try:
            out[k] = float(v)
        except ValueError:
            pass
    return out


def parse_log(log_path: Path):
    multistart_runs: list[tuple[int, int | None, int]] = []
    multistart_selected: tuple[int, int, int] | None = None
    final_metrics: dict[str, str] | None = None
    final_line_no = -1

    with log_path.open("r", encoding="utf-8") as f:
        for lineno, raw in enumerate(f, start=1):
            line = raw.rstrip("\n")

            m = MULTISTART_RUN.search(line)
            if m:
                run = int(m.group(1))
                seed = int(m.group(2)) if m.group(2) is not None else None
                crossings = int(m.group(3))
                multistart_runs.append((run, seed, crossings))
                continue

            m = MULTISTART_SELECTED.search(line)
            if m:
                multistart_selected = (
                    int(m.group(1)), int(m.group(2)), int(m.group(3))
                )
                continue

            m = OGDF_COMPLETED.search(line)
            if m:
                parsed = parse_metrics_line(m.group(1))
                # Prefer cluster_graph strategy; otherwise keep the most
                # recent line so non-bundle paths still report something.
                if parsed.get("strategy") == "cluster_graph":
                    final_metrics = parsed
                    final_line_no = lineno
                elif final_metrics is None:
                    final_metrics = parsed
                    final_line_no = lineno

    return multistart_runs, multistart_selected, final_metrics, final_line_no


def fmt_delta(current: float, baseline: float, lower_is_better: bool = True) -> str:
    if baseline == 0:
        return f"{current:.3f} (baseline 0)"
    pct = (current - baseline) / abs(baseline) * 100.0
    sign = "+" if pct >= 0 else ""
    arrow = ""
    if lower_is_better:
        arrow = " ✓" if current < baseline else (" ✗" if current > baseline else " =")
    else:
        arrow = " ✓" if current > baseline else (" ✗" if current < baseline else " =")
    return f"{current:.3f} (baseline {baseline:.3f}, {sign}{pct:.1f}%{arrow})"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--log", type=Path, default=Path("log.txt"))
    p.add_argument(
        "--baseline",
        type=Path,
        default=Path("data/erd-poc/baselines/captain-2026-05-24.json"),
    )
    args = p.parse_args()

    if not args.log.exists():
        sys.exit(f"log not found: {args.log}")
    if not args.baseline.exists():
        sys.exit(f"baseline not found: {args.baseline}")

    baseline = json.loads(args.baseline.read_text())
    bm = baseline["metrics"]

    runs, selected, metrics, line_no = parse_log(args.log)

    print(f"[log] {args.log} → {sum(1 for _ in args.log.open())} lines parsed")
    print(f"[baseline] {args.baseline.name} (captured {baseline.get('captured_at')})")
    print()

    if runs:
        print("=== Multistart per-run crossings ===")
        best_cross = min(c for _, _, c in runs)
        for run, seed, cross in runs:
            marker = " ← best" if cross == best_cross else ""
            seed_str = "initial" if seed is None else f"seed={seed}"
            print(f"  run {run:>2} {seed_str:>13}  crossings={cross:>6}{marker}")
        if selected:
            sel_cross, sel_seed, sel_runs = selected
            sel_seed_str = "initial" if sel_seed == -1 else f"seed={sel_seed}"
            print(
                f"  selected: {sel_seed_str} crossings={sel_cross} "
                f"({sel_runs} runs total)"
            )
        # diversity check: how different are the runs?
        if len(runs) >= 2:
            crosses = [c for _, _, c in runs]
            spread = max(crosses) - min(crosses)
            mean = sum(crosses) / len(crosses)
            print(
                f"  spread: {spread} crossings ({spread / mean * 100:.1f}% of mean) — "
                + ("high diversity (good)" if spread / mean > 0.1
                   else "low diversity (consider new seed_base or more runs)")
            )
        print()
    else:
        print("[multistart] NO multistart logs found — DJERD_MULTISTART_RUNS=1?")
        print()

    if metrics is None:
        sys.exit("no OGDF layout completed line found in log")

    print(f"=== Final metrics (log.txt:{line_no}, strategy={metrics.get('strategy')}) ===")

    # Headline comparisons.
    def get_float(d, key, default=0.0):
        try:
            return float(d.get(key, default))
        except (ValueError, TypeError):
            return default

    def get_int(d, key, default=0):
        try:
            return int(d.get(key, default))
        except (ValueError, TypeError):
            return default

    visual = get_int(metrics, "visualCrossings")
    edge_cross = get_int(metrics, "edgeCrossings")
    edge_node = get_int(metrics, "edgeNodeIntersections")
    bundle_xs = get_int(metrics, "bundleEdgeIntersections")
    quality = get_float(metrics, "quality")
    bbox_w = get_float(metrics, "bboxWidth")
    bbox_h = get_float(metrics, "bboxHeight")
    bbox_b = bbox_w * bbox_h / 1e9
    baseline_bbox_b = bm["nodeBBoxWidth"] * bm["nodeBBoxHeight"] / 1e9

    print(f"  nodes:                  {metrics.get('nodes')} (baseline {bm['nodes']})")
    print(f"  leafBundles:            {metrics.get('leafBundles')} (baseline {bm['leafBundles']})")
    print(f"  visualCrossings:        {fmt_delta(visual, bm['visualCrossings'])}")
    print(f"  edgeCrossings:          {fmt_delta(edge_cross, bm['edgeCrossings'])}")
    print(f"  edgeNodeIntersections:  {fmt_delta(edge_node, bm['edgeNodeIntersections'])}")
    print(f"  bundleEdgeIntersections:{fmt_delta(bundle_xs, bm['bundleEdgeIntersections'])}")
    print(f"  quality (composite):    {fmt_delta(quality, bm['quality'], lower_is_better=False)}")
    print(f"  bbox (B):               {fmt_delta(bbox_b, baseline_bbox_b)}")
    print(f"  worstEdge:              {metrics.get('worstEdge')}")
    print(f"                          (baseline: {bm['worstEdge']})")

    # qSub breakdown.
    qsub_curr = parse_qsub(metrics.get("qSub", ""))
    qsub_base = parse_qsub(bm.get("qSub", ""))
    if qsub_curr and qsub_base:
        print("  qSub breakdown:")
        for k in ("clean", "sev", "ang", "str", "cmp", "uni", "spr"):
            if k in qsub_curr and k in qsub_base:
                delta = qsub_curr[k] - qsub_base[k]
                arrow = "↑" if delta > 0.005 else ("↓" if delta < -0.005 else " ")
                print(
                    f"    {k:>5}: {qsub_curr[k]:.3f} (baseline {qsub_base[k]:.3f}, "
                    f"{delta:+.3f} {arrow})"
                )
    print()

    # Verdict.
    print("=== Verdict ===")
    bundle_breakage = bundle_xs - bm["bundleEdgeIntersections"]
    visual_delta = visual - bm["visualCrossings"]
    quality_delta = quality - bm["quality"]

    if bundle_breakage > 20:
        print(f"  ✗ FAIL — bundle structure destroyed "
              f"(bundleEdgeIntersections +{bundle_breakage}). "
              "Same pattern as stress post-pass failure. "
              "Recommend DJERD_MULTISTART_RUNS=1.")
        sys.exit(2)
    elif visual_delta < -20 and quality_delta > 0.005:
        print(f"  ✓ PASS — visualCross {visual_delta:+d}, quality {quality_delta:+.3f}, "
              f"bundle structure preserved (Δ={bundle_breakage:+d}).")
        sys.exit(0)
    elif visual_delta >= 0:
        print(f"  ⚠ WARN — no improvement (visualCross {visual_delta:+d}). "
              "Try different seed_base or more runs.")
        sys.exit(1)
    else:
        print(f"  ⚠ MARGINAL — visualCross {visual_delta:+d}, quality {quality_delta:+.3f}. "
              "Improvement is small; consider more runs.")
        sys.exit(1)


if __name__ == "__main__":
    main()
