#!/usr/bin/env python3
"""Train a state-level prior over useful action families.

v36 scores concrete candidate moves after the generator has already produced
them.  This v37 prior learns a cheaper question from the same exact-verifier
data: for the current layout state, which action families are worth checking?
It does not replace exact verification; it narrows the verification surface.
"""

from __future__ import annotations

import argparse
import math
import random
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F


STATE_METRIC_NAMES = ["cross", "overlaps", "edgeNode", "bboxB", "score"]

# Extended ML-signal metric column names emitted by build_v35_action_dataset
# when --extended-metrics is enabled (Gansner stress + distribution stats).
# Order matches metrics_extended.measure_extended's append order.
EXTENDED_METRIC_NAMES = [
    "stress",
    "edge_length_cv",
    "crossing_angle_mean",
    "crossing_angle_cv",
    "hub_clearance_p10",
    "cluster_compactness_mean",
]

# Composite quality + 7 sub-scores. Present when dataset was built with
# --extended-metrics on a recent build_v35 (after compositeQuality
# integration). Detection is name-based via state_metric_names.
COMPOSITE_METRIC_NAMES = [
    "composite_quality",
    "sub_clean_quality",
    "sub_severity_quality",
    "sub_angle_quality",
    "sub_stress_quality",
    "sub_compact_quality",
    "sub_uniform_quality",
    "sub_spread_quality",
]


def state_feature_names(
    static_names: list[str],
    has_extended: bool = False,
    has_composite: bool = False,
) -> list[str]:
    names = [
        "graph.logNodes",
        "graph.logEdges",
        "metric.logCross",
        "metric.logVisualCross",
        "metric.logOverlaps",
        "metric.logEdgeNode",
        "metric.bboxB",
        "metric.logScore",
        "rate.crossPerEdge",
        "rate.edgeNodePerEdge",
        "rate.overlapPerNode",
        "pos.logStdX",
        "pos.logStdY",
        "pos.logSpanX",
        "pos.logSpanY",
        "pos.logAreaB",
        "pos.logAspect",
    ]
    if has_extended:
        # Extended signals: stress + uniformity + geometry quality.
        # All log1p-scaled except dimensionless ratios so the magnitudes
        # land in a network-friendly range alongside the other features.
        names.extend([
            "metric.logStress",
            "metric.edgeLengthCv",
            "metric.crossingAngleMean",
            "metric.crossingAngleCv",
            "metric.logHubClearP10",
            "metric.logClusterCompact",
        ])
    if has_composite:
        # Bennett-weighted composite quality + 7 sub-scores. Already in
        # [0, 1] so no extra scaling needed.
        names.extend([
            "metric.compositeQuality",
            "metric.subClean",
            "metric.subSeverity",
            "metric.subAngle",
            "metric.subStress",
            "metric.subCompact",
            "metric.subUniform",
            "metric.subSpread",
        ])
    names.extend([f"node.mean.{name}" for name in static_names])
    names.extend([f"node.max.{name}" for name in static_names])
    names.extend([f"node.std.{name}" for name in static_names])
    return names


def state_feature_values(
    node_static: np.ndarray,
    static_names: list[str],
    edges: np.ndarray,
    positions: np.ndarray,
    metrics: np.ndarray,
    has_extended: bool = False,
    has_composite: bool = False,
) -> np.ndarray:
    node_count = float(max(1, positions.shape[0]))
    edge_count = float(max(1, edges.shape[0]))
    cross = float(metrics[0])
    overlaps = float(metrics[1])
    edge_node = float(metrics[2])
    bbox_b = float(metrics[3])
    score = float(metrics[4])
    span = positions.max(axis=0) - positions.min(axis=0)
    area_b = float((span[0] * span[1]) / 1_000_000_000.0)
    aspect = float(span[0] / max(1.0, span[1]))
    values = [
        math.log1p(node_count),
        math.log1p(edge_count),
        math.log1p(max(0.0, cross)),
        math.log1p(max(0.0, cross + edge_node)),
        math.log1p(max(0.0, overlaps)),
        math.log1p(max(0.0, edge_node)),
        bbox_b,
        math.log1p(max(0.0, score)),
        cross / edge_count,
        edge_node / edge_count,
        overlaps / node_count,
        math.log1p(float(positions[:, 0].std())),
        math.log1p(float(positions[:, 1].std())),
        math.log1p(float(max(0.0, span[0]))),
        math.log1p(float(max(0.0, span[1]))),
        math.log1p(max(0.0, area_b)),
        math.log1p(max(0.0, aspect)),
    ]
    if has_extended:
        stress = float(metrics[5])
        edge_len_cv = float(metrics[6])
        x_ang_mean = float(metrics[7])
        x_ang_cv = float(metrics[8])
        hub_clear = float(metrics[9])
        cluster_compact = float(metrics[10])
        values.extend([
            math.log1p(max(0.0, stress)),
            edge_len_cv,
            x_ang_mean,
            x_ang_cv,
            math.log1p(max(0.0, hub_clear)),
            math.log1p(max(0.0, cluster_compact)),
        ])
    if has_composite:
        # composite_quality (0..1) + 7 sub-scores (each 0..1). Already
        # network-friendly magnitudes; no extra scaling.
        for idx in range(11, 19):
            values.append(float(metrics[idx]))
    values.extend(float(x) for x in node_static.mean(axis=0))
    values.extend(float(x) for x in node_static.max(axis=0))
    values.extend(float(x) for x in node_static.std(axis=0))
    assert len(values) == len(
        state_feature_names(static_names, has_extended, has_composite)
    )
    return np.array(values, dtype=np.float32)


class FamilyPrior(nn.Module):
    def __init__(self, in_dim: int, out_dim: int, hidden: int, layers: int, dropout: float):
        super().__init__()
        blocks: list[nn.Module] = []
        cur = in_dim
        for _ in range(layers):
            blocks.extend([nn.Linear(cur, hidden), nn.SiLU(), nn.Dropout(dropout)])
            cur = hidden
        self.body = nn.Sequential(*blocks)
        self.gain = nn.Linear(cur, out_dim)
        self.accept = nn.Linear(cur, out_dim)

    def forward(self, x):
        h = self.body(x)
        return self.gain(h), self.accept(h)


def _detect_has_extended(data) -> bool:
    """Detect whether dataset includes extended state_metrics columns.

    Prefers the explicit state_metric_names array (recorded by
    build_v35_action_dataset.py when --extended-metrics was set). Falls
    back to inspecting the state_metrics row width for older datasets.
    """
    if "state_metric_names" in data.files:
        names = [str(x) for x in data["state_metric_names"]]
        return any(n in EXTENDED_METRIC_NAMES for n in names)
    return int(data["state_metrics"].shape[1]) >= 5 + len(EXTENDED_METRIC_NAMES)


def _detect_has_composite(data) -> bool:
    """Composite + 7 sub-scores live at columns 11..18 in datasets
    built by a build_v35 newer than the compositeQuality integration."""
    if "state_metric_names" in data.files:
        names = [str(x) for x in data["state_metric_names"]]
        return any(n in COMPOSITE_METRIC_NAMES for n in names)
    return int(data["state_metrics"].shape[1]) >= 5 + len(EXTENDED_METRIC_NAMES) + len(COMPOSITE_METRIC_NAMES)


def load_states(paths: list[Path]) -> tuple[np.ndarray, list[str], np.ndarray, list[str]]:
    feature_names: list[str] | None = None
    has_extended: bool | None = None
    has_composite: bool | None = None
    action_names: list[str] = []
    action_index: dict[str, int] = {}
    rows: list[np.ndarray] = []
    target_maps: list[dict[str, float]] = []

    for path in paths:
        data = np.load(path, allow_pickle=False)
        static_names = [str(x) for x in data["node_static_names"]]
        path_has_extended = _detect_has_extended(data)
        path_has_composite = _detect_has_composite(data)
        if has_extended is None:
            has_extended = path_has_extended
            has_composite = path_has_composite
        elif has_extended != path_has_extended or has_composite != path_has_composite:
            raise ValueError(
                f"dataset {path} mixes extended/composite metric schemas "
                f"with prior datasets"
            )
        names = state_feature_names(static_names, has_extended, has_composite)
        if feature_names is None:
            feature_names = names
        elif feature_names != names:
            raise ValueError(f"incompatible state feature names in {path}")
        local_actions = [str(x) for x in data["action_type_names"]]
        for action in local_actions:
            if action not in action_index:
                action_index[action] = len(action_names)
                action_names.append(action)

        sample_state = data["sample_state"].astype(np.int64)
        sample_action = data["sample_action_type"].astype(np.int64)
        sample_gain = data["sample_gain"].astype(np.float32)
        for state_id in range(int(data["state_positions"].shape[0])):
            rows.append(
                state_feature_values(
                    data["node_static"].astype(np.float32),
                    static_names,
                    data["edges"].astype(np.int32),
                    data["state_positions"][state_id].astype(np.float32),
                    data["state_metrics"][state_id].astype(np.float32),
                    has_extended,
                    has_composite,
                )
            )
            state_mask = sample_state == state_id
            best_by_action: dict[str, float] = {}
            for action_idx, action_name in enumerate(local_actions):
                gains = sample_gain[state_mask & (sample_action == action_idx)]
                if gains.size == 0:
                    continue
                best_by_action[action_name] = max(0.0, float(gains.max()))
            target_maps.append(best_by_action)
        print(
            f"dataset {path}: states={data['state_positions'].shape[0]} "
            f"actions={len(local_actions)} "
            f"extendedMetrics={path_has_extended} "
            f"compositeMetrics={path_has_composite}",
            flush=True,
        )

    assert feature_names is not None
    y = np.zeros((len(rows), len(action_names)), dtype=np.float32)
    for row, target_map in enumerate(target_maps):
        for action_name, gain in target_map.items():
            y[row, action_index[action_name]] = gain
    return np.vstack(rows).astype(np.float32), feature_names, y, action_names


def standardize(x: np.ndarray, train_idx: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    mean = x[train_idx].mean(axis=0, keepdims=True)
    std = x[train_idx].std(axis=0, keepdims=True)
    std[std < 1e-6] = 1.0
    return ((x - mean) / std).astype(np.float32), mean.squeeze(0), std.squeeze(0)


def split_rows(n: int, val_frac: float, seed: int) -> tuple[np.ndarray, np.ndarray]:
    idx = np.arange(n, dtype=np.int64)
    rng = np.random.default_rng(seed)
    rng.shuffle(idx)
    n_val = max(1, int(round(n * val_frac))) if n > 1 else 0
    val = idx[:n_val]
    train = idx[n_val:]
    if train.size == 0:
        train = val.copy()
    if val.size == 0:
        val = train.copy()
    return train.astype(np.int64), val.astype(np.int64)


def topk_metrics(scores: np.ndarray, gains: np.ndarray, idxs: np.ndarray, ks: tuple[int, ...]) -> dict[str, float]:
    out: dict[str, float] = {}
    best_actions = np.argmax(gains[idxs], axis=1)
    best_gains = gains[idxs, best_actions]
    eligible = best_gains > 0
    if not np.any(eligible):
        for k in ks:
            out[f"hit@{k}"] = 0.0
            out[f"regret@{k}"] = 0.0
        return out
    for k in ks:
        hits = 0
        regrets: list[float] = []
        for local_row, idx in enumerate(idxs):
            if best_gains[local_row] <= 0:
                continue
            order = np.argsort(-scores[idx])[: min(k, scores.shape[1])]
            if int(best_actions[local_row]) in set(int(x) for x in order):
                hits += 1
            top_gain = float(gains[idx, order].max()) if order.size else 0.0
            regrets.append(float(best_gains[local_row]) - top_gain)
        denom = int(np.sum(eligible))
        out[f"hit@{k}"] = hits / max(1, denom)
        out[f"regret@{k}"] = float(np.mean(regrets)) if regrets else 0.0
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", type=Path, action="append", required=True)
    ap.add_argument("--ckpt", type=Path, required=True)
    ap.add_argument("--epochs", type=int, default=120)
    ap.add_argument("--batch-size", type=int, default=32)
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--layers", type=int, default=2)
    ap.add_argument("--dropout", type=float, default=0.05)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--val-frac", type=float, default=0.25)
    ap.add_argument("--seed", type=int, default=37)
    ap.add_argument("--gain-scale", type=float, default=50.0)
    ap.add_argument("--accept-min-gain", type=float, default=1.0)
    ap.add_argument("--device", default="cpu")
    args = ap.parse_args()

    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)

    x_raw, feature_names, gains, action_names = load_states(args.dataset)
    train_idx, val_idx = split_rows(x_raw.shape[0], args.val_frac, args.seed)
    x, mean, std = standardize(x_raw, train_idx)
    y_gain = np.arcsinh(gains / args.gain_scale).astype(np.float32)
    y_accept = (gains >= args.accept_min_gain).astype(np.float32)

    device = torch.device(args.device)
    xt = torch.from_numpy(x).to(device)
    gain_t = torch.from_numpy(y_gain).to(device)
    accept_t = torch.from_numpy(y_accept).to(device)
    model = FamilyPrior(x.shape[1], gains.shape[1], args.hidden, args.layers, args.dropout).to(device)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)

    pos = max(1.0, float(y_accept[train_idx].sum()))
    neg = max(1.0, float(y_accept[train_idx].size - y_accept[train_idx].sum()))
    pos_weight = torch.full((gains.shape[1],), min(20.0, neg / pos), device=device)

    print(
        f"loaded states={x.shape[0]} actions={gains.shape[1]} "
        f"features={x.shape[1]} train={train_idx.size} val={val_idx.size}",
        flush=True,
    )
    best = float("inf")
    best_state = None
    train_arr = train_idx.copy()
    t0 = time.time()
    for epoch in range(1, args.epochs + 1):
        model.train()
        np.random.shuffle(train_arr)
        total = 0.0
        seen = 0
        for start in range(0, train_arr.size, args.batch_size):
            batch_np = train_arr[start : start + args.batch_size]
            batch = torch.from_numpy(batch_np).to(device)
            pred_gain, pred_accept = model(xt[batch])
            loss = F.smooth_l1_loss(pred_gain, gain_t[batch])
            loss = loss + 0.75 * F.binary_cross_entropy_with_logits(
                pred_accept,
                accept_t[batch],
                pos_weight=pos_weight,
            )
            opt.zero_grad(set_to_none=True)
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            opt.step()
            total += float(loss.item()) * int(batch.numel())
            seen += int(batch.numel())

        model.eval()
        with torch.no_grad():
            pred_gain, pred_accept = model(xt)
            pred_gain_np = torch.sinh(pred_gain).cpu().numpy() * args.gain_scale
            pred_prob_np = torch.sigmoid(pred_accept).cpu().numpy()
            score = pred_gain_np + pred_prob_np * args.gain_scale
            metrics = topk_metrics(score, gains, val_idx, (1, 3, 5, 8))
            val_loss = F.smooth_l1_loss(pred_gain[val_idx], gain_t[val_idx]).item()
        select_regret = metrics["regret@5"]
        if select_regret < best:
            best = select_regret
            best_state = {k: v.detach().cpu() for k, v in model.state_dict().items()}
        if epoch == 1 or epoch % 10 == 0 or epoch == args.epochs:
            print(
                f"epoch {epoch:03d} trainLoss={total/max(1, seen):.4f} "
                f"valLoss={val_loss:.4f} hit@3={metrics['hit@3']:.3f} "
                f"hit@5={metrics['hit@5']:.3f} regret@3={metrics['regret@3']:.1f} "
                f"regret@5={metrics['regret@5']:.1f}",
                flush=True,
            )

    if best_state is not None:
        model.load_state_dict(best_state)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "model": model.state_dict(),
            "feature_names": feature_names,
            "action_names": action_names,
            "mean": mean,
            "std": std,
            "hidden": args.hidden,
            "layers": args.layers,
            "dropout": args.dropout,
            "gain_scale": args.gain_scale,
            "accept_min_gain": args.accept_min_gain,
            "args": vars(args),
        },
        args.ckpt,
    )
    print(f"saved {args.ckpt} elapsed={time.time() - t0:.1f}s")


if __name__ == "__main__":
    main()
