#!/usr/bin/env bash
set -euo pipefail

cd /Users/lky/project/django-erd-maker

LOG=/tmp/v34-under1000-carrier-overlap-repair.bg.log
PID=/tmp/v34-under1000-carrier-overlap-repair.bg.pid
PY=/Users/lky/project/django-erd-maker/.venv-ml/bin/python

echo "$$" > "$PID"
exec > "$LOG" 2>&1

echo "started $(date '+%Y-%m-%d %H:%M:%S') pid=$$"
exec "$PY" -u scripts/erd-poc/v34_move_search.py \
  --layout data/erd-poc/layouts/real-main.json \
  --positions data/erd-poc/v34-under1000/under1000.tsv \
  --out-tsv /tmp/v34-under1000-carrier-overlap-repair.tsv \
  --rounds 120 \
  --top-nodes 0 \
  --top-edges 0 \
  --top-clusters 80 \
  --steps 150,300,800,1600,3200 \
  --anchor-radii 150,300,600,1000,1800,3000 \
  --swap-pairs 0 \
  --cluster-swap-pairs 500 \
  --overlap-candidates 500 \
  --loose-pack-groups 50 \
  --loose-pack-rotations 8 \
  --loose-pack-spacing-scale 2.2 \
  --loose-anchor-nodes 200 \
  --loose-anchor-radii 150,300,600,1000,1800,3000,5000 \
  --group-anchor-groups 220 \
  --group-anchor-max-size 100 \
  --group-anchor-radii 0,150,300,600,1000,1800,3000,5000,8000 \
  --component-anchor-components 80 \
  --component-anchor-max-group-size 8 \
  --component-anchor-max-total-nodes 80 \
  --component-anchor-radii 0,300,600,1000,1800,3000,5000,8000 \
  --bundle-orbit-groups 32 \
  --bundle-orbit-rotations 16 \
  --bundle-orbit-radius-scales 0.7,1.0,1.4 \
  --count-bundle-nodes \
  --overlap-margin 16 \
  --overlap-weight 1000 \
  --bbox-weight 800 \
  --bbox-target-b 4.0 \
  --final-max-candidates 18000 \
  --min-gain 1 \
  --carrier-cross \
  --measure-rigid
