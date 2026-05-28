#!/usr/bin/env bash
set -euo pipefail

cd /Users/lky/project/django-erd-maker

LOG=/tmp/v34-under1000-raw-overlap-spread.bg.log
PID=/tmp/v34-under1000-raw-overlap-spread.bg.pid
OUT=/tmp/v34-under1000-raw-overlap-spread.tsv
PY=/Users/lky/project/django-erd-maker/.venv-ml/bin/python

echo "$$" > "$PID"
exec > "$LOG" 2>&1

echo "started $(date '+%Y-%m-%d %H:%M:%S') pid=$$"
"$PY" -u scripts/erd-poc/v34_move_search.py \
  --layout data/erd-poc/layouts/real-main.json \
  --positions data/erd-poc/v34-under1000/under1000.tsv \
  --out-tsv "$OUT" \
  --rounds 60 \
  --top-nodes 0 \
  --top-edges 0 \
  --top-clusters 0 \
  --steps 150,300 \
  --anchor-radii 150,300 \
  --swap-pairs 0 \
  --cluster-swap-pairs 0 \
  --overlap-candidates 300 \
  --overlap-spread-components 100 \
  --overlap-spread-spacing-scale 1.5 \
  --overlap-spread-rotations 8 \
  --overlap-spread-batch-sizes 8,16,32,64,100 \
  --count-bundle-nodes \
  --overlap-margin 16 \
  --overlap-weight 1000 \
  --bbox-weight 800 \
  --bbox-target-b 4.0 \
  --final-max-candidates 7000 \
  --min-gain 1 \
  --measure-rigid
status=$?
echo "finished $(date '+%Y-%m-%d %H:%M:%S') status=$status"
exit "$status"
