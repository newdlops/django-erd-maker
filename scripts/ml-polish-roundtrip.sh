#!/usr/bin/env bash
# ML polish round-trip helper.
#
# Workflow:
#   1. First IDE F5 run with DJERD_LAYOUT_OUTPUT_FILE / DJANGO_ERD_PRESERVE_
#      LAYOUT_INPUTS=1 set in launch.json saves:
#        - C++ output JSON to /tmp/layout-pre-ml.json
#        - nodes.tsv / edges.tsv to /tmp/django-erd-ogdf-<mode>-XXXX/
#   2. Run THIS script:
#        bash scripts/ml-polish-roundtrip.sh
#      It locates the preserved nodes/edges TSV from the latest extension
#      log, runs Python polish, then C++ re-route → /tmp/layout-post-ml.json
#   3. Second IDE F5 run loads /tmp/layout-post-ml.json automatically.
#
# Override the inputs:
#   --pre-json <path>   default /tmp/layout-pre-ml.json
#   --post-json <path>  default /tmp/layout-post-ml.json
#   --nodes <path> --edges <path>  bypass log-scrape

set -euo pipefail
cd "$(dirname "$0")/.."

PRE_JSON="/tmp/layout-pre-ml.json"
POST_JSON="/tmp/layout-post-ml.json"
NODES_FILE=""
EDGES_FILE=""

while (( $# > 0 )); do
  case "$1" in
    --pre-json)  PRE_JSON="$2"; shift 2;;
    --post-json) POST_JSON="$2"; shift 2;;
    --nodes)     NODES_FILE="$2"; shift 2;;
    --edges)     EDGES_FILE="$2"; shift 2;;
    -h|--help)
      sed -n '2,/^$/p' "$0" | sed 's/^# //; s/^#//'; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 1;;
  esac
done

if [[ ! -f "$PRE_JSON" ]]; then
  echo "[err] $PRE_JSON not found. Run IDE F5 first." >&2
  exit 1
fi

# Locate nodes.tsv / edges.tsv.
if [[ -z "$NODES_FILE" || -z "$EDGES_FILE" ]]; then
  # Latest preserved directory from log.txt.
  LOG_FILE="log.txt"
  if [[ ! -f "$LOG_FILE" ]]; then
    echo "[err] log.txt not found and --nodes/--edges not given." >&2
    exit 1
  fi
  # Two log message formats supported:
  #   "OGDF layout inputs preserved at <dir> ..." (env-var preserve, success path)
  #   "OGDF layout input preserved for debugging ... directory=<dir> ..." (failure path)
  PRESERVED_DIR=$(grep "OGDF layout inputs preserved at" "$LOG_FILE" \
    | tail -1 \
    | sed -E 's/.*OGDF layout inputs preserved at ([^ ]+) .*/\1/')
  if [[ -z "$PRESERVED_DIR" ]]; then
    PRESERVED_DIR=$(grep "OGDF layout input preserved" "$LOG_FILE" \
      | tail -1 \
      | grep -oE "directory=[^ ]+" \
      | sed 's/directory=//')
  fi
  if [[ -z "$PRESERVED_DIR" ]]; then
    echo "[err] No preserved-input log line in log.txt." >&2
    echo "      Set DJANGO_ERD_PRESERVE_LAYOUT_INPUTS=1 in launch.json env" >&2
    echo "      and run IDE F5 once before this script." >&2
    exit 1
  fi
  NODES_FILE="$PRESERVED_DIR/nodes.tsv"
  EDGES_FILE="$PRESERVED_DIR/edges.tsv"
fi

if [[ ! -f "$NODES_FILE" || ! -f "$EDGES_FILE" ]]; then
  echo "[err] nodes/edges TSV missing: $NODES_FILE / $EDGES_FILE" >&2
  exit 1
fi

echo "[roundtrip] pre-json=$PRE_JSON"
echo "[roundtrip] nodes=$NODES_FILE"
echo "[roundtrip] edges=$EDGES_FILE"

# Step A: Python polish (strengthened: multi-restart, sharpness anneal,
#          longer iter budget, mild edge-length penalty).
POLISH_JSON="/tmp/layout-polished.json"
ITERS="${POLISH_ITERS:-3000}"
RESTARTS="${POLISH_RESTARTS:-3}"
SHARP_START="${POLISH_SHARP_START:-5}"
SHARP_END="${POLISH_SHARP_END:-20}"
echo
echo "==> Step A: Python polish ($(date +%H:%M:%S))"
echo "    iters=$ITERS restarts=$RESTARTS sharpness=$SHARP_START→$SHARP_END"
.venv-ml/bin/python scripts/ml-layout-polish.py \
  --input "$PRE_JSON" \
  --output "$POLISH_JSON" \
  --iters "$ITERS" --lr 80 \
  --sharpness "$SHARP_START" --sharpness-end "$SHARP_END" \
  --num-restarts "$RESTARTS" --jitter-std 250 \
  --w-anchor 2e-7 --w-edge-len 0.001 \
  2>&1 | grep -E "edges=|carriers|initial|step.*hard_cross|run [0-9]+ carrier|best run|final|delta:" | tail -30

# Step B: C++ re-route with --positions-tsv.
BIN="bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
POSITIONS_TSV="$POLISH_JSON.positions.tsv"
echo
echo "==> Step B: C++ re-route with ML positions ($(date +%H:%M:%S))"
"$BIN" layout --mode hierarchical_barycenter \
  --nodes-file "$NODES_FILE" --edges-file "$EDGES_FILE" \
  --edge-routing straight --cluster-graph 1 \
  --positions-tsv "$POSITIONS_TSV" \
  > "$POST_JSON" 2>/tmp/ml-roundtrip-cpp-stderr.log

echo "==> Done ($(date +%H:%M:%S))"
echo
echo "[roundtrip] post-json=$POST_JSON  size=$(wc -c < "$POST_JSON")"
grep "carrier-cross\|edgeCrossings\|stuck-leaf-2d\|hot-region-sa\|ml-positions" /tmp/ml-roundtrip-cpp-stderr.log | tail -8
echo
echo "Now F5 in VS Code → IDE will load polished layout from $POST_JSON"
