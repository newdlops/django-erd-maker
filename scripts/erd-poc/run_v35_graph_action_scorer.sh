#!/usr/bin/env bash
set -euo pipefail

cd /Users/lky/project/django-erd-maker

PY=/Users/lky/project/django-erd-maker/.venv-ml/bin/python
DATA=data/erd-poc/v35-move-scorer/v35-action-dataset.npz
HOT700_DATA=data/erd-poc/v35-move-scorer/v35-action-dataset-hot700.npz
RANDOM700_DATA=data/erd-poc/v35-move-scorer/v35-action-dataset-random700.npz
CKPT=data/erd-poc/checkpoints/v35-graph-action-scorer.pt

starts=(
  --start under1000=data/erd-poc/v34-under1000/under1000.tsv
  --start groupanchor2=data/erd-poc/v34-under1000/groupanchor2.tsv
)
[[ -f /tmp/v34-under1000-raw-overlap-spread.tsv ]] && \
  starts+=(--start zero-overlap=/tmp/v34-under1000-raw-overlap-spread.tsv)

"$PY" -u scripts/erd-poc/build_v35_action_dataset.py \
  --out "$DATA" \
  "${starts[@]}" \
  --rounds 4 \
  --top-nodes 80 \
  --top-edges 100 \
  --top-clusters 90 \
  --steps 75,150,300,600,1000,1800,3000,5000 \
  --anchor-radii 75,150,300,600,1000,1800,3000,5000,8000 \
  --cross-pair-candidates 250 \
  --cross-pair-steps 75,150,300,600,1000,1800,3000,5000 \
  --cross-fan-edges 120 \
  --cross-fan-steps 75,150,300,600,1000,1800,3000,5000 \
  --cross-group-fan-groups 160 \
  --cross-group-fan-max-size 120 \
  --cross-group-fan-steps 75,150,300,600,1000,1800,3000,5000 \
  --cross-carrier-pair-candidates 0 \
  --cross-carrier-pair-max-size 120 \
  --cross-carrier-pair-steps 75,150,300,600,1000,1800,3000,5000 \
  --edge-node-relief-hits 100 \
  --edge-node-relief-max-group-size 120 \
  --edge-node-relief-steps 75,150,300,600,1000,1800,3000,5000 \
  --edge-node-precise-hits 220 \
  --edge-node-precise-neighborhood-max-nodes 64 \
  --edge-node-precise-pads 0,45,100,180,300 \
  --edge-node-clear-padding 32 \
  --edge-node-corridor-edges 90 \
  --edge-node-corridor-blockers 18 \
  --edge-node-corridor-pads 0,45,100,180,300 \
  --edge-node-force-hits 220 \
  --edge-node-force-nodes 160 \
  --edge-node-force-scales 0.25,0.45,0.7,1.0,1.35 \
  --edge-node-force-padding 55 \
  --cross-hotspot-bypass-hotspots 8 \
  --cross-hotspot-bypass-edges 8 \
  --cross-hotspot-bypass-max-size 80 \
  --cross-hotspot-bypass-cell-size 5000 \
  --cross-hotspot-bypass-steps 75,150,300,600,1000,1800,3000,5000 \
  --cross-partner-orbit-edges 0 \
  --cross-partner-radii 150,300,600,1000,1800,3000,5000,8000 \
  --cross-endpoint-swap-pairs 0 \
  --swap-pairs 160 \
  --cluster-swap-pairs 600 \
  --loose-pack-groups 40 \
  --loose-anchor-nodes 180 \
  --loose-anchor-radii 150,300,600,1000,1800,3000,5000,8000 \
  --group-anchor-groups 240 \
  --group-anchor-max-size 120 \
  --component-anchor-components 100 \
  --component-anchor-max-group-size 10 \
  --component-anchor-max-total-nodes 100 \
  --bundle-orbit-groups 40 \
  --final-max-candidates 4000 \
  --final-per-action-candidates 1000

"$PY" -u scripts/erd-poc/build_v35_action_dataset.py \
  --layout data/erd-poc/v35-move-scorer/subsets/captain-hot700.json \
  --start hot700=data/erd-poc/v35-move-scorer/subsets/captain-hot700.tsv \
  --out "$HOT700_DATA" \
  --rounds 4 \
  --top-nodes 80 \
  --top-edges 100 \
  --top-clusters 90 \
  --steps 75,150,300,600,1000,1800,3000,5000 \
  --anchor-radii 75,150,300,600,1000,1800,3000,5000,8000 \
  --cross-pair-candidates 250 \
  --cross-pair-steps 75,150,300,600,1000,1800,3000,5000 \
  --cross-fan-edges 120 \
  --cross-fan-steps 75,150,300,600,1000,1800,3000,5000 \
  --cross-group-fan-groups 160 \
  --cross-group-fan-max-size 120 \
  --cross-group-fan-steps 75,150,300,600,1000,1800,3000,5000 \
  --cross-carrier-pair-candidates 0 \
  --cross-carrier-pair-max-size 120 \
  --cross-carrier-pair-steps 75,150,300,600,1000,1800,3000,5000 \
  --edge-node-relief-hits 100 \
  --edge-node-relief-max-group-size 120 \
  --edge-node-relief-steps 75,150,300,600,1000,1800,3000,5000 \
  --edge-node-precise-hits 220 \
  --edge-node-precise-neighborhood-max-nodes 64 \
  --edge-node-precise-pads 0,45,100,180,300 \
  --edge-node-clear-padding 32 \
  --edge-node-corridor-edges 90 \
  --edge-node-corridor-blockers 18 \
  --edge-node-corridor-pads 0,45,100,180,300 \
  --edge-node-force-hits 220 \
  --edge-node-force-nodes 160 \
  --edge-node-force-scales 0.25,0.45,0.7,1.0,1.35 \
  --edge-node-force-padding 55 \
  --cross-hotspot-bypass-hotspots 8 \
  --cross-hotspot-bypass-edges 8 \
  --cross-hotspot-bypass-max-size 80 \
  --cross-hotspot-bypass-cell-size 5000 \
  --cross-hotspot-bypass-steps 75,150,300,600,1000,1800,3000,5000 \
  --cross-partner-orbit-edges 0 \
  --cross-partner-radii 150,300,600,1000,1800,3000,5000,8000 \
  --cross-endpoint-swap-pairs 0 \
  --swap-pairs 160 \
  --cluster-swap-pairs 600 \
  --loose-pack-groups 40 \
  --loose-anchor-nodes 180 \
  --loose-anchor-radii 150,300,600,1000,1800,3000,5000,8000 \
  --group-anchor-groups 240 \
  --group-anchor-max-size 120 \
  --component-anchor-components 100 \
  --component-anchor-max-group-size 10 \
  --component-anchor-max-total-nodes 100 \
  --bundle-orbit-groups 40 \
  --final-max-candidates 4000 \
  --final-per-action-candidates 1000

"$PY" -u scripts/erd-poc/build_v35_action_dataset.py \
  --layout data/erd-poc/v35-move-scorer/subsets/captain-random700-s0.json \
  --start random700=data/erd-poc/v35-move-scorer/subsets/captain-random700-s0.tsv \
  --out "$RANDOM700_DATA" \
  --rounds 4 \
  --top-nodes 80 \
  --top-edges 100 \
  --top-clusters 90 \
  --steps 75,150,300,600,1000,1800,3000,5000 \
  --anchor-radii 75,150,300,600,1000,1800,3000,5000,8000 \
  --cross-pair-candidates 250 \
  --cross-pair-steps 75,150,300,600,1000,1800,3000,5000 \
  --cross-fan-edges 120 \
  --cross-fan-steps 75,150,300,600,1000,1800,3000,5000 \
  --cross-group-fan-groups 160 \
  --cross-group-fan-max-size 120 \
  --cross-group-fan-steps 75,150,300,600,1000,1800,3000,5000 \
  --cross-carrier-pair-candidates 0 \
  --cross-carrier-pair-max-size 120 \
  --cross-carrier-pair-steps 75,150,300,600,1000,1800,3000,5000 \
  --edge-node-relief-hits 100 \
  --edge-node-relief-max-group-size 120 \
  --edge-node-relief-steps 75,150,300,600,1000,1800,3000,5000 \
  --edge-node-precise-hits 220 \
  --edge-node-precise-neighborhood-max-nodes 64 \
  --edge-node-precise-pads 0,45,100,180,300 \
  --edge-node-clear-padding 32 \
  --edge-node-corridor-edges 90 \
  --edge-node-corridor-blockers 18 \
  --edge-node-corridor-pads 0,45,100,180,300 \
  --edge-node-force-hits 220 \
  --edge-node-force-nodes 160 \
  --edge-node-force-scales 0.25,0.45,0.7,1.0,1.35 \
  --edge-node-force-padding 55 \
  --cross-hotspot-bypass-hotspots 8 \
  --cross-hotspot-bypass-edges 8 \
  --cross-hotspot-bypass-max-size 80 \
  --cross-hotspot-bypass-cell-size 5000 \
  --cross-hotspot-bypass-steps 75,150,300,600,1000,1800,3000,5000 \
  --cross-partner-orbit-edges 0 \
  --cross-partner-radii 150,300,600,1000,1800,3000,5000,8000 \
  --cross-endpoint-swap-pairs 0 \
  --swap-pairs 160 \
  --cluster-swap-pairs 600 \
  --loose-pack-groups 40 \
  --loose-anchor-nodes 180 \
  --loose-anchor-radii 150,300,600,1000,1800,3000,5000,8000 \
  --group-anchor-groups 240 \
  --group-anchor-max-size 120 \
  --component-anchor-components 100 \
  --component-anchor-max-group-size 10 \
  --component-anchor-max-total-nodes 100 \
  --bundle-orbit-groups 40 \
  --final-max-candidates 4000 \
  --final-per-action-candidates 1000

train_datasets=(--dataset "$DATA")
train_datasets+=(--dataset "$HOT700_DATA")
train_datasets+=(--dataset "$RANDOM700_DATA")

"$PY" -u scripts/erd-poc/train_v35_graph_action_scorer.py \
  "${train_datasets[@]}" \
  --ckpt "$CKPT" \
  --epochs 80 \
  --batch-size 2048 \
  --hidden 256 \
  --layers 3 \
  --dropout 0.05 \
  --lr 3e-4 \
  --val-frac 0.25 \
  --gain-scale 20 \
  --rank-weight 1.0 \
  --rank-temperature 50 \
  --accept-top-n 64 \
  --accept-min-gain 1 \
  --accept-regret 100 \
  --select-k 50
