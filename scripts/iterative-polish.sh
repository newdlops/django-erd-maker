#!/usr/bin/env bash
# Iterative polish loop: ml-polish → C++ re-route → ml-polish → C++ re-route ...
# Each cycle compounds gains. Tracks edgeCrossings per cycle. Saves best.
#
# Pre-requisites:
#   - /tmp/layout-pre-ml.json present (initial C++ baseline)
#   - log.txt has "OGDF layout inputs preserved at <dir>" (for nodes/edges TSV)
#
# Usage:
#   bash scripts/iterative-polish.sh                # default 8 cycles
#   CYCLES=15 bash scripts/iterative-polish.sh      # custom

set -euo pipefail
cd "$(dirname "$0")/.."

CYCLES="${CYCLES:-8}"
ITERS="${POLISH_ITERS:-3000}"
RESTARTS="${POLISH_RESTARTS:-1}"
SHARP_START="${POLISH_SHARP_START:-5}"
SHARP_END="${POLISH_SHARP_END:-20}"

CURRENT="/tmp/layout-pre-ml.json"
BEST_JSON="/tmp/layout-iterative-best.json"
BEST_TSV="/tmp/layout-iterative-best.positions.tsv"
LOG_FILE="log.txt"

if [[ ! -f "$CURRENT" ]]; then
  echo "[err] $CURRENT not found. Run IDE F5 first." >&2
  exit 1
fi

# Find preserved nodes/edges TSV from log.
PRESERVED=$(grep "OGDF layout inputs preserved at" "$LOG_FILE" \
  | tail -1 | sed -E 's/.*OGDF layout inputs preserved at ([^ ]+) .*/\1/')
NODES="$PRESERVED/nodes.tsv"
EDGES="$PRESERVED/edges.tsv"
if [[ ! -f "$NODES" || ! -f "$EDGES" ]]; then
  echo "[err] preserved TSV missing: $NODES / $EDGES" >&2
  exit 1
fi

BIN="bin/ogdf/darwin-arm64/django-erd-ogdf-layout"

# Get baseline crossings.
echo "Reading baseline..."
BASELINE=$(jq -r '.engineMetadata.edgeCrossings' "$CURRENT")
echo "  baseline edgeCrossings = $BASELINE"

BEST_CROSS=$BASELINE
cp "$CURRENT" "$BEST_JSON"

echo
printf "%-8s  %-10s  %-10s  %s\n" "cycle" "polish_in" "rerouted" "delta_vs_base"
echo "  -----------------------------------------------"

for c in $(seq 1 "$CYCLES"); do
  IN_JSON="$CURRENT"
  POLISH_JSON="/tmp/cycle${c}-polished.json"
  TSV_PATH="$POLISH_JSON.positions.tsv"
  OUT_JSON="/tmp/cycle${c}-routed.json"

  IN_CROSS=$(jq -r '.engineMetadata.edgeCrossings' "$IN_JSON")

  # Polish step.
  # w_edge_len=0: drop contraction pressure (caused 70% bbox compression
  # over multi-cycle iterative). w_cluster_spread=10: penalize per-cluster
  # contraction so members maintain baseline spread (room to resolve cross).
  .venv-ml/bin/python scripts/ml-layout-polish.py \
    --input "$IN_JSON" \
    --output "$POLISH_JSON" \
    --iters "$ITERS" --lr 80 \
    --sharpness "$SHARP_START" --sharpness-end "$SHARP_END" \
    --num-restarts "$RESTARTS" \
    --w-anchor 2e-7 --w-edge-len 0 --w-cluster-spread 10 \
    > "/tmp/cycle${c}-polish.log" 2>&1

  # C++ re-route step.
  "$BIN" layout --mode hierarchical_barycenter \
    --nodes-file "$NODES" --edges-file "$EDGES" \
    --edge-routing straight --cluster-graph 1 \
    --positions-tsv "$TSV_PATH" \
    > "$OUT_JSON" 2>"/tmp/cycle${c}-cpp.log"

  OUT_CROSS=$(jq -r '.engineMetadata.edgeCrossings' "$OUT_JSON")
  DELTA=$((OUT_CROSS - BASELINE))
  PCT=$(awk -v d="$DELTA" -v b="$BASELINE" 'BEGIN{printf "%.1f%%", 100*d/b}')

  printf "%-8s  %-10s  %-10s  %+d (%s)\n" "$c" "$IN_CROSS" "$OUT_CROSS" "$DELTA" "$PCT"

  if (( OUT_CROSS < BEST_CROSS )); then
    BEST_CROSS=$OUT_CROSS
    cp "$OUT_JSON" "$BEST_JSON"
    cp "$TSV_PATH" "$BEST_TSV" 2>/dev/null || true
  fi

  # Next cycle takes this output as input.
  CURRENT="$OUT_JSON"

  # Early stop if metric stops improving for 2 cycles.
  if (( c >= 3 )); then
    PREV_CROSS=$(jq -r '.engineMetadata.edgeCrossings' "/tmp/cycle$((c-2))-routed.json")
    if (( OUT_CROSS >= PREV_CROSS - 5 )); then
      echo "  → plateau (Δ < 5 over 2 cycles), stopping early"
      break
    fi
  fi
done

echo
DELTA=$((BEST_CROSS - BASELINE))
PCT=$(awk -v d="$DELTA" -v b="$BASELINE" 'BEGIN{printf "%.1f%%", 100*d/b}')
echo "Best: $BEST_CROSS (Δ $DELTA, $PCT)  → $BEST_JSON"
echo "To load in IDE: cp $BEST_JSON /tmp/layout-post-ml.json && reload"
