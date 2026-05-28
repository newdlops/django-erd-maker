#!/bin/bash
# Multi-project comparison of stress majorization post-pass.
#
# Usage:
#   scripts/compare-stress-postpass.sh <nodes.tsv> <edges.tsv> [iters]
#
# Runs the OGDF binary twice (iters=0 and iters=N, default 20) on the
# same nodes/edges TSV pair, then prints a side-by-side metric table.
# Used to validate whether the Captain-tuned stress=20 default
# generalizes to other Django projects.
set -euo pipefail

NODES="${1:?nodes.tsv path required}"
EDGES="${2:?edges.tsv path required}"
ITERS="${3:-20}"

BIN="$(dirname "$0")/../bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT

echo "Running OGDF on $(basename "$NODES") ..."
echo ""
DJERD_STRESS_POST_PASS_ITERS=0 "$BIN" layout \
    --mode fmmm --nodes-file "$NODES" --edges-file "$EDGES" \
    --edge-routing straight --cluster-graph 1 \
    > "$TMP/no-stress.json" 2> "$TMP/no-stress.err"

DJERD_STRESS_POST_PASS_ITERS="$ITERS" "$BIN" layout \
    --mode fmmm --nodes-file "$NODES" --edges-file "$EDGES" \
    --edge-routing straight --cluster-graph 1 \
    > "$TMP/with-stress.json" 2> "$TMP/with-stress.err"

python3 - "$TMP/no-stress.json" "$TMP/with-stress.json" "$ITERS" <<'PYEOF'
import json, sys
b = json.load(open(sys.argv[1]))['engineMetadata']
s = json.load(open(sys.argv[2]))['engineMetadata']
iters = sys.argv[3]
print(f"{'metric':24s} {'baseline (0)':>14s} {'stress (' + iters + ')':>14s} {'delta':>20s}")
print("-" * 75)
keys = [
    ('visualCrossings','visualCross'),
    ('edgeCrossings','edgeCross'),
    ('edgeNodeIntersections','edgeNode'),
    ('boundingBoxArea','bbox (1e9)'),
    ('aspectRatio','aspect'),
    ('stressScore','stress'),
    ('compositeQuality','composite'),
    ('subCleanQuality','sub_clean'),
    ('subSeverityQuality','sub_severity'),
    ('subCompactQuality','sub_compact'),
    ('cleanEdgeRatio','cleanEdgeRatio'),
    ('crossingsPerEdgeP90','xPerEdgeP90'),
]
better = worse = same = 0
prefer_high = {'compositeQuality','subCleanQuality','subSeverityQuality',
               'subCompactQuality','subStressQuality','subAngleQuality',
               'subUniformQuality','subSpreadQuality','cleanEdgeRatio',
               'nodeAreaCoverage'}
for key, name in keys:
    bv = b[key]; sv = s[key]
    if key == 'boundingBoxArea':
        bv, sv = bv/1e9, sv/1e9
    if isinstance(bv, float):
        delta = sv - bv
        pct = delta / bv * 100 if abs(bv) > 1e-6 else 0.0
        is_better = delta > 0 if key in prefer_high else delta < 0
        is_worse = delta < 0 if key in prefer_high else delta > 0
        if abs(pct) < 0.5: marker = '·'
        elif is_better: marker = '✓'; better += 1
        elif is_worse: marker = '✗'; worse += 1
        else: marker = '·'
        if abs(pct) < 0.5 and abs(delta) < 0.001: same += 1
        print(f"{name:24s} {bv:>14.4f} {sv:>14.4f}   ({delta:+.4f}, {pct:+.1f}%) {marker}")
    else:
        delta = sv - bv
        pct = delta / bv * 100 if bv > 0 else 0.0
        is_better = delta > 0 if key in prefer_high else delta < 0
        is_worse = delta < 0 if key in prefer_high else delta > 0
        marker = '✓' if is_better else ('✗' if is_worse else '·')
        if is_better: better += 1
        elif is_worse: worse += 1
        else: same += 1
        print(f"{name:24s} {bv:>14d} {sv:>14d}   ({delta:+d}, {pct:+.1f}%) {marker}")
print("-" * 75)
print(f"Verdict: {better} better, {worse} worse, {same} unchanged of {len(keys)} metrics.")
PYEOF
PY_RC=$?

if [ "$PY_RC" -ne 0 ]; then
    echo "Python comparison script failed"
    exit "$PY_RC"
fi
