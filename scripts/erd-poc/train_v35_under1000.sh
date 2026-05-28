#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

.venv-ml/bin/python -u scripts/erd-poc/train_diffusion.py \
  --layouts data/erd-poc/captain-corpus-v35 \
  --ckpt data/erd-poc/checkpoints/v35-under1000.pt \
  --init-from data/erd-poc/checkpoints/v32-stronger.pt \
  --T 200 \
  --epochs 220 \
  --hidden 256 \
  --layers 6 \
  --lr 8e-5 \
  --upweight real-main-v34-under1000 \
  --upweight-factor 80 \
  --w-cross 0.35 \
  --cross-warmup 30 \
  --w-overlap 0.25 \
  --overlap-margin 50 \
  --overlap-mode subset \
  --overlap-sample-nodes 512 \
  --node-drop-prob 0.03 \
  --save-every 20
