"""Reuse predetermined trained NN heads for global shared interior anchors."""
import argparse
import json
from pathlib import Path
import run_compact_source_port_transfer as base
from compact_shared_anchor_policy import SharedAnchorPatchActor
from geometry_world_model import digest


def run(args):
    assert args.view == 'individual'
    original = base.SourcePortPatchActor
    base.SourcePortPatchActor = SharedAnchorPatchActor
    try:
        base.run(args)
    finally:
        base.SourcePortPatchActor = original
    path = args.out / 'report.json'
    report = json.loads(path.read_text())
    report['kind'] = 'trained-compact-head-shared-anchor-transfer-v1'
    report['endpointOffsetsAlwaysZero'] = False
    report['commonInteriorAnchorPerOriginalCard'] = True
    report['globalAnchorPredictionsUseTheExistingSharedTrainedHead'] = True
    report['coordinateRepairsAfterNativeRejection'] = 0
    for name in ('compact_shared_anchor_policy.py', Path(__file__).name):
        report['codeSha256'][name] = digest(Path(__file__).parent / name)
    path.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--parent', type=Path, required=True)
    p.add_argument('--source-binding', type=Path, required=True)
    p.add_argument('--candidate', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True)
    p.add_argument('--view', choices=['individual'], required=True)
    p.add_argument('--out', type=Path, required=True)
    run(p.parse_args())
