#!/usr/bin/env python3
"""Compare composite vs legacy v37 ckpts on the same Captain state.

For each registered start file (under1000, groupanchor2, hot700,
random700) we read the positions, compute v34 metrics + composite
quality, build feature vectors aligned to each ckpt's expected
feature_names (zero-fill any missing features), then ask each model
for its top-K action ranking. Output: per-state side-by-side table
showing rank divergence + score deltas so we can see whether the
composite-aware model is actually using the new signals.

Usage:
  python scripts/erd-poc/compare_v37_ckpts.py
  python scripts/erd-poc/compare_v37_ckpts.py --topk 5
"""

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


v34 = load_module("v34_move_search", ROOT / "scripts/erd-poc/v34_move_search.py")
metrics_extended = load_module(
    "metrics_extended", ROOT / "scripts/erd-poc/metrics_extended.py"
)
v37 = load_module(
    "train_v37_action_family_prior",
    ROOT / "scripts/erd-poc/train_v37_action_family_prior.py",
)


def load_ckpt(path: Path) -> dict:
    try:
        ck = torch.load(path, map_location="cpu", weights_only=False)
    except TypeError:
        ck = torch.load(path, map_location="cpu")
    return ck


def build_model(ckpt: dict) -> torch.nn.Module:
    model = v37.FamilyPrior(
        len(ckpt["feature_names"]),
        len(ckpt["action_names"]),
        int(ckpt["hidden"]),
        int(ckpt["layers"]),
        float(ckpt["dropout"]),
    )
    model.load_state_dict(ckpt["model"])
    model.eval()
    return model


def features_for_ckpt(
    ckpt: dict,
    node_static: np.ndarray,
    node_static_names: list[str],
    edges: np.ndarray,
    positions: np.ndarray,
    base_metric5: list[float],
    ext_metric_extra: list[float],
) -> np.ndarray:
    """Build the row of features matching the order the ckpt expects.

    Always computes a value for every feature the ckpt was trained on;
    features the ckpt does NOT use are skipped via row_values lookup.
    """
    has_extended = any(
        n in {
            "metric.logStress", "metric.edgeLengthCv",
            "metric.crossingAngleMean", "metric.crossingAngleCv",
            "metric.logHubClearP10", "metric.logClusterCompact",
        }
        for n in ckpt["feature_names"]
    )
    has_composite = any(
        n.startswith("metric.composite") or n.startswith("metric.sub")
        for n in ckpt["feature_names"]
    )
    # Compose the metric vector to feed state_feature_values.
    metric_row = list(base_metric5)
    if has_extended:
        metric_row.extend(ext_metric_extra[:6])
    if has_composite:
        metric_row.extend(ext_metric_extra[6:14])
    metric_arr = np.array(metric_row, dtype=np.float32)
    values = v37.state_feature_values(
        node_static.astype(np.float32),
        node_static_names,
        edges.astype(np.int32),
        positions.astype(np.float32),
        metric_arr,
        has_extended,
        has_composite,
    )
    name_to_val = {
        name: float(val)
        for name, val in zip(
            v37.state_feature_names(node_static_names, has_extended, has_composite),
            values,
        )
    }
    row = np.array(
        [name_to_val.get(name, 0.0) for name in ckpt["feature_names"]],
        dtype=np.float32,
    )
    return row


def predict_top_actions(
    ckpt: dict, model: torch.nn.Module, row: np.ndarray, topk: int
) -> list[tuple[str, float]]:
    mean = np.asarray(ckpt["mean"], dtype=np.float32)
    std = np.asarray(ckpt["std"], dtype=np.float32)
    std[std < 1e-6] = 1.0
    gain_scale = float(ckpt.get("gain_scale", 50.0))
    x = ((row - mean) / std).astype(np.float32)
    with torch.no_grad():
        pred_gain, pred_accept = model(torch.from_numpy(x.reshape(1, -1)))
    gain = torch.sinh(pred_gain).cpu().numpy().reshape(-1) * gain_scale
    prob = torch.sigmoid(pred_accept).cpu().numpy().reshape(-1)
    score = gain + prob * gain_scale
    order = np.argsort(-score)[:topk]
    return [(ckpt["action_names"][int(i)], float(score[int(i)])) for i in order]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--composite", type=Path,
                   default=ROOT / "data/erd-poc/checkpoints/v37-big-composite.pt")
    p.add_argument("--legacy", type=Path,
                   default=ROOT / "data/erd-poc/checkpoints/v37-big-legacy.pt")
    p.add_argument("--layout", type=Path,
                   default=ROOT / "data/erd-poc/layouts/real-main.json")
    p.add_argument("--topk", type=int, default=5)
    p.add_argument("--starts", nargs="*", default=None,
                   help='Format: name=path/to/positions.tsv (default uses 4 standard starts)')
    args = p.parse_args()

    if args.starts is None:
        args.starts = [
            "under1000=data/erd-poc/v34-under1000/under1000.tsv",
            "groupanchor2=data/erd-poc/v34-under1000/groupanchor2.tsv",
            "hot700=data/erd-poc/v35-move-scorer/subsets/captain-hot700.tsv",
            "random700=data/erd-poc/v35-move-scorer/subsets/captain-random700-s0.tsv",
        ]

    composite_ckpt = load_ckpt(args.composite)
    legacy_ckpt = load_ckpt(args.legacy)
    composite_model = build_model(composite_ckpt)
    legacy_model = build_model(legacy_ckpt)

    print(f"composite ckpt: {len(composite_ckpt['feature_names'])} features, {len(composite_ckpt['action_names'])} actions")
    print(f"legacy ckpt:    {len(legacy_ckpt['feature_names'])} features, {len(legacy_ckpt['action_names'])} actions")
    print()

    layout = v34.load_layout(args.layout)
    nodes = layout["nodes"]
    edges = v34.graph_edges(layout)
    widths, heights = v34.render_node_sizes(layout, edges)
    cluster_ids = [str(nd.get("clusterId") or "") for nd in nodes]
    model_ids = [str(nd["modelId"]) for nd in nodes]
    node_static, node_static_names = build_v35_action_dataset_helpers(layout, edges)

    # Set up v34.measure parameters (mirrors build_v35_action_dataset).
    evaluator = v34.fce.FastCrossEval(edges, len(nodes))
    active_mask = v34.render_active_overlap_mask(layout, False)
    collision_geometry = v34.build_render_collision_geometry(layout, edges, False)
    overlap_pairs = collision_geometry.overlap_pairs
    edge_node_pairs = collision_geometry.edge_node_pairs

    for start_text in args.starts:
        name, path = start_text.split("=", 1)
        path_obj = Path(path)
        if not path_obj.is_absolute():
            path_obj = ROOT / path
        if not path_obj.exists():
            print(f"skip {name}: {path_obj} not found")
            continue
        positions = v34.read_positions_tsv(path_obj, layout)
        m = v34.measure(
            positions, evaluator, widths, heights, active_mask,
            overlap_pairs, 16.0, 5000.0, 800.0, 4.0,
            edge_node_pairs=edge_node_pairs,
            edge_node_margin=0.0,
            edge_node_weight=0.5,
            collision_geometry=collision_geometry,
        )
        ext = metrics_extended.measure_extended(
            positions, edges, widths, heights, cluster_ids,
            model_ids=model_ids,
        )
        base5 = [m.cross, m.overlaps, m.edge_node, m.bbox_b, m.score]
        ext14 = [
            ext.stress_score, ext.edge_length_cv, ext.crossing_angle_mean,
            ext.crossing_angle_cv, ext.hub_clearance_p10, ext.cluster_compactness_mean,
            ext.composite_quality, ext.sub_clean_quality, ext.sub_severity_quality,
            ext.sub_angle_quality, ext.sub_stress_quality, ext.sub_compact_quality,
            ext.sub_uniform_quality, ext.sub_spread_quality,
        ]
        comp_row = features_for_ckpt(
            composite_ckpt, node_static, node_static_names, edges, positions, base5, ext14)
        leg_row = features_for_ckpt(
            legacy_ckpt, node_static, node_static_names, edges, positions, base5, ext14)
        comp_top = predict_top_actions(composite_ckpt, composite_model, comp_row, args.topk)
        leg_top = predict_top_actions(legacy_ckpt, legacy_model, leg_row, args.topk)

        print(f"=== start: {name} ===")
        print(f"  v34 metrics: cross={m.cross} overlaps={m.overlaps} edgeNode={m.edge_node} score={m.score:.1f}")
        print(f"  composite quality: {ext.composite_quality:.4f}")
        print(f"  {'rank':>4}  {'composite (act, score)':<60}  {'legacy (act, score)':<60}  match?")
        comp_names_top = {a for a, _ in comp_top}
        leg_names_top = {a for a, _ in leg_top}
        for k in range(args.topk):
            c_name = comp_top[k][0] if k < len(comp_top) else ""
            c_score = comp_top[k][1] if k < len(comp_top) else 0.0
            l_name = leg_top[k][0] if k < len(leg_top) else ""
            l_score = leg_top[k][1] if k < len(leg_top) else 0.0
            same = "✓" if c_name == l_name else "✗"
            print(f"  {k+1:>4}  {c_name:<45} {c_score:>10.2f}     {l_name:<45} {l_score:>10.2f}  {same}")
        overlap = len(comp_names_top & leg_names_top)
        print(f"  top-{args.topk} overlap: {overlap}/{args.topk}")
        print()


def build_v35_action_dataset_helpers(layout, edges):
    """Mirror build_v35_action_dataset.py's node_static_features helper."""
    build_v35 = load_module(
        "build_v35_action_dataset",
        ROOT / "scripts/erd-poc/build_v35_action_dataset.py",
    )
    return build_v35.node_static_features(layout, edges)


if __name__ == "__main__":
    main()
