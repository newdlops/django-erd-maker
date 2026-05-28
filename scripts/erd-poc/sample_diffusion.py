#!/usr/bin/env python3
"""Sample a layout from trained diffusion model.

Process:
  1. Load graph features (no positions needed — diffusion generates them)
  2. Sample x_T ~ N(0, I)
  3. For t = T-1 down to 0:
       ε̂ = model(x_t, t, graph)
       x_{t-1} = (x_t - ((1-α_t)/√(1-α_bar_t)) · ε̂) / √α_t  + σ_t · z
  4. Unnormalize x_0 using captain's expert pos_mean/pos_std
  5. Write as positions.tsv

Optional cross-loss guidance: at each step, add a small gradient
push from soft_cross_loss to bias the sample toward low-cross layouts.
"""

import argparse
import json
import math
import subprocess
import tempfile
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

import importlib.util
spec = importlib.util.spec_from_file_location(
    "train_diffusion", Path(__file__).parent / "train_diffusion.py"
)
tdf = importlib.util.module_from_spec(spec); spec.loader.exec_module(tdf)

spec2 = importlib.util.spec_from_file_location(
    "fast_cross_eval", Path(__file__).parent / "fast_cross_eval.py"
)
fce = importlib.util.module_from_spec(spec2); spec2.loader.exec_module(fce)

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
NODES_TSV = ROOT / "data/erd-poc/graphs/real-main/nodes.tsv"
EDGES_TSV = ROOT / "data/erd-poc/graphs/real-main/edges.tsv"


def soft_cross_loss(pos, edge_index_fwd, sharpness=8.0, sample_pairs=3000):
    """Differentiable approximation of straight-line edge crossings.
    Reused from train_distill — sigmoid-smoothed orientation test.
    pos: [N, 2] in real coordinates.
    edge_index_fwd: [2, E] forward edges only.
    """
    E = edge_index_fwd.shape[1]
    if E < 2:
        return torch.tensor(0.0, device=pos.device, requires_grad=True)
    K = min(sample_pairs, E * (E - 1) // 2)
    i_idx = torch.randint(0, E, (K,), device=pos.device)
    j_idx = torch.randint(0, E, (K,), device=pos.device)
    keep = i_idx != j_idx
    if keep.sum() == 0:
        return torch.tensor(0.0, device=pos.device, requires_grad=True)
    i_idx = i_idx[keep]; j_idx = j_idx[keep]
    si, ti = edge_index_fwd[0][i_idx], edge_index_fwd[1][i_idx]
    sj, tj = edge_index_fwd[0][j_idx], edge_index_fwd[1][j_idx]
    keep = (si != sj) & (si != tj) & (ti != sj) & (ti != tj)
    if keep.sum() == 0:
        return torch.tensor(0.0, device=pos.device, requires_grad=True)
    i_idx = i_idx[keep]; j_idx = j_idx[keep]
    a1, b1 = pos[edge_index_fwd[0][i_idx]], pos[edge_index_fwd[1][i_idx]]
    a2, b2 = pos[edge_index_fwd[0][j_idx]], pos[edge_index_fwd[1][j_idx]]
    eps = 1.0
    abx = b1[:, 0] - a1[:, 0]; aby = b1[:, 1] - a1[:, 1]
    cdx = b2[:, 0] - a2[:, 0]; cdy = b2[:, 1] - a2[:, 1]
    ab_len = (abx * abx + aby * aby + eps).sqrt()
    cd_len = (cdx * cdx + cdy * cdy + eps).sqrt()
    ac_x, ac_y = a2[:, 0] - a1[:, 0], a2[:, 1] - a1[:, 1]
    ad_x, ad_y = b2[:, 0] - a1[:, 0], b2[:, 1] - a1[:, 1]
    cb_x, cb_y = b1[:, 0] - a2[:, 0], b1[:, 1] - a2[:, 1]
    ac_len = (ac_x * ac_x + ac_y * ac_y + eps).sqrt()
    ad_len = (ad_x * ad_x + ad_y * ad_y + eps).sqrt()
    cb_len = (cb_x * cb_x + cb_y * cb_y + eps).sqrt()
    c1 = (abx * ac_y - aby * ac_x) / (ab_len * ac_len)
    c2 = (abx * ad_y - aby * ad_x) / (ab_len * ad_len)
    ca_x, ca_y = -ac_x, -ac_y
    c3 = (cdx * ca_y - cdy * ca_x) / (cd_len * ac_len)
    c4 = (cdx * cb_y - cdy * cb_x) / (cd_len * cb_len)
    s1 = torch.sigmoid(sharpness*c1) * torch.sigmoid(-sharpness*c2) \
       + torch.sigmoid(-sharpness*c1) * torch.sigmoid(sharpness*c2)
    s2 = torch.sigmoid(sharpness*c3) * torch.sigmoid(-sharpness*c4) \
       + torch.sigmoid(-sharpness*c3) * torch.sigmoid(sharpness*c4)
    return (s1 * s2).sum()


def bbox_loss(pos):
    """Compactness penalty: mean squared deviation from centroid.
    Lower = more compact layout. Differentiable.
    """
    mean = pos.mean(dim=0, keepdim=True)
    return ((pos - mean) ** 2).sum(dim=1).mean()


def sample(model, data, diff, T, device,
           guidance_strength=0.0, bbox_guidance=0.0,
           guidance_start_t=None, pos_mean=None, pos_std=None, seed=0):
    """DDPM sampling with combined cross + bbox guidance.

    At each guided step we compute:
      • cross_grad: gradient of soft_cross_loss w.r.t. x_0_est (real coords)
      • bbox_grad:  gradient of bbox_loss w.r.t. x_0_est (real coords)
    Both are normalized to unit norm so weights have predictable meaning.
    Final push: guidance_strength * cross_grad + bbox_guidance * bbox_grad.

    guidance_start_t: only apply guidance for t < this value (let early
    denoising form rough structure first; refine late).
    """
    torch.manual_seed(seed)
    n = data["x"].shape[0]
    node_feat = data["x"].to(device)
    edge_index = data["edge_index"].to(device)
    edge_attr = data["edge_attr"].to(device)
    # Build forward-only edge index for cross loss
    if edge_index.shape[1] > 0:
        # Take every-other (the [::2] convention from train_distill)
        edge_fwd = edge_index[:, ::2]
    else:
        edge_fwd = edge_index
    use_guidance = guidance_strength > 0 or bbox_guidance > 0
    if guidance_start_t is None:
        guidance_start_t = T // 4  # last 25% of steps
    x_t = torch.randn(n, 2, device=device)
    model.eval()
    for t_idx in range(T - 1, -1, -1):
        t = torch.tensor([t_idx], device=device)
        with torch.no_grad():
            eps_hat = model(x_t, t, node_feat, edge_index, edge_attr)
        alpha = diff["alphas"][t_idx]
        alpha_bar = diff["alpha_bars"][t_idx]
        beta = diff["betas"][t_idx]
        sqrt_alpha_bar = diff["sqrt_alpha_bars"][t_idx]
        sqrt_one_minus_alpha_bar = diff["sqrt_one_minus_alpha_bars"][t_idx]

        # Combined cross + bbox guidance on estimated x_0
        if use_guidance and t_idx <= guidance_start_t \
                and pos_mean is not None and pos_std is not None:
            x_0_est = (x_t - sqrt_one_minus_alpha_bar * eps_hat) \
                      / sqrt_alpha_bar
            x_0_est = x_0_est.detach().requires_grad_(True)
            pm = pos_mean.to(device) if isinstance(pos_mean, torch.Tensor) \
                 else torch.tensor(pos_mean, device=device)
            x_real = x_0_est * pos_std + pm
            combined_grad = torch.zeros_like(x_0_est)
            if guidance_strength > 0:
                cross = soft_cross_loss(x_real, edge_fwd, sample_pairs=2000)
                g = torch.autograd.grad(cross, x_0_est, retain_graph=True)[0]
                combined_grad = combined_grad + guidance_strength * g
            if bbox_guidance > 0:
                # Scale bbox so it can balance cross. bbox_loss magnitude ~ pos_std²
                # in real coords; we want the gradient pull per step similar to
                # cross. Scale by 1/pos_std² to normalize.
                bb = bbox_loss(x_real) / (pos_std ** 2)
                g = torch.autograd.grad(bb, x_0_est)[0]
                combined_grad = combined_grad + bbox_guidance * g
            # Project gradient back through forward process to ε space
            eps_hat = eps_hat + sqrt_one_minus_alpha_bar * combined_grad
        mean = (x_t - (beta / sqrt_one_minus_alpha_bar) * eps_hat) \
               / torch.sqrt(alpha)
        if t_idx > 0:
            sigma = torch.sqrt(beta)
            z = torch.randn_like(x_t)
            x_t = mean + sigma * z
        else:
            x_t = mean
    return x_t


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--ckpt", type=Path, required=True)
    p.add_argument("--layout", type=Path, required=True,
                   help="layout JSON for graph features (positions ignored)")
    p.add_argument("--expert", type=Path,
                   default=ROOT / "data/erd-poc/expert-strong/real-main.json",
                   help="for pos_mean/pos_std normalization")
    p.add_argument("--out-tsv", type=Path, required=True)
    p.add_argument("--T", type=int, default=200)
    p.add_argument("--hidden", type=int, default=128)
    p.add_argument("--layers", type=int, default=4)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--n-samples", type=int, default=4,
                   help="generate N samples; keep best by fast cross")
    p.add_argument("--device", default="cpu")
    p.add_argument("--measure-rigid", action="store_true",
                   help="also run C++ rigid reroute on best sample")
    p.add_argument("--guidance", type=float, default=0.0,
                   help="cross-loss guidance strength (gradient norm, 0 = off)")
    p.add_argument("--bbox-guidance", type=float, default=0.0,
                   help="bbox-compactness guidance strength (gradient norm)")
    p.add_argument("--guidance-start-frac", type=float, default=0.25,
                   help="apply guidance for last frac of denoising steps")
    args = p.parse_args()

    device = torch.device(args.device)
    diff = tdf.get_diffusion_constants(args.T, device=device)

    layout = json.loads(args.layout.read_text())
    expert = json.loads(args.expert.read_text())
    data = tdf.build_graph_data(layout, expert_layout=expert)
    n = data["x"].shape[0]
    print(f"layout: N={n}, E={data['edge_index'].shape[1]//2}")
    print(f"pos_mean={data['pos_mean'].tolist()} pos_std={data['pos_std']:.1f}")

    # Build evaluator for fast cross
    id2 = {nd["modelId"]: i for i, nd in enumerate(layout["nodes"])}
    edges_for_eval = []
    for re in layout.get("routedEdges", []):
        s = id2.get(re.get("sourceModelId"))
        t = id2.get(re.get("targetModelId"))
        if s is None or t is None or s == t: continue
        edges_for_eval.append((s, t))
    edges_arr = np.array(edges_for_eval, dtype=np.int32)
    evaluator = fce.FastCrossEval(edges_arr, n)

    model = tdf.GraphDenoiser(node_feat_dim=10, hidden=args.hidden,
                                num_layers=args.layers,
                                edge_feat_dim=6).to(device)
    sd = torch.load(args.ckpt, map_location=device, weights_only=True)
    model.load_state_dict(sd)
    print(f"loaded {args.ckpt}")

    # Sample N times, keep best
    best_x = None
    best_cross = None
    pos_mean = data["pos_mean"].cpu()
    pos_std = data["pos_std"]
    guidance_start_t = int(args.T * args.guidance_start_frac)
    for k in range(args.n_samples):
        t0 = time.time()
        x_0_norm = sample(model, data, diff, args.T, device,
                          guidance_strength=args.guidance,
                          bbox_guidance=args.bbox_guidance,
                          guidance_start_t=guidance_start_t,
                          pos_mean=data["pos_mean"], pos_std=data["pos_std"],
                          seed=args.seed + k)
        elapsed = time.time() - t0
        x_real = x_0_norm.cpu() * pos_std + pos_mean
        cross = evaluator.count_crossings(x_real.numpy())
        # Bbox
        xs = x_real[:, 0]; ys = x_real[:, 1]
        bbox = float(((xs.max()-xs.min()) * (ys.max()-ys.min())).item() / 1e9)
        print(f"  sample {k+1}/{args.n_samples}: cross={cross} "
              f"bbox={bbox:.2f}B ({elapsed:.1f}s)")
        if best_cross is None or cross < best_cross:
            best_cross = cross
            best_x = x_real

    # Write best as TSV
    args.out_tsv.parent.mkdir(parents=True, exist_ok=True)
    with args.out_tsv.open("w") as f:
        for i, nd in enumerate(layout["nodes"]):
            x = best_x[i, 0].item()
            y = best_x[i, 1].item()
            f.write(f"{nd['modelId']}\t{x:.3f}\t{y:.3f}\n")
    print(f"\nBest: cross={best_cross}, wrote {args.out_tsv}")

    if args.measure_rigid:
        print(f"\nC++ rigid reroute:")
        r = subprocess.run(
            [str(BINARY), "layout",
             "--mode", "hierarchical_barycenter",
             "--nodes-file", str(NODES_TSV),
             "--edges-file", str(EDGES_TSV),
             "--edge-routing", "straight",
             "--cluster-graph", "1",
             "--positions-tsv", str(args.out_tsv),
             "--rigid-positions", "1"],
            capture_output=True, text=True,
        )
        if r.returncode == 0:
            d = json.loads(r.stdout)
            em = d["engineMetadata"]
            ndArr = d["nodes"]
            xs = [nd["position"]["x"] + nd["size"]["width"]/2 for nd in ndArr]
            ys = [nd["position"]["y"] + nd["size"]["height"]/2 for nd in ndArr]
            bbox = (max(xs)-min(xs)) * (max(ys)-min(ys)) / 1e9
            print(
                f"  rigid cross={em['edgeCrossings']} "
                f"overlap={em.get('nodeOverlaps', '?')} "
                f"bbox={bbox:.2f}B"
            )
        else:
            print(f"  binary failed: {r.stderr[:300]}")


if __name__ == "__main__":
    main()
