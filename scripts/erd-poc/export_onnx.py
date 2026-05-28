#!/usr/bin/env python3
"""ONNX export PoC for DistillGAT.

Goals:
  1. Wrap DistillGAT so it takes raw tensors (no PyG Data object).
  2. Export to ONNX with dynamic axes (variable node/edge count).
  3. Verify ONNX inference output matches PyTorch.

Usage:
  python scripts/erd-poc/export_onnx.py \\
    --ckpt data/erd-poc/checkpoints/v12-general-best.pt \\
    --out data/erd-poc/checkpoints/v12-general-best.onnx
"""

import argparse
import importlib.util
import json
from pathlib import Path

import torch
import torch.nn as nn

ROOT = Path(__file__).resolve().parents[2]
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


class TensorWrapper(nn.Module):
    """Wraps DistillGAT to accept raw tensors instead of PyG Data."""
    def __init__(self, model: train_distill.DistillGAT):
        super().__init__()
        self.model = model

    def forward(self, x, app_idx, baseline, edge_index, edge_attr):
        from types import SimpleNamespace
        data = SimpleNamespace(
            x=x, app_idx=app_idx, baseline=baseline,
            edge_index=edge_index, edge_attr=edge_attr,
        )
        return self.model(data)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--ckpt", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--hidden", type=int, default=256)
    p.add_argument("--layers", type=int, default=8)
    p.add_argument("--dropout", type=float, default=0.0)
    p.add_argument("--opset", type=int, default=17)
    args = p.parse_args()

    print(f"loading {args.ckpt} (hidden={args.hidden} layers={args.layers})")
    model = train_distill.DistillGAT(
        node_feat_dim=10, hidden=args.hidden,
        num_layers=args.layers, dropout=args.dropout,
    )
    sd = torch.load(args.ckpt, map_location="cpu", weights_only=True)
    model.load_state_dict(sd)
    model.eval()

    # Build example inputs from captain
    layout = json.loads(CAPTAIN_BASELINE.read_text())
    expert = json.loads(EXPERT_LAYOUT.read_text())
    data, ids, pos_mean, pos_std = apply_distill_real.build_data_from_layout(
        layout, edges_override=None, expert_layout=expert,
    )
    print(f"example inputs: N={data.x.shape[0]} "
          f"E={data.edge_index.shape[1]} feat={data.x.shape[1]}")

    wrapper = TensorWrapper(model).eval()
    # Sanity: PyTorch reference output
    with torch.no_grad():
        ref_out = wrapper(
            data.x, data.app_idx, data.baseline,
            data.edge_index, data.edge_attr,
        )
    print(f"PyTorch output: shape={ref_out.shape} "
          f"mean={ref_out.mean().item():.4f}")

    # Export
    args.out.parent.mkdir(parents=True, exist_ok=True)
    print(f"exporting to {args.out} (opset={args.opset})")
    try:
        torch.onnx.export(
            wrapper,
            (data.x, data.app_idx, data.baseline,
             data.edge_index, data.edge_attr),
            str(args.out),
            input_names=["x", "app_idx", "baseline",
                         "edge_index", "edge_attr"],
            output_names=["positions"],
            dynamic_axes={
                "x": {0: "N"},
                "app_idx": {0: "N"},
                "baseline": {0: "N"},
                "edge_index": {1: "E"},
                "edge_attr": {0: "E"},
                "positions": {0: "N"},
            },
            opset_version=args.opset,
            do_constant_folding=True,
        )
        print(f"✓ ONNX export OK")
    except Exception as e:
        print(f"✗ ONNX export FAILED: {type(e).__name__}: {e}")
        return 1

    # Validate with onnxruntime
    try:
        import onnxruntime as ort  # type: ignore
    except ImportError:
        print("onnxruntime not installed — skipping verification")
        print(f"  install: pip install onnxruntime")
        return 0

    print(f"running onnxruntime inference...")
    sess = ort.InferenceSession(str(args.out),
                                 providers=["CPUExecutionProvider"])
    onnx_out = sess.run(["positions"], {
        "x": data.x.numpy(),
        "app_idx": data.app_idx.numpy(),
        "baseline": data.baseline.numpy(),
        "edge_index": data.edge_index.numpy(),
        "edge_attr": data.edge_attr.numpy(),
    })[0]
    onnx_tensor = torch.from_numpy(onnx_out)
    diff = (ref_out - onnx_tensor).abs()
    print(f"  max abs diff: {diff.max().item():.6f}")
    print(f"  mean abs diff: {diff.mean().item():.6f}")
    if diff.max().item() < 1e-3:
        print(f"✓ ONNX inference matches PyTorch (tolerance 1e-3)")
        return 0
    else:
        print(f"✗ ONNX inference DIVERGES from PyTorch")
        return 2


if __name__ == "__main__":
    import sys
    sys.exit(main() or 0)
