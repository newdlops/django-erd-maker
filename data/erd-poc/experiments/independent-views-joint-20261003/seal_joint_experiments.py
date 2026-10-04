"""Exclusive immutable archive; stream-copy and verify every stored byte."""
import gzip
import hashlib
import json
from pathlib import Path
import shutil

SOURCE=Path('.tmp/visualcross-ml-150-750-20261003')
TARGET=Path('data/erd-poc/experiments/independent-views-joint-20261003')
assert not TARGET.exists(), 'archive already exists; do not overwrite'
TARGET.mkdir(parents=True)

def digest(path,compressed=False):
    result=hashlib.sha256()
    with (gzip.open(path,'rb') if compressed else path.open('rb')) as source:
        for block in iter(lambda:source.read(1024*1024),b''):result.update(block)
    return result.hexdigest()

records=[]
def copy(source,relative):
    assert source.is_file() and not source.is_symlink()
    compressed=source.suffix in ['.jsonl','.tsv'] or source.stat().st_size>131072 and source.suffix in ['.json','.log']
    if str(relative) in ['combined-best/candidate.layout.json','combined-best/audit.json','baseline-audit/candidate.layout.json']:
        compressed=False
    stored=Path(str(relative)+('.gz' if compressed else ''));destination=TARGET/stored
    destination.parent.mkdir(parents=True,exist_ok=True)
    sha=digest(source)
    if compressed:
        with source.open('rb') as input,destination.open('xb') as output:
            with gzip.GzipFile(filename='',mode='wb',fileobj=output,mtime=0,compresslevel=6) as archive:
                shutil.copyfileobj(input,archive,1024*1024)
    else:
        with source.open('rb') as input,destination.open('xb') as output:shutil.copyfileobj(input,output,1024*1024)
        destination.chmod(source.stat().st_mode&0o777)
    assert digest(destination,compressed)==sha
    assert digest(source)==sha, 'source changed while archiving'
    records.append({'source':str(source),'stored':str(stored),'sha256':sha,'bytes':source.stat().st_size,'gzip':compressed,'restoredHashVerified':True})

for source in sorted(SOURCE.rglob('*')):
    if source.is_file():copy(source,source.relative_to(SOURCE))
names=['run_memory_bounded.py','ml_component_environment.cpp','ml_joint_batch_environment.cpp','joint_layout_proxy.py',
       'run_joint_neural_layout.py','learn_card_policy.py','learned_global_replay.py','run_individual_policy_experiment.py',
       'run_learned_leaf_layout.py','apply_learned_components.cjs','compose_independent_views.cjs',
       'audit_individual_policy_experiment.cjs','audit_leaf_card_connections.cjs','export_learned_components.cjs',
       'constrained_dual_node_geometry.h','constrained_boundary_sweep.h','constrained_scene.h']
for name in names:copy(Path('scripts/erd-poc')/name,Path('code')/name)
for version in [10,11,15,16]:
    name=f'leaf-component-policy-v{version}.npz'
    copy(Path('data/erd-poc/checkpoints')/name,Path('dependencies')/name)
copy(Path('.tmp/visualcross-ml-targets-20261003/port-environment-v4'),Path('dependencies/port-environment-v4'))
stages=[];policy_actions=0;neural_batches=0
for directory in sorted(SOURCE.iterdir()):
    if not directory.is_dir():continue
    report_file=next((directory/name for name in ['workflow.audit.json','workflow.json'] if (directory/name).is_file()),None)
    if report_file is None:continue
    report=json.loads(report_file.read_text())
    stats=json.loads((directory/'learned.tsv.stats.json').read_text())
    policy=json.loads((directory/'learned.tsv.policy.json').read_text())
    joint=policy.get('modelKind')=='coordinated-displacement-network'
    if joint:neural_batches+=stats['policyActionsEvaluated']
    else:policy_actions+=stats['policyActionsEvaluated']
    stages.append({'name':directory.name,'jointNetwork':joint,'overviewOnly':stats['overviewOnly'],
                   'visual':stats['visual'],'individualVisual':stats['individualVisual'],
                   'evaluated':stats['policyActionsEvaluated'],'candidateSha256':report['candidateSha256']})
combined=json.loads((SOURCE/'combined-best/audit.json').read_text())
manifest={'scope':'ML experiments for overview <=150 and individual <=750','sourceDirectory':str(SOURCE),
          'overviewTarget':150,'individualTarget':750,'overviewVisual':combined['overviewVisual'],'individualVisual':combined['individualVisual'],
          'targetMet':combined['thresholdsMet'],'promoted':False,'combinedCandidateSha256':combined['candidateSha256'],
          'bestIndividualSource':'individual-joint-refine2','bestOverviewSource':'.tmp/visualcross-ml-targets-20261003/overview-final-moving1',
          'priorArchive':'data/erd-poc/experiments/independent-views-neutral-area-20261003/manifest.json',
          'policyActions':policy_actions,'jointNeuralBatches':neural_batches,'stages':stages,
          'proposalAuthority':'trained frozen neural network outputs; exact geometry only decodes and measures',
          'trainingScope':'new joint network is Captain-trained, not an unseen-graph evaluation',
          'actualBrowserVerified':False,'validation':'validation.json','files':records}
(TARGET/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'archive':str(TARGET),'manifestSha256':digest(TARGET/'manifest.json'),'files':len(records),
                  'storedBytes':sum(p.stat().st_size for p in TARGET.rglob('*') if p.is_file()),
                  'stages':len(stages),'policyActions':policy_actions,'jointNeuralBatches':neural_batches,
                  'overview':combined['overviewVisual'],'individual':combined['individualVisual'],'targetMet':manifest['targetMet']}))
