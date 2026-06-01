#!/usr/bin/env python3
"""Generalization probe for the v37 family-prior.

Runs the production ML scorer stage on synthetic graphs the prior never saw
during training (trained only on the Captain 1301 multistart corpus). Compares
two conditions per graph:

  ON  : family-prior-ckpt = v37-multistart-1301-light.pt, top-actions = 8
        (exactly production: search focuses on the 8 prior-recommended families)
  OFF : top-actions = 0 (prior disabled; search checks all 12 families)

If gain(ON) ~= gain(OFF) across diverse graphs, the Captain-trained prior
generalizes (it focuses the search without sacrificing quality on unseen
structure). If gain(ON) << gain(OFF), the prior is overfit to Captain's family
distribution and misdirects the search on other graphs.

Usage:
  .venv-ml/bin/python scripts/erd-poc/eval_generalization.py \
     --graphs synth-2152-n358-e421 synth-2039-n474-e535 ...
"""
from __future__ import annotations
import argparse, json, re, subprocess, tempfile, os, statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCORER = ROOT / "scripts/erd-poc/eval_v35_scorer_filter.py"
PY = ROOT / ".venv-ml/bin/python"
V36 = ROOT / "data/erd-poc/checkpoints/v36-pure-action-scorer.pt"
V37 = ROOT / "data/erd-poc/checkpoints/v37-multistart-1301-light.pt"

# Production-default scorer args (the readStringEnv fallbacks in
# runOgdfLayout.ts:buildV36ScorerArgs). Family-prior args are appended per run.
PROD_ARGS = [
    "--rounds", "1", "--ml-top-k", "60", "--per-action-k", "12",
    "--score-batch-size", "4096", "--min-gain", "1",
    "--max-cross-regression", "0", "--max-overlap-regression", "0",
    "--max-edge-node-regression", "0", "--max-bbox-growth", "1.05",
    "--overlap-margin", "16", "--overlap-weight", "5000",
    "--bbox-weight", "800", "--bbox-target-b", "4.0",
    "--edge-node-margin", "0", "--edge-node-weight", "0.5", "--advance", "ml",
    "--top-nodes", "40", "--top-edges", "40", "--top-clusters", "50",
    "--steps", "75,150,300,600,1000,1800",
    "--anchor-radii", "75,150,300,600,1000,1800,3000",
    "--cross-pair-candidates", "80", "--cross-pair-steps", "75,150,300,600,1000,1800",
    "--cross-fan-edges", "40", "--cross-fan-steps", "75,150,300,600,1000,1800",
    "--cross-group-fan-groups", "50", "--cross-group-fan-max-size", "120",
    "--cross-group-fan-steps", "75,150,300,600,1000,1800",
    "--swap-pairs", "40", "--cluster-swap-pairs", "100",
    "--edge-node-relief-hits", "40", "--edge-node-relief-max-group-size", "120",
    "--edge-node-relief-steps", "75,150,300,600,1000,1800",
    "--edge-node-precise-hits", "50",
    "--edge-node-precise-neighborhood-max-nodes", "32",
    "--edge-node-precise-pads", "0,60,140", "--edge-node-clear-padding", "32",
    "--edge-node-corridor-edges", "20", "--edge-node-corridor-blockers", "8",
    "--edge-node-corridor-pads", "0,60,140",
    "--edge-node-force-hits", "50", "--edge-node-force-nodes", "64",
    "--edge-node-force-scales", "0.25,0.5,0.85", "--edge-node-force-padding", "55",
    "--overlap-candidates", "120", "--overlap-spread-components", "60",
    "--loose-pack-groups", "12", "--loose-anchor-nodes", "40",
    "--loose-anchor-radii", "150,300,600,1000,1800,3000,5000,8000",
    "--semantic-anchor-nodes", "12", "--group-anchor-groups", "70",
    "--group-anchor-max-size", "120", "--component-anchor-components", "30",
    "--component-anchor-max-group-size", "10", "--component-anchor-max-total-nodes", "100",
    "--bundle-orbit-groups", "12",
    "--final-max-candidates", "400", "--final-per-action-candidates", "80",
    "--no-compare-full",
]

INIT_RE = re.compile(r"initial cross=(\d+) overlaps=(\d+) edgeNode=(\d+) visualCross=(\d+) bbox=([\d.]+)B")
FINAL_RE = re.compile(r"done accepted=(\d+).*final cross=(\d+) overlaps=(\d+) edgeNode=(\d+) visualCross=(\d+) bbox=([\d.]+)B")
FAM_RE = re.compile(r"familyPrior=([^\s]+)")


def layout_path(name: str, layout_dir: str | None) -> str:
    if layout_dir:
        return str(Path(layout_dir) / f"{name}.json")
    return str(ROOT / f"data/erd-poc/layouts/{name}.json")


def write_pos_tsv(name: str, layout_dir: str | None) -> str:
    L = json.load(open(layout_path(name, layout_dir)))
    lines = []
    for nd in L["nodes"]:
        mid = nd.get("modelId")
        if not isinstance(mid, str):
            continue
        p = nd.get("position") or {}
        s = nd.get("size") or {}
        x = float(p.get("x", 0)) + float(s.get("width", 0)) / 2
        y = float(p.get("y", 0)) + float(s.get("height", 0)) / 2
        lines.append(f"{mid}\t{x:.3f}\t{y:.3f}")
    fp = tempfile.NamedTemporaryFile("w", suffix=".tsv", delete=False)
    fp.write("\n".join(lines))
    fp.close()
    return fp.name


def run(name: str, pos: str, prior_on: bool, layout_dir: str | None,
        prior_ckpt: str = str(V37)) -> dict:
    out = tempfile.NamedTemporaryFile(suffix=".tsv", delete=False).name
    args = [str(PY), str(SCORER),
            "--layout", layout_path(name, layout_dir),
            "--positions", pos, "--out-tsv", out,
            "--ckpt", str(V36)]
    if prior_on:
        args += ["--family-prior-ckpt", prior_ckpt,
                 "--family-prior-top-actions", "8",
                 "--family-prior-min-candidates", "120"]
    else:
        args += ["--family-prior-top-actions", "0"]
    args += PROD_ARGS
    r = subprocess.run(args, capture_output=True, text=True)
    init = INIT_RE.search(r.stdout)
    fin = FINAL_RE.search(r.stdout)
    fam = FAM_RE.findall(r.stdout)
    if not init or not fin:
        return {"err": r.stdout[-300:] + " || " + r.stderr[-300:]}
    return {
        "init_vcross": int(init.group(4)), "init_en": int(init.group(3)),
        "init_cross": int(init.group(1)),
        "accepted": int(fin.group(1)),
        "fin_vcross": int(fin.group(5)), "fin_en": int(fin.group(4)),
        "fin_cross": int(fin.group(2)),
        "fams": fam[-1] if fam else "",
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--graphs", nargs="+", required=True)
    ap.add_argument("--layout-dir", default=None,
                    help="dir of <name>.json layouts (default: data/erd-poc/layouts)")
    ap.add_argument("--prior-ckpt", default=str(V37),
                    help="family-prior ckpt for the ON condition (default: production v37 light)")
    a = ap.parse_args()
    print(f"{'graph':<24} {'init_vC':>7} | {'OFF_vC':>6} {'OFFacc':>6} | {'ON_vC':>6} {'ONacc':>6} | {'gOFF':>5} {'gON':>5} {'verdict':>8}")
    g_on, g_off = [], []
    for g in a.graphs:
        pos = write_pos_tsv(g, a.layout_dir)
        off = run(g, pos, False, a.layout_dir)
        on = run(g, pos, True, a.layout_dir, a.prior_ckpt)
        if "err" in off or "err" in on:
            print(f"{g:<24} ERR off={off.get('err','')[:80]} on={on.get('err','')[:80]}")
            continue
        goff = off["init_vcross"] - off["fin_vcross"]
        gon = on["init_vcross"] - on["fin_vcross"]
        g_off.append(goff)
        g_on.append(gon)
        verdict = "ON==OFF" if gon == goff else ("ON<OFF" if gon < goff else "ON>OFF")
        print(f"{g:<24} {off['init_vcross']:>7} | {off['fin_vcross']:>6} {off['accepted']:>6} | "
              f"{on['fin_vcross']:>6} {on['accepted']:>6} | {goff:>5} {gon:>5} {verdict:>8}")
        if gon < goff:
            allfams = {"semantic_orphan_to_louvain_cluster", "node_position_swap",
                       "node_translate", "node_anchor_to_neighbors", "edge_endpoint_translate",
                       "edge_translate", "overlap_component_line", "louvain_cluster_centroid_swap",
                       "louvain_cluster_translate", "louvain_cluster_anchor_to_neighbors",
                       "overlap_component_ring", "overlap_components_batch_line"}
            picked = set(on["fams"].replace(":fallback", "").split(","))
            print(f"    prior picked: {sorted(picked & allfams)}")
            print(f"    prior DROPPED: {sorted(allfams - picked)}")
    if g_off:
        print(f"\nsummary over {len(g_off)} graphs:")
        print(f"  total gain OFF={sum(g_off)}  ON={sum(g_on)}  ratio ON/OFF={sum(g_on)/max(1,sum(g_off)):.3f}")
        print(f"  mean  gain OFF={statistics.mean(g_off):.1f}  ON={statistics.mean(g_on):.1f}")
        worse = sum(1 for o, n in zip(g_off, g_on) if n < o)
        same = sum(1 for o, n in zip(g_off, g_on) if n == o)
        better = sum(1 for o, n in zip(g_off, g_on) if n > o)
        print(f"  graphs where ON worse than OFF: {worse}, equal: {same}, better: {better}")


if __name__ == "__main__":
    main()
