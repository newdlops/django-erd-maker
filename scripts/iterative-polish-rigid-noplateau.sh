#!/usr/bin/env bash
# Iterative cluster-rigid polish: each cycle = rigid polish → C++ re-route.
# Cluster internal layout preserved, only cluster translations + hub
# positions move. Compounds gains across cycles without compressing bbox.

set -euo pipefail
cd "$(dirname "$0")/.."

CYCLES="${CYCLES:-6}"
ITERS="${POLISH_ITERS:-3000}"
SHARP_START="${POLISH_SHARP_START:-5}"
SHARP_END="${POLISH_SHARP_END:-20}"

CURRENT="${PRE_JSON:-/tmp/layout-pre-ml-v2.json}"
BEST_JSON="/tmp/layout-rigid-best.json"
LOG_FILE="log.txt"

if [[ ! -f "$CURRENT" ]]; then
  echo "[err] $CURRENT not found." >&2
  exit 1
fi

PRESERVED=$(grep "OGDF layout inputs preserved at" "$LOG_FILE" \
  | tail -1 | sed -E 's/.*OGDF layout inputs preserved at ([^ ]+) .*/\1/')
NODES="$PRESERVED/nodes.tsv"
EDGES="$PRESERVED/edges.tsv"
if [[ ! -f "$NODES" || ! -f "$EDGES" ]]; then
  echo "[err] preserved TSV missing" >&2
  exit 1
fi

BIN="bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
BASELINE=$(jq -r '.engineMetadata.edgeCrossings' "$CURRENT")
echo "Reading baseline... $BASELINE"

BEST_CROSS=$BASELINE
cp "$CURRENT" "$BEST_JSON"

echo
printf "%-8s  %-10s  %-10s  %s\n" "cycle" "polish_in" "rerouted" "delta_vs_base"
echo "  -----------------------------------------------"

for c in $(seq 1 "$CYCLES"); do
  IN_JSON="$CURRENT"
  RIGID_JSON="/tmp/rigid-cycle${c}.json"
  TSV_PATH="$RIGID_JSON.positions.tsv"
  OUT_JSON="/tmp/rigid-routed-${c}.json"
  IN_CROSS=$(jq -r '.engineMetadata.edgeCrossings' "$IN_JSON")

  EXTRA_FLAGS="${EXTRA_FLAGS:-}"
  .venv-ml/bin/python scripts/ml-layout-polish-rigid.py \
    --input "$IN_JSON" \
    --output "$RIGID_JSON" \
    --iters "$ITERS" --lr 100 \
    --sharpness "$SHARP_START" --sharpness-end "$SHARP_END" \
    --enable-rotation \
    --w-anchor-rot "${W_ROT:-1e-5}" \
    --w-anchor-trans "${W_TRANS:-1e-7}" \
    --w-anchor-hub "${W_HUB:-1e-7}" \
    $EXTRA_FLAGS \
    > "/tmp/rigid-cycle${c}-polish.log" 2>&1

  "$BIN" layout --mode hierarchical_barycenter \
    --nodes-file "$NODES" --edges-file "$EDGES" \
    --edge-routing straight --cluster-graph 1 \
    --positions-tsv "$TSV_PATH" \
    > "$OUT_JSON" 2>"/tmp/rigid-cycle${c}-cpp.log"

  OUT_CROSS=$(jq -r '.engineMetadata.edgeCrossings' "$OUT_JSON")
  DELTA=$((OUT_CROSS - BASELINE))
  PCT=$(awk -v d="$DELTA" -v b="$BASELINE" 'BEGIN{printf "%.1f%%", 100*d/b}')

  printf "%-8s  %-10s  %-10s  %+d (%s)\n" "$c" "$IN_CROSS" "$OUT_CROSS" "$DELTA" "$PCT"

  if (( OUT_CROSS < BEST_CROSS )); then
    BEST_CROSS=$OUT_CROSS
    cp "$OUT_JSON" "$BEST_JSON"
  fi

  CURRENT="$OUT_JSON"

  # Plateau check: only allow early stop after >= 5 cycles, since some
  # configurations show transient regression in early cycles before
  # compounding gains in later ones.
  if (( c >= 99 )); then
    PREV=$(jq -r '.engineMetadata.edgeCrossings' "/tmp/rigid-routed-$((c-2)).json")
    if (( OUT_CROSS >= PREV - 5 )); then
      echo "  → plateau, stopping"
      break
    fi
  fi
done

echo
DELTA=$((BEST_CROSS - BASELINE))
PCT=$(awk -v d="$DELTA" -v b="$BASELINE" 'BEGIN{printf "%.1f%%", 100*d/b}')
echo "Best: $BEST_CROSS (Δ $DELTA, $PCT)  → $BEST_JSON"
echo "Load: cp $BEST_JSON /tmp/layout-post-ml.json && reload IDE"
