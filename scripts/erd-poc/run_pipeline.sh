#!/usr/bin/env bash
# Run the C++ binary on each synthetic graph to produce expert layouts.
# Usage: bash scripts/erd-poc/run_pipeline.sh [data/erd-poc/graphs] [data/erd-poc/layouts]

set -euo pipefail
cd "$(dirname "$0")/../.."

GRAPH_DIR="${1:-data/erd-poc/graphs}"
LAYOUT_DIR="${2:-data/erd-poc/layouts}"
BIN="bin/ogdf/darwin-arm64/django-erd-ogdf-layout"

mkdir -p "$LAYOUT_DIR"
TOTAL=$(ls -1 "$GRAPH_DIR" | wc -l | xargs)
DONE=0
FAILED=0
START=$(date +%s)

for g in "$GRAPH_DIR"/*/; do
  name=$(basename "$g")
  out="$LAYOUT_DIR/$name.json"
  err="$LAYOUT_DIR/$name.stderr.log"
  DONE=$((DONE+1))

  # Skip if already done.
  if [[ -s "$out" ]]; then
    echo "[$DONE/$TOTAL] $name (skip, already exists)"
    continue
  fi

  echo "[$DONE/$TOTAL] $name (start)"
  start_t=$(date +%s)
  if "$BIN" layout --mode hierarchical_barycenter \
       --nodes-file "$g/nodes.tsv" --edges-file "$g/edges.tsv" \
       --edge-routing straight --cluster-graph 1 \
       > "$out" 2> "$err"; then
    elapsed=$(($(date +%s) - start_t))
    bytes=$(wc -c < "$out" | xargs)
    cross=$(grep -oE "carrier-grouped [0-9]+" "$err" | head -1 | awk '{print $2}')
    echo "  done in ${elapsed}s, ${bytes}B, edgeCrossings=${cross:-?}"
  else
    FAILED=$((FAILED+1))
    echo "  FAILED, see $err"
    rm -f "$out"
  fi
done

ELAPSED=$(( $(date +%s) - START ))
echo
echo "Pipeline batch done: $DONE total, $FAILED failed, ${ELAPSED}s elapsed"
