#!/usr/bin/env bash
set -euo pipefail

cd /Users/lky/project/django-erd-maker

PY=/Users/lky/project/django-erd-maker/.venv-ml/bin/python
OUT_DIR=data/erd-poc/v35-move-scorer
mkdir -p "$OUT_DIR"

common=(
  --layout data/erd-poc/layouts/real-main.json
  --rounds 8
  --top-nodes 60
  --top-edges 60
  --top-clusters 70
  --steps 75,150,300,600,1000,1800,3000
  --anchor-radii 75,150,300,600,1000,1800,3000,5000
  --swap-pairs 80
  --cluster-swap-pairs 300
  --overlap-candidates 120
  --overlap-spread-components 60
  --overlap-spread-spacing-scale 1.4
  --overlap-spread-rotations 4
  --overlap-spread-batch-sizes 8,16,32
  --loose-pack-groups 30
  --loose-pack-rotations 4
  --loose-pack-spacing-scale 2.0
  --loose-anchor-nodes 120
  --loose-anchor-radii 150,300,600,1000,1800,3000,5000
  --group-anchor-groups 180
  --group-anchor-max-size 100
  --group-anchor-radii 0,150,300,600,1000,1800,3000,5000,8000
  --component-anchor-components 70
  --component-anchor-max-group-size 8
  --component-anchor-max-total-nodes 80
  --component-anchor-radii 0,300,600,1000,1800,3000,5000,8000
  --bundle-orbit-groups 28
  --bundle-orbit-rotations 8
  --bundle-orbit-radius-scales 0.8,1.0,1.3
  --count-bundle-nodes
  --overlap-margin 16
  --overlap-weight 5000
  --bbox-weight 800
  --bbox-target-b 4.0
  --final-max-candidates 4000
  --min-gain 1
)

"$PY" -u scripts/erd-poc/v34_move_search.py "${common[@]}" \
  --positions data/erd-poc/v34-under1000/under1000.tsv \
  --out-tsv "$OUT_DIR/under1000-overlap-path.tsv" \
  --log-jsonl "$OUT_DIR/under1000-overlap-path.jsonl"

"$PY" -u scripts/erd-poc/v34_move_search.py "${common[@]}" \
  --positions data/erd-poc/v34-under1000/groupanchor2.tsv \
  --out-tsv "$OUT_DIR/groupanchor2-path.tsv" \
  --log-jsonl "$OUT_DIR/groupanchor2-path.jsonl"

if [[ -f /tmp/v34-under1000-raw-overlap-spread.tsv ]]; then
  "$PY" -u scripts/erd-poc/v34_move_search.py "${common[@]}" \
    --positions /tmp/v34-under1000-raw-overlap-spread.tsv \
    --out-tsv "$OUT_DIR/zero-overlap-path.tsv" \
    --log-jsonl "$OUT_DIR/zero-overlap-path.jsonl"
fi

echo "wrote $OUT_DIR/*.jsonl"
