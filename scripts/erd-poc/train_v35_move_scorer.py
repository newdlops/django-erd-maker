#!/usr/bin/env python3
"""Train v35 move scorer from v34 counterfactual JSONL logs.

This is the "judgment" model path: instead of imitating final coordinates,
learn to score generic candidate moves by their exact-measured improvement.
The model consumes fixed aggregate features, so it can run on graphs with
different node counts as long as the candidate generator emits the same
feature schema.
"""

from __future__ import annotations

import argparse
import json
import math
import random
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F


NUMERIC_NAMES = [
    "graph.nodes",
    "graph.edges",
    "base.cross",
    "base.overlaps",
    "base.bbox_b",
    "base.score",
    "candidate.moved",
    "candidate.movedFrac",
    "candidate.dx",
    "candidate.dy",
    "candidate.moveDist",
    "candidate.priority",
    "candidate.groupIsLouvain",
    "candidate.otherGroupIsLouvain",
    "candidate.sourceGroupIsLouvain",
    "candidate.groupIsPseudo",
    "candidate.otherGroupIsPseudo",
    "candidate.groupIsComponent",
    "candidate.semanticScore",
    "candidate.semanticTokenJaccard",
    "candidate.semanticSharedTokens",
    "candidate.semanticAppMatch",
    "candidate.semanticTargetSize",
    "candidate.semanticTargetCrossIncident",
    "candidate.meanCrossIncident",
    "candidate.maxCrossIncident",
    "candidate.centroidX",
    "candidate.centroidY",
    "candidate.spreadX",
    "candidate.spreadY",
    "candidate.targetSpreadX",
    "candidate.targetSpreadY",
    "candidate.targetCentroidX",
    "candidate.targetCentroidY",
    "candidate.targetDeltaX",
    "candidate.targetDeltaY",
    "candidate.targetMoveDistMean",
    "candidate.targetMoveDistMax",
    "candidate.targetMoveDistStd",
    "candidate.swapDistance",
    "candidate.otherMeanCrossIncident",
    "log.graph.nodes",
    "log.graph.edges",
    "log.base.cross",
    "log.base.score",
    "log.candidate.moved",
    "log.candidate.moveDist",
    "log.candidate.priority",
    "log.candidate.meanCrossIncident",
    "log.candidate.maxCrossIncident",
    "log.candidate.targetMoveDistMean",
    "log.candidate.targetMoveDistMax",
]


@dataclass
class Example:
    group: str
    kind: str
    numeric: dict[str, float]
    gain: float


def finite(value: object, default: float = 0.0) -> float:
    try:
        got = float(value)
    except (TypeError, ValueError):
        return default
    if math.isfinite(got):
        return got
    return default


def read_examples(paths: list[Path]) -> list[Example]:
    examples: list[Example] = []
    for file_idx, path in enumerate(paths):
        with path.open() as fh:
            for line_idx, line in enumerate(fh):
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    continue
                graph = obj.get("graph") or {}
                base = obj.get("base") or {}
                cand = obj.get("candidate") or {}
                action = obj.get("action") or {}
                n_nodes = max(1.0, finite(graph.get("nodes"), 1.0))
                dx = finite(cand.get("dx"))
                dy = finite(cand.get("dy"))
                moved = finite(cand.get("moved"))
                move_dist = math.hypot(dx, dy)
                priority = finite(cand.get("priority"))
                mean_cross = finite(cand.get("meanCrossIncident"))
                max_cross = finite(cand.get("maxCrossIncident"))
                target_move_mean = finite(cand.get("targetMoveDistMean"))
                target_move_max = finite(cand.get("targetMoveDistMax"))
                numeric = {
                    "graph.nodes": n_nodes,
                    "graph.edges": finite(graph.get("edges")),
                    "base.cross": finite(base.get("cross")),
                    "base.overlaps": finite(base.get("overlaps")),
                    "base.bbox_b": finite(base.get("bbox_b")),
                    "base.score": finite(base.get("score")),
                    "candidate.moved": moved,
                    "candidate.movedFrac": moved / n_nodes,
                    "candidate.dx": dx,
                    "candidate.dy": dy,
                    "candidate.moveDist": move_dist,
                    "candidate.priority": priority,
                    "candidate.groupIsLouvain": finite(cand.get("groupIsLouvain")),
                    "candidate.otherGroupIsLouvain": finite(
                        cand.get("otherGroupIsLouvain")
                    ),
                    "candidate.sourceGroupIsLouvain": finite(
                        cand.get("sourceGroupIsLouvain")
                    ),
                    "candidate.groupIsPseudo": finite(cand.get("groupIsPseudo")),
                    "candidate.otherGroupIsPseudo": finite(
                        cand.get("otherGroupIsPseudo")
                    ),
                    "candidate.groupIsComponent": finite(cand.get("groupIsComponent")),
                    "candidate.semanticScore": finite(cand.get("semanticScore")),
                    "candidate.semanticTokenJaccard": finite(
                        cand.get("semanticTokenJaccard")
                    ),
                    "candidate.semanticSharedTokens": finite(
                        cand.get("semanticSharedTokens")
                    ),
                    "candidate.semanticAppMatch": finite(cand.get("semanticAppMatch")),
                    "candidate.semanticTargetSize": finite(
                        cand.get("semanticTargetSize")
                    ),
                    "candidate.semanticTargetCrossIncident": finite(
                        cand.get("semanticTargetCrossIncident")
                    ),
                    "candidate.meanCrossIncident": mean_cross,
                    "candidate.maxCrossIncident": max_cross,
                    "candidate.centroidX": finite(cand.get("centroidX")),
                    "candidate.centroidY": finite(cand.get("centroidY")),
                    "candidate.spreadX": finite(cand.get("spreadX")),
                    "candidate.spreadY": finite(cand.get("spreadY")),
                    "candidate.targetSpreadX": finite(cand.get("targetSpreadX")),
                    "candidate.targetSpreadY": finite(cand.get("targetSpreadY")),
                    "candidate.targetCentroidX": finite(cand.get("targetCentroidX")),
                    "candidate.targetCentroidY": finite(cand.get("targetCentroidY")),
                    "candidate.targetDeltaX": finite(cand.get("targetDeltaX")),
                    "candidate.targetDeltaY": finite(cand.get("targetDeltaY")),
                    "candidate.targetMoveDistMean": target_move_mean,
                    "candidate.targetMoveDistMax": target_move_max,
                    "candidate.targetMoveDistStd": finite(cand.get("targetMoveDistStd")),
                    "candidate.swapDistance": finite(cand.get("swapDistance")),
                    "candidate.otherMeanCrossIncident": finite(
                        cand.get("otherMeanCrossIncident")
                    ),
                    "log.graph.nodes": math.log1p(n_nodes),
                    "log.graph.edges": math.log1p(max(0.0, finite(graph.get("edges")))),
                    "log.base.cross": math.log1p(max(0.0, finite(base.get("cross")))),
                    "log.base.score": math.log1p(max(0.0, finite(base.get("score")))),
                    "log.candidate.moved": math.log1p(max(0.0, moved)),
                    "log.candidate.moveDist": math.log1p(max(0.0, move_dist)),
                    "log.candidate.priority": math.log1p(max(0.0, priority)),
                    "log.candidate.meanCrossIncident": math.log1p(
                        max(0.0, mean_cross)
                    ),
                    "log.candidate.maxCrossIncident": math.log1p(
                        max(0.0, max_cross)
                    ),
                    "log.candidate.targetMoveDistMean": math.log1p(
                        max(0.0, target_move_mean)
                    ),
                    "log.candidate.targetMoveDistMax": math.log1p(
                        max(0.0, target_move_max)
                    ),
                }
                gain = -finite(obj.get("deltaScore"))
                round_id = obj.get("round", "?")
                examples.append(
                    Example(
                        group=f"{file_idx}:{round_id}",
                        kind=str(
                            action.get("type")
                            or cand.get("kind")
                            or "unknown"
                        ),
                        numeric=numeric,
                        gain=gain,
                    )
                )
    return examples


def split_by_group(
    examples: list[Example], val_frac: float, seed: int
) -> tuple[list[int], list[int]]:
    groups = sorted({ex.group for ex in examples})
    rng = random.Random(seed)
    rng.shuffle(groups)
    n_val = max(1, int(round(len(groups) * val_frac))) if len(groups) > 1 else 0
    val_groups = set(groups[:n_val])
    train_idx: list[int] = []
    val_idx: list[int] = []
    for idx, ex in enumerate(examples):
        (val_idx if ex.group in val_groups else train_idx).append(idx)
    return train_idx, val_idx


def build_matrix(
    examples: list[Example],
    kind_to_idx: dict[str, int],
) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[str]]:
    n = len(examples)
    x_num = np.zeros((n, len(NUMERIC_NAMES)), dtype=np.float32)
    x_kind = np.zeros((n, len(kind_to_idx)), dtype=np.float32)
    gains = np.zeros(n, dtype=np.float32)
    groups: list[str] = []
    for row, ex in enumerate(examples):
        for col, name in enumerate(NUMERIC_NAMES):
            x_num[row, col] = np.float32(ex.numeric.get(name, 0.0))
        x_kind[row, kind_to_idx[ex.kind]] = 1.0
        gains[row] = np.float32(ex.gain)
        groups.append(ex.group)
    return np.concatenate([x_num, x_kind], axis=1), gains, np.array(groups), groups


class MoveScorer(nn.Module):
    def __init__(self, in_dim: int, hidden: int, layers: int, dropout: float):
        super().__init__()
        blocks: list[nn.Module] = []
        cur = in_dim
        for _ in range(layers):
            blocks.append(nn.Linear(cur, hidden))
            blocks.append(nn.SiLU())
            blocks.append(nn.Dropout(dropout))
            cur = hidden
        self.body = nn.Sequential(*blocks)
        self.gain_head = nn.Linear(cur, 1)
        self.good_head = nn.Linear(cur, 1)

    def forward(self, x: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        h = self.body(x)
        return self.gain_head(h).squeeze(-1), self.good_head(h).squeeze(-1)


def standardize(x: np.ndarray, train_idx: list[int]) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    mean = x[train_idx].mean(axis=0, keepdims=True)
    std = x[train_idx].std(axis=0, keepdims=True)
    std[std < 1e-6] = 1.0
    return (x - mean) / std, mean.squeeze(0), std.squeeze(0)


def inverse_gain(y: torch.Tensor, gain_scale: float) -> torch.Tensor:
    return torch.sinh(y) * gain_scale


def topk_metrics(
    pred_score: np.ndarray,
    true_gain: np.ndarray,
    groups: np.ndarray,
    idxs: list[int],
    ks: tuple[int, ...] = (1, 5, 20),
) -> dict[str, float]:
    by_group: dict[str, list[int]] = {}
    for idx in idxs:
        by_group.setdefault(str(groups[idx]), []).append(idx)
    hits = {k: 0 for k in ks}
    regrets: list[float] = []
    eligible = 0
    for members in by_group.values():
        best_idx = max(members, key=lambda i: true_gain[i])
        best_gain = float(true_gain[best_idx])
        if best_gain <= 0:
            continue
        eligible += 1
        ranked = sorted(members, key=lambda i: pred_score[i], reverse=True)
        for k in ks:
            if best_idx in ranked[: min(k, len(ranked))]:
                hits[k] += 1
        regrets.append(best_gain - float(true_gain[ranked[0]]))
    out: dict[str, float] = {"eligibleGroups": float(eligible)}
    if eligible == 0:
        for k in ks:
            out[f"top{k}Hit"] = 0.0
        out["top1Regret"] = 0.0
        return out
    for k in ks:
        out[f"top{k}Hit"] = hits[k] / eligible
    out["top1Regret"] = float(np.mean(regrets)) if regrets else 0.0
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--jsonl", type=Path, nargs="+", required=True)
    ap.add_argument("--ckpt", type=Path, required=True)
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--batch-size", type=int, default=1024)
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--layers", type=int, default=3)
    ap.add_argument("--dropout", type=float, default=0.05)
    ap.add_argument("--lr", type=float, default=3e-4)
    ap.add_argument("--val-frac", type=float, default=0.2)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--gain-scale", type=float, default=20.0)
    ap.add_argument("--rank-weight", type=float, default=1.0)
    ap.add_argument("--rank-temperature", type=float, default=50.0)
    ap.add_argument("--device", default="cpu")
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    random.seed(args.seed)
    np.random.seed(args.seed)

    examples = read_examples(args.jsonl)
    if not examples:
        raise SystemExit("no examples loaded")
    kinds = sorted({ex.kind for ex in examples})
    kind_to_idx = {kind: idx for idx, kind in enumerate(kinds)}
    x, gain, groups, _group_list = build_matrix(examples, kind_to_idx)
    train_idx, val_idx = split_by_group(examples, args.val_frac, args.seed)
    if not train_idx:
        raise SystemExit("empty train split")
    if not val_idx:
        val_idx = train_idx[:]
    x, mean, std = standardize(x, train_idx)
    y = np.arcsinh(gain / args.gain_scale).astype(np.float32)
    good = (gain > 0).astype(np.float32)

    device = torch.device(args.device)
    xt = torch.from_numpy(x).to(device)
    yt = torch.from_numpy(y).to(device)
    good_t = torch.from_numpy(good).to(device)
    model = MoveScorer(x.shape[1], args.hidden, args.layers, args.dropout).to(device)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    n_pos = max(1.0, float(good[train_idx].sum()))
    n_neg = max(1.0, float(len(train_idx) - good[train_idx].sum()))
    pos_weight = torch.tensor([min(20.0, n_neg / n_pos)], device=device)

    print(
        f"loaded examples={len(examples)} groups={len(set(groups))} "
        f"kinds={len(kinds)} train={len(train_idx)} val={len(val_idx)}"
    )
    print(f"positive train={int(good[train_idx].sum())}/{len(train_idx)}")
    t0 = time.time()
    train_idx_arr = np.array(train_idx, dtype=np.int64)
    val_idx_arr = np.array(val_idx, dtype=np.int64)
    group_to_train_idxs: dict[str, np.ndarray] = {}
    for group in sorted({str(groups[i]) for i in train_idx}):
        members = [i for i in train_idx if str(groups[i]) == group]
        if max(float(gain[i]) for i in members) > 0:
            group_to_train_idxs[group] = np.array(members, dtype=np.int64)
    best = float("inf")
    best_state = None
    for epoch in range(1, args.epochs + 1):
        model.train()
        np.random.shuffle(train_idx_arr)
        total = 0.0
        seen = 0
        for start in range(0, len(train_idx_arr), args.batch_size):
            batch_np = train_idx_arr[start : start + args.batch_size]
            batch = torch.from_numpy(batch_np).to(device)
            pred_y, pred_good = model(xt[batch])
            loss_reg = F.smooth_l1_loss(pred_y, yt[batch])
            loss_cls = F.binary_cross_entropy_with_logits(
                pred_good,
                good_t[batch],
                pos_weight=pos_weight,
            )
            loss = loss_reg + 0.25 * loss_cls
            opt.zero_grad(set_to_none=True)
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            opt.step()
            total += float(loss.item()) * int(batch.numel())
            seen += int(batch.numel())
        if args.rank_weight > 0 and group_to_train_idxs:
            rank_groups = list(group_to_train_idxs.values())
            random.shuffle(rank_groups)
            for members_np in rank_groups:
                members = torch.from_numpy(members_np).to(device)
                _pred_gain, pred_rank = model(xt[members])
                gains_group = torch.clamp(
                    torch.from_numpy(gain[members_np]).to(device),
                    min=0.0,
                )
                if gains_group.max() <= 0:
                    continue
                target = torch.softmax(
                    gains_group / max(1e-6, args.rank_temperature),
                    dim=0,
                )
                rank_loss = -(target * F.log_softmax(pred_rank, dim=0)).sum()
                opt.zero_grad(set_to_none=True)
                (args.rank_weight * rank_loss).backward()
                nn.utils.clip_grad_norm_(model.parameters(), 5.0)
                opt.step()

        model.eval()
        with torch.no_grad():
            pred_y_val, pred_good_val = model(xt[val_idx_arr])
            val_reg = F.smooth_l1_loss(pred_y_val, yt[val_idx_arr]).item()
            pred_y_all, pred_rank_all = model(xt)
            pred_gain_all = inverse_gain(pred_y_all, args.gain_scale).cpu().numpy()
            pred_rank_all_np = pred_rank_all.cpu().numpy()
            pred_gain_val = pred_gain_all[val_idx_arr]
            true_gain_val = gain[val_idx_arr]
            mae = float(np.mean(np.abs(pred_gain_val - true_gain_val)))
            corr = (
                float(np.corrcoef(pred_gain_val, true_gain_val)[0, 1])
                if len(val_idx_arr) > 1
                else 0.0
            )
            if not math.isfinite(corr):
                corr = 0.0
            top = topk_metrics(pred_rank_all_np, gain, groups, val_idx)
        if val_reg < best:
            best = val_reg
            best_state = {k: v.detach().cpu() for k, v in model.state_dict().items()}
        print(
            f"epoch {epoch:03d} trainLoss={total/max(1, seen):.4f} "
            f"valReg={val_reg:.4f} maeGain={mae:.2f} corr={corr:.3f} "
            f"top1={top['top1Hit']:.3f} top5={top['top5Hit']:.3f} "
            f"regret={top['top1Regret']:.2f}"
        )

    args.ckpt.parent.mkdir(parents=True, exist_ok=True)
    if best_state is not None:
        model.load_state_dict(best_state)
    torch.save(
        {
            "model": model.state_dict(),
            "numeric_names": NUMERIC_NAMES,
            "kind_to_idx": kind_to_idx,
            "mean": mean,
            "std": std,
            "gain_scale": args.gain_scale,
            "hidden": args.hidden,
            "layers": args.layers,
            "dropout": args.dropout,
            "args": vars(args),
        },
        args.ckpt,
    )
    print(f"saved {args.ckpt} elapsed={time.time() - t0:.1f}s")


if __name__ == "__main__":
    main()
