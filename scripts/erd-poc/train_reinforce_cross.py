#!/usr/bin/env python3
"""REINFORCE on captain ERD: reward = -edgeCrossings (real C++ measurement).

The policy predicts positions; at each step, we sample N candidates by
adding Gaussian noise (σ_position) to the predicted means, evaluate
each via C++ rigid reroute, and compute REINFORCE gradient:

    L = -E[(R - baseline) * log π(a | s)]

where π is Gaussian(μ=model_output, σ=hyperparam) and a is the
sampled positions tensor.

Init from v12-general-best so the starting point is reasonable.
"""

import argparse
import importlib.util
import json
import subprocess
import tempfile
import time
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
NODES_TSV = ROOT / "data/erd-poc/graphs/real-main/nodes.tsv"
EDGES_TSV = ROOT / "data/erd-poc/graphs/real-main/edges.tsv"
CAPTAIN_BASELINE = ROOT / "data/erd-poc/layouts/real-main.json"
EXPERT_LAYOUT = ROOT / "data/erd-poc/expert-strong/real-main.json"

spec = importlib.util.spec_from_file_location(
    "train_distill", Path(__file__).parent / "train_distill.py"
)
train_distill = importlib.util.module_from_spec(spec)
spec.loader.exec_module(train_distill)

apply_spec = importlib.util.spec_from_file_location(
    "apply_distill_real", Path(__file__).parent / "apply_distill_real.py"
)
apply_distill_real = importlib.util.module_from_spec(apply_spec)
apply_spec.loader.exec_module(apply_distill_real)


def eval_real_cross(positions_real: torch.Tensor,
                    ids: list,
                    work: Path,
                    rigid: bool = True) -> int:
    """Write positions tsv, run C++ rigid reroute, return cross count."""
    tsv = work / f"p-{time.time_ns()}.tsv"
    with tsv.open("w") as f:
        for i, mid in enumerate(ids):
            x = positions_real[i, 0].item()
            y = positions_real[i, 1].item()
            f.write(f"{mid}\t{x:.3f}\t{y:.3f}\n")
    args = [str(BINARY), "layout",
            "--mode", "hierarchical_barycenter",
            "--nodes-file", str(NODES_TSV),
            "--edges-file", str(EDGES_TSV),
            "--edge-routing", "straight",
            "--cluster-graph", "1",
            "--positions-tsv", str(tsv)]
    if rigid:
        args += ["--rigid-positions", "1"]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"binary failed: {r.stderr[:300]}")
    d = json.loads(r.stdout)
    nodes = d["nodes"]
    xs = [n["position"]["x"] + n["size"]["width"]/2 for n in nodes]
    ys = [n["position"]["y"] + n["size"]["height"]/2 for n in nodes]
    bbox = (max(xs)-min(xs)) * (max(ys)-min(ys)) / 1e9
    tsv.unlink(missing_ok=True)
    return int(d["engineMetadata"]["edgeCrossings"]), bbox


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--init-from", type=Path, required=True)
    p.add_argument("--ckpt", type=Path, required=True)
    p.add_argument("--steps", type=int, default=50)
    p.add_argument("--samples-per-step", type=int, default=4)
    p.add_argument("--lr", type=float, default=3e-5)
    p.add_argument("--sigma-pos", type=float, default=500.0,
                   help="Gaussian noise std (real-coord units)")
    p.add_argument("--hidden", type=int, default=256)
    p.add_argument("--layers", type=int, default=8)
    p.add_argument("--dropout", type=float, default=0.0)
    p.add_argument("--device", default="cpu")
    p.add_argument("--rigid", action="store_true", default=True,
                   help="evaluate via rigid reroute (fast); else full post-pass")
    p.add_argument("--reward-fullpass", action="store_true", default=False,
                   help="use FULL post-pass cross as reward (slow ~130s/sample "
                        "but matches what user sees). Overrides --rigid.")
    p.add_argument("--fullpass-every", type=int, default=0,
                   help="run full post-pass eval (slow, ~130s) every N steps "
                        "for logging only (ignored if --reward-fullpass)")
    p.add_argument("--seed", type=int, default=0)
    args = p.parse_args()

    torch.manual_seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="reinforce-"))
    print(f"workdir: {work}")

    # Load captain baseline + build data (same as apply_distill_real)
    layout = json.loads(CAPTAIN_BASELINE.read_text())
    expert = json.loads(EXPERT_LAYOUT.read_text())
    data, ids, pos_mean, pos_std = apply_distill_real.build_data_from_layout(
        layout, edges_override=None, expert_layout=expert
    )
    n = data.x.shape[0]
    print(f"Captain: N={n} E={data.edge_index.shape[1]//2}")
    print(f"pos_mean={pos_mean.tolist()} pos_std={pos_std.item():.1f}")

    device = torch.device(args.device)
    model = train_distill.DistillGAT(
        node_feat_dim=10, hidden=args.hidden,
        num_layers=args.layers, dropout=args.dropout,
    ).to(device)
    sd = torch.load(args.init_from, map_location=device, weights_only=True)
    model.load_state_dict(sd)
    model.train()
    print(f"loaded init from {args.init_from}")

    optim = torch.optim.AdamW(model.parameters(), lr=args.lr)

    data = data.to(device)
    # Sigma in normalized space
    sigma_norm = args.sigma_pos / pos_std.item()
    print(f"sigma_pos={args.sigma_pos} (norm={sigma_norm:.4f})")

    # Baseline (initial) eval — use same mode as training reward
    eval_rigid_init = not args.reward_fullpass
    with torch.no_grad():
        mu = model(data)
    mu_real = mu.cpu() * pos_std + pos_mean
    init_cross, init_bbox = eval_real_cross(mu_real, ids, work, eval_rigid_init)
    mode = "FULL post-pass" if args.reward_fullpass else "rigid"
    print(f"init ({mode}): cross={init_cross} bbox={init_bbox:.2f}B")
    best_cross = init_cross
    baseline_reward = -float(init_cross)

    torch.save(model.state_dict(), args.ckpt)
    best_path = args.ckpt.with_name(args.ckpt.stem + "-best.pt")

    t0 = time.time()
    for step in range(args.steps):
        optim.zero_grad()
        mu = model(data)
        # Sample N candidates: a = (μ + σ·ε).detach() — actions are fixed
        # tensors; gradient only flows through μ in log_prob below.
        eps = torch.randn(args.samples_per_step, n, 2, device=device)
        samples = (mu.unsqueeze(0) + sigma_norm * eps).detach()  # [K, n, 2]
        # log π(a|s) = -0.5 * ||(a-μ)/σ||²; gradient w.r.t. μ is (a-μ)/σ².
        log_probs = -0.5 * (((samples - mu.unsqueeze(0)) / sigma_norm) ** 2
                            ).sum(dim=(1, 2))  # [K]

        # Evaluate each sample's real cross
        rewards = []
        bboxes = []
        # If --reward-fullpass, use full post-pass cross (real signal but slow);
        # otherwise rigid (fast but loose proxy)
        eval_rigid = not args.reward_fullpass
        for k in range(args.samples_per_step):
            sample_real = (samples[k].detach().cpu()
                           * pos_std + pos_mean)
            try:
                c, b = eval_real_cross(sample_real, ids, work, eval_rigid)
            except Exception as e:
                print(f"  eval failed for sample {k}: {e}")
                c, b = 999999, 0
            rewards.append(-c)
            bboxes.append(b)
            if c < best_cross:
                best_cross = c
                # Save the current model state (since this sample came
                # from its distribution; sampling near-mu is a search
                # along the policy's own variance).
                torch.save(model.state_dict(), best_path)

        rewards_t = torch.tensor(rewards, dtype=torch.float32, device=device)
        # Advantage = reward - moving baseline (variance reduction)
        advantage = rewards_t - baseline_reward
        # REINFORCE loss (gradient ascent on E[advantage * log_prob])
        loss = -(advantage * log_probs).mean()
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        optim.step()

        # Update baseline (EMA)
        baseline_reward = 0.9 * baseline_reward + 0.1 * rewards_t.mean().item()

        elapsed = time.time() - t0
        mean_cross = -rewards_t.mean().item()
        min_sample_cross = -rewards_t.max().item()
        print(f"step {step+1:3d}/{args.steps}  "
              f"mean_cross={mean_cross:.0f}  "
              f"min_sample={min_sample_cross:.0f}  "
              f"best={best_cross}  loss={loss.item():+.2f}  "
              f"({elapsed:.0f}s)")

        # Save current ckpt every 10 steps
        if (step + 1) % 10 == 0:
            torch.save(model.state_dict(), args.ckpt)
        # Periodic full-pass eval (slow but matches what user sees)
        if args.fullpass_every > 0 and (step + 1) % args.fullpass_every == 0:
            with torch.no_grad():
                mu_eval = model(data)
            mu_real = mu_eval.cpu() * pos_std + pos_mean
            try:
                fc, fb = eval_real_cross(mu_real, ids, work, rigid=False)
                print(f"  [FULL-PASS eval] cross={fc} bbox={fb:.2f}B")
            except Exception as e:
                print(f"  [FULL-PASS eval] failed: {e}")

    torch.save(model.state_dict(), args.ckpt)
    elapsed = time.time() - t0
    print(f"\nDone in {elapsed:.0f}s. Best real cross = {best_cross} "
          f"(init was {init_cross})")
    print(f"  current ckpt: {args.ckpt}")
    print(f"  best ckpt:    {best_path}")


if __name__ == "__main__":
    main()
