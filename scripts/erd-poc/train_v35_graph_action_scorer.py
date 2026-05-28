#!/usr/bin/env python3
"""Train an action-aware scorer from v35 NPZ action datasets."""

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


def standardize(x: np.ndarray, train_idx: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    mean = x[train_idx].mean(axis=0, keepdims=True)
    std = x[train_idx].std(axis=0, keepdims=True)
    std[std < 1e-6] = 1.0
    return ((x - mean) / std).astype(np.float32), mean.squeeze(0), std.squeeze(0)


def build_action_features(data: np.lib.npyio.NpzFile) -> tuple[np.ndarray, list[str]]:
    node_static = data["node_static"].astype(np.float32)
    states = data["state_positions"].astype(np.float32)
    sample_state = data["sample_state"].astype(np.int64)
    sample_action_type = data["sample_action_type"].astype(np.int64)
    action_names = [str(x) for x in data["action_type_names"]]
    scalars = data["sample_scalars"].astype(np.float32)
    scalar_names = [f"scalar.{x}" for x in data["scalar_names"]]
    starts = data["sample_moved_start"].astype(np.int64)
    lens = data["sample_moved_len"].astype(np.int64)
    flat_indices = data["moved_indices"].astype(np.int64)
    flat_deltas = data["moved_deltas"].astype(np.float32)

    state_mean = states.mean(axis=1)
    state_std = states.reshape(states.shape[0], -1).std(axis=1) + 1e-6
    n = sample_state.shape[0]
    action_one_hot = np.zeros((n, len(action_names)), dtype=np.float32)
    action_one_hot[np.arange(n), sample_action_type] = 1.0

    local = np.zeros((n, node_static.shape[1] * 2 + 16), dtype=np.float32)
    for row in range(n):
        start = starts[row]
        length = lens[row]
        if length <= 0:
            continue
        idx = flat_indices[start : start + length]
        delta = flat_deltas[start : start + length]
        static = node_static[idx]
        pos = states[sample_state[row], idx]
        pos_norm = (pos - state_mean[sample_state[row]]) / state_std[sample_state[row]]
        dist = np.linalg.norm(delta, axis=1)
        cursor = 0
        local[row, cursor : cursor + node_static.shape[1]] = static.mean(axis=0)
        cursor += node_static.shape[1]
        local[row, cursor : cursor + node_static.shape[1]] = static.max(axis=0)
        cursor += node_static.shape[1]
        local[row, cursor : cursor + 2] = pos_norm.mean(axis=0)
        cursor += 2
        local[row, cursor : cursor + 2] = pos_norm.std(axis=0)
        cursor += 2
        local[row, cursor : cursor + 2] = delta.mean(axis=0)
        cursor += 2
        local[row, cursor : cursor + 2] = delta.std(axis=0)
        cursor += 2
        local[row, cursor] = float(length)
        cursor += 1
        local[row, cursor] = math.log1p(float(length))
        cursor += 1
        local[row, cursor] = float(dist.mean()) if dist.size else 0.0
        cursor += 1
        local[row, cursor] = float(dist.max()) if dist.size else 0.0
        cursor += 1
        local[row, cursor] = float(dist.std()) if dist.size else 0.0
        cursor += 1
        local[row, cursor] = math.log1p(float(dist.mean())) if dist.size else 0.0
        cursor += 1
        local[row, cursor] = math.log1p(float(dist.max())) if dist.size else 0.0
        cursor += 1
        local[row, cursor] = float((dist > 0).mean()) if dist.size else 0.0

    local_names = []
    static_names = [str(x) for x in data["node_static_names"]]
    local_names.extend([f"moved.mean.{name}" for name in static_names])
    local_names.extend([f"moved.max.{name}" for name in static_names])
    local_names.extend(
        [
            "moved.posMeanX",
            "moved.posMeanY",
            "moved.posStdX",
            "moved.posStdY",
            "delta.meanX",
            "delta.meanY",
            "delta.stdX",
            "delta.stdY",
            "moved.count",
            "moved.logCount",
            "delta.distMean",
            "delta.distMax",
            "delta.distStd",
            "delta.logDistMean",
            "delta.logDistMax",
            "delta.nonzeroFrac",
        ]
    )
    # Optional: per-sample state-level metrics. Datasets built with
    # --extended-metrics have 19 columns (5 base + 6 extended + 8
    # composite). Older 5-col datasets contribute the legacy cross/
    # overlaps/edgeNode/bbox/score row. Either way the row is appended
    # to the per-sample feature vector so the scorer sees the state
    # context, not just the action params + moved-node summary.
    if "state_metrics" in data.files:
        state_metrics = data["state_metrics"].astype(np.float32)
        per_sample_state = state_metrics[sample_state]
        if "state_metric_names" in data.files:
            sm_names = [
                f"state.{str(x)}" for x in data["state_metric_names"]
            ]
        else:
            sm_names = [f"state.col{i}" for i in range(per_sample_state.shape[1])]
    else:
        per_sample_state = np.zeros((n, 0), dtype=np.float32)
        sm_names = []
    names = (
        scalar_names
        + [f"action.{name}" for name in action_names]
        + local_names
        + sm_names
    )
    return np.concatenate([scalars, action_one_hot, local, per_sample_state], axis=1), names


class ActionScorer(nn.Module):
    def __init__(self, in_dim: int, hidden: int, layers: int, dropout: float):
        super().__init__()
        blocks: list[nn.Module] = []
        cur = in_dim
        for _ in range(layers):
            blocks.extend([nn.Linear(cur, hidden), nn.SiLU(), nn.Dropout(dropout)])
            cur = hidden
        self.body = nn.Sequential(*blocks)
        self.gain = nn.Linear(cur, 1)
        self.rank = nn.Linear(cur, 1)

    def forward(self, x):
        h = self.body(x)
        return self.gain(h).squeeze(-1), self.rank(h).squeeze(-1)


def merge_feature_sets(
    feature_sets: list[tuple[np.ndarray, list[str]]],
) -> tuple[np.ndarray, list[str]]:
    feature_names: list[str] = []
    feature_index: dict[str, int] = {}
    for _x, names in feature_sets:
        for name in names:
            if name in feature_index:
                continue
            feature_index[name] = len(feature_names)
            feature_names.append(name)

    total_rows = sum(x.shape[0] for x, _names in feature_sets)
    merged = np.zeros((total_rows, len(feature_names)), dtype=np.float32)
    cursor = 0
    for x, names in feature_sets:
        cols = np.array([feature_index[name] for name in names], dtype=np.int64)
        merged[cursor : cursor + x.shape[0], cols] = x
        cursor += x.shape[0]
    return merged, feature_names


def load_training_inputs(
    paths: list[Path],
) -> tuple[np.ndarray, list[str], np.ndarray, np.ndarray, int]:
    feature_sets: list[tuple[np.ndarray, list[str]]] = []
    gains: list[np.ndarray] = []
    states: list[np.ndarray] = []
    state_offset = 0
    for path in paths:
        data = np.load(path, allow_pickle=False)
        x_part, names = build_action_features(data)
        feature_sets.append((x_part, names))
        gains.append(data["sample_gain"].astype(np.float32))
        part_state = data["sample_state"].astype(np.int64) + state_offset
        states.append(part_state)
        state_offset += int(data["state_positions"].shape[0])
        print(
            f"dataset {path}: samples={x_part.shape[0]} "
            f"states={data['state_positions'].shape[0]} "
            f"features={x_part.shape[1]}",
            flush=True,
        )
    x_raw, feature_names = merge_feature_sets(feature_sets)
    return (
        x_raw,
        feature_names,
        np.concatenate(gains).astype(np.float32),
        np.concatenate(states).astype(np.int64),
        state_offset,
    )


def split_states_from_array(
    sample_state: np.ndarray,
    state_count: int,
    val_frac: float,
    seed: int,
) -> tuple[np.ndarray, np.ndarray]:
    state_ids = np.arange(state_count, dtype=np.int64)
    rng = np.random.default_rng(seed)
    rng.shuffle(state_ids)
    n_val = max(1, int(round(len(state_ids) * val_frac))) if len(state_ids) > 1 else 0
    val_states = set(int(x) for x in state_ids[:n_val])
    val_mask = np.array([int(s) in val_states for s in sample_state])
    train_idx = np.flatnonzero(~val_mask)
    val_idx = np.flatnonzero(val_mask)
    if train_idx.size == 0:
        train_idx = np.flatnonzero(val_mask)
    if val_idx.size == 0:
        val_idx = train_idx.copy()
    return train_idx.astype(np.int64), val_idx.astype(np.int64)


def build_accept_labels(
    gains: np.ndarray,
    states: np.ndarray,
    top_n: int,
    min_gain: float,
    max_regret: float,
) -> np.ndarray:
    labels = np.zeros(gains.shape[0], dtype=np.float32)
    for state_id in sorted(set(int(s) for s in states)):
        members = np.flatnonzero(states == state_id)
        positive = members[gains[members] >= min_gain]
        if positive.size == 0:
            continue
        ranked = positive[np.argsort(-gains[positive])]
        if top_n > 0:
            labels[ranked[:top_n]] = 1.0
        best_gain = float(gains[ranked[0]])
        labels[positive[gains[positive] >= best_gain - max_regret]] = 1.0
    return labels


def topk_metrics(scores, gains, states, idxs, accept, ks=(1, 5, 20, 50, 100)):
    by_state: dict[int, list[int]] = {}
    for idx in idxs:
        by_state.setdefault(int(states[idx]), []).append(int(idx))
    eligible = 0
    best_hits = {k: 0 for k in ks}
    accept_hits = {k: 0 for k in ks}
    positive_hits = {k: 0 for k in ks}
    regrets = {k: [] for k in ks}
    for members in by_state.values():
        best = max(members, key=lambda i: gains[i])
        best_gain = float(gains[best])
        if best_gain <= 0:
            continue
        eligible += 1
        ranked = sorted(members, key=lambda i: scores[i], reverse=True)
        for k in ks:
            head = ranked[: min(k, len(ranked))]
            if best in head:
                best_hits[k] += 1
            if any(accept[i] > 0 for i in head):
                accept_hits[k] += 1
            if any(gains[i] > 0 for i in head):
                positive_hits[k] += 1
            top_gain = max(float(gains[i]) for i in head)
            regrets[k].append(best_gain - top_gain)
    out = {"eligible": eligible}
    for k in ks:
        out[f"best{k}"] = best_hits[k] / max(1, eligible)
        out[f"accept{k}"] = accept_hits[k] / max(1, eligible)
        out[f"positive{k}"] = positive_hits[k] / max(1, eligible)
        out[f"regret{k}"] = float(np.mean(regrets[k])) if regrets[k] else 0.0
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", type=Path, action="append", required=True)
    ap.add_argument("--ckpt", type=Path, required=True)
    ap.add_argument("--epochs", type=int, default=60)
    ap.add_argument("--batch-size", type=int, default=2048)
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--layers", type=int, default=3)
    ap.add_argument("--dropout", type=float, default=0.05)
    ap.add_argument("--lr", type=float, default=3e-4)
    ap.add_argument("--val-frac", type=float, default=0.25)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--gain-scale", type=float, default=20.0)
    ap.add_argument("--rank-weight", type=float, default=1.0)
    ap.add_argument("--rank-temperature", type=float, default=50.0)
    ap.add_argument("--accept-top-n", type=int, default=64)
    ap.add_argument("--accept-min-gain", type=float, default=1.0)
    ap.add_argument("--accept-regret", type=float, default=100.0)
    ap.add_argument("--select-k", type=int, default=50)
    ap.add_argument("--device", default="cpu")
    args = ap.parse_args()

    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)

    x_raw, feature_names, gain, state, state_count = load_training_inputs(args.dataset)
    train_idx, val_idx = split_states_from_array(
        state,
        state_count,
        args.val_frac,
        args.seed,
    )
    x, mean, std = standardize(x_raw, train_idx)
    y = np.arcsinh(gain / args.gain_scale).astype(np.float32)
    positive = (gain > 0).astype(np.float32)
    accept = build_accept_labels(
        gain,
        state,
        args.accept_top_n,
        args.accept_min_gain,
        args.accept_regret,
    )

    device = torch.device(args.device)
    xt = torch.from_numpy(x).to(device)
    yt = torch.from_numpy(y).to(device)
    accept_t = torch.from_numpy(accept).to(device)
    model = ActionScorer(x.shape[1], args.hidden, args.layers, args.dropout).to(device)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    pos = max(1.0, float(accept[train_idx].sum()))
    neg = max(1.0, float(train_idx.size - accept[train_idx].sum()))
    pos_weight = torch.tensor([min(20.0, neg / pos)], device=device)

    train_states: dict[int, np.ndarray] = {}
    for s in sorted(set(int(state[i]) for i in train_idx)):
        members = train_idx[state[train_idx] == s]
        if gain[members].max() > 0:
            train_states[s] = members.astype(np.int64)

    print(
        f"loaded samples={gain.size} states={state_count} datasets={len(args.dataset)} "
        f"features={x.shape[1]} train={train_idx.size} val={val_idx.size} "
        f"positive={int(positive[train_idx].sum())} "
        f"acceptable={int(accept[train_idx].sum())}"
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
            pred_y, pred_rank = model(xt[batch])
            loss = F.smooth_l1_loss(pred_y, yt[batch])
            loss = loss + 0.5 * F.binary_cross_entropy_with_logits(
                pred_rank,
                accept_t[batch],
                pos_weight=pos_weight,
            )
            opt.zero_grad(set_to_none=True)
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            opt.step()
            total += float(loss.item()) * int(batch.numel())
            seen += int(batch.numel())
        if args.rank_weight > 0 and train_states:
            state_batches = list(train_states.values())
            random.shuffle(state_batches)
            for members_np in state_batches:
                members = torch.from_numpy(members_np).to(device)
                _pred_y, pred_rank = model(xt[members])
                accept_mask = torch.from_numpy(accept[members_np]).to(device)
                if accept_mask.max() <= 0:
                    continue
                target_gain = torch.clamp(
                    torch.from_numpy(gain[members_np]).to(device),
                    min=0.0,
                )
                weights = accept_mask * torch.exp(
                    torch.clamp(
                        target_gain / max(1e-6, args.rank_temperature),
                        max=20.0,
                    )
                )
                target = weights / weights.sum()
                rank_loss = -(target * F.log_softmax(pred_rank, dim=0)).sum()
                opt.zero_grad(set_to_none=True)
                (args.rank_weight * rank_loss).backward()
                nn.utils.clip_grad_norm_(model.parameters(), 5.0)
                opt.step()

        model.eval()
        with torch.no_grad():
            val_t = torch.from_numpy(val_idx).to(device)
            pred_y_val, _pred_good_val = model(xt[val_t])
            val_reg = F.smooth_l1_loss(pred_y_val, yt[val_t]).item()
            pred_y_all, pred_rank_all = model(xt)
            pred_gain = torch.sinh(pred_y_all).cpu().numpy() * args.gain_scale
            pred_rank = pred_rank_all.cpu().numpy()
            val_gain = gain[val_idx]
            mae = float(np.mean(np.abs(pred_gain[val_idx] - val_gain)))
            corr = (
                float(np.corrcoef(pred_gain[val_idx], val_gain)[0, 1])
                if val_idx.size > 1
                else 0.0
            )
            if not math.isfinite(corr):
                corr = 0.0
            top = topk_metrics(pred_rank, gain, state, val_idx, accept)
        select_regret = top.get(f"regret{args.select_k}", top["regret50"])
        if select_regret < best:
            best = select_regret
            best_state = {k: v.detach().cpu() for k, v in model.state_dict().items()}
        print(
            f"epoch {epoch:03d} trainLoss={total/max(1, seen):.4f} "
            f"valReg={val_reg:.4f} maeGain={mae:.2f} corr={corr:.3f} "
            f"best@20={top['best20']:.3f} accept@20={top['accept20']:.3f} "
            f"accept@50={top['accept50']:.3f} pos@50={top['positive50']:.3f} "
            f"regret@20={top['regret20']:.2f} regret@50={top['regret50']:.2f}",
            flush=True,
        )

    if best_state is not None:
        model.load_state_dict(best_state)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "model": model.state_dict(),
            "feature_names": feature_names,
            "mean": mean,
            "std": std,
            "hidden": args.hidden,
            "layers": args.layers,
            "dropout": args.dropout,
            "gain_scale": args.gain_scale,
            "accept_top_n": args.accept_top_n,
            "accept_min_gain": args.accept_min_gain,
            "accept_regret": args.accept_regret,
            "select_k": args.select_k,
            "args": vars(args),
        },
        args.ckpt,
    )
    print(f"saved {args.ckpt} elapsed={time.time() - t0:.1f}s")


if __name__ == "__main__":
    main()
