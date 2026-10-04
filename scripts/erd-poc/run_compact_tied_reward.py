#!/usr/bin/env python3
"""Use the existing 18-weight reward learner with fixed shared endpoints."""
import argparse
import json
from pathlib import Path
import run_compact_patch_reward as base
from compact_patch_tied_policy import TiedPatchActor
from geometry_world_model import digest


def run(args):
    original=base.PatchActor; base.PatchActor=TiedPatchActor
    try: base.run(args)
    finally: base.PatchActor=original
    path=args.out/'report.json'; report=json.loads(path.read_text())
    report['kind']='compact-source-spacing-patch-tied-endpoint-native-reward-learning-v2'
    report['sourceCoincidentEndpointGroupsTiedInEveryForward']=True
    report['coordinateRepairsAfterNativeRejection']=0
    report['entrypoint']=str(Path(__file__))
    report['codeSha256']['compact_patch_tied_policy.py']=digest(Path(__file__).parent/'compact_patch_tied_policy.py')
    report['codeSha256'][Path(__file__).name]=digest(__file__)
    path.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--source-binding',type=Path,required=True); p.add_argument('--directory',type=Path,required=True)
    p.add_argument('--environment',type=Path,required=True); p.add_argument('--view',choices=['overview','individual'],required=True)
    p.add_argument('--out',type=Path,required=True); p.add_argument('--seed',type=int,default=107103)
    p.add_argument('--patch-size',type=int,default=8); p.add_argument('--root-rank',type=int,default=0)
    p.add_argument('--max-step',type=float,default=256.); p.add_argument('--sigma',type=float,default=.005)
    p.add_argument('--rate',type=float,default=.001); p.add_argument('--iterations',type=int,default=64)
    p.add_argument('--seconds',type=float,default=30.); run(p.parse_args())
