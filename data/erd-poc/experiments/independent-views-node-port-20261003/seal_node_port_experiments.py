"""Seal joint node/endpoint trials, including the rejected corner-cut branch."""
import gzip
import hashlib
import json
from pathlib import Path
import shutil
source=Path('.tmp/visualcross-ml-150-750-20261003')
target=Path('data/erd-poc/experiments/independent-views-node-port-20261003')
names=['individual-node-port1','overview-node-port1','individual-node-port2','individual-node-port-stage1',
       'individual-node-port-refine1','individual-node-port-refine2','individual-node-port-refine3',
       'individual-node-port-stage2','individual-node-port-strict1','individual-node-port-strict2','individual-node-port-strict3']
def digest(path, compressed=False):
    value = hashlib.sha256()
    with (gzip.open(path, 'rb') if compressed else path.open('rb')) as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            value.update(chunk)
    return value.hexdigest()

records = []
def copy(path, relative, plain=False):
    assert path.is_file() and not path.is_symlink()
    compressed = not plain and (path.suffix in ['.tsv', '.jsonl']
                  or path.stat().st_size > 131072 and path.suffix in ['.json', '.log'])
    stored = Path(str(relative)+('.gz' if compressed else ''))
    destination = target/stored
    destination.parent.mkdir(parents=True, exist_ok=True)
    sha = digest(path)
    with path.open('rb') as input, destination.open('xb') as output:
        if compressed:
            with gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0, compresslevel=6) as archive:
                shutil.copyfileobj(input, archive, 1024*1024)
        else:
            shutil.copyfileobj(input, output, 1024*1024)
    if not compressed:
        destination.chmod(path.stat().st_mode & 0o777)
    assert sha == digest(path) == digest(destination, compressed)
    records.append({'source': str(path), 'stored': str(stored), 'sha256': sha,
                    'gzip': compressed, 'bytes': path.stat().st_size, 'restoredHashVerified': True})


reports={name:json.loads((source/name/('workflow.json' if name.startswith('overview') else 'workflow.audit.json')).read_text()) for name in names}
combined=json.loads((source/'combined-node-port-valid/audit.json').read_text())
assert combined['actualProductFileLoadVerified'] and combined['roundTripViewSwitchVerified']
assert combined['overviewTarget']==150 and combined['individualTarget']==750
assert combined['overviewVisual']==299 and combined['individualVisual']<1996 and not combined['thresholdsMet']
assert all(r['allChecksPassed'] for r in reports.values())
assert not target.exists(),'preserve existing archive'
target.mkdir(parents=True)
stages=[];batches=0;actions=0
invalid={'individual-node-port-stage1','individual-node-port-refine1','individual-node-port-refine2','individual-node-port-refine3'}
resources=json.loads((source/'node-port-validation-summary.json').read_text())
for name in names:
    directory=source/name;report=reports[name]
    stats=json.loads((directory/'learned.tsv.stats.json').read_text())
    assert digest(Path(report['candidate']))==report['candidateSha256']
    neural='frozenNetworkBatchesReplayed' in report
    count=stats['policyActionsEvaluated']
    if neural:
        assert count==report['frozenNetworkBatchesReplayed'];batches+=count
        for file,sha in report['implementationHashes'].items():assert digest(directory/('source-'+file))==sha
        for file,sha in report['verifiedOutputHashes'].items():assert digest(directory/file)==sha
    else:actions+=count
    stages.append({'name':name,'visual':stats['visual'],'actualActions':count,'jointNetwork':neural,
                   'historicalChecksPassed':True,'laterProductOutwardContractRejected':name in invalid,
                   'promoted':False,'candidateSha256':report['candidateSha256'],
                   'peakMiB':resources['stagePeakMiB'][name]})
    for file in sorted(directory.rglob('*')):
        if file.is_file():copy(file,file.relative_to(source))
for name in ['neural-port-validation.json','neural-port-outward-validation.json','node-port-outward-recheck.json',
             'node-port-validation-summary.json','check_node_port_outward.py','seal_node_port_experiments.py']:
    copy(source/name,Path(name))
for name in ['run_joint_neural_layout.py','joint_layout_proxy.py','joint_grouped_routes.py','joint_neural_ports.py',
             'joint_hard_geometry.py','joint_graph_features.py','joint_sampled_proxy.py','validate_joint_neural_ports.py',
             'ml_joint_batch_environment.cpp','ml_component_environment.cpp','ml_port_environment.cpp',
             'learn_card_policy.py','learned_global_replay.py','run_memory_bounded.py','run_individual_policy_experiment.py',
             'apply_learned_components.cjs','audit_individual_policy_experiment.cjs','audit_leaf_card_connections.cjs',
             'export_learned_components.cjs','compose_independent_views.cjs',
             'constrained_dual_node_geometry.h','constrained_boundary_sweep.h','constrained_scene.h']:
    copy(Path('scripts/erd-poc')/name,Path('code')/name)
prior=Path('data/erd-poc/experiments/independent-views-hard-graph-20261003')
for name in ['constrained_boundary_sweep.h','audit_individual_policy_experiment.cjs']:
    copy(prior/'code'/name,Path('code-before-outward')/name)
for name in ['joint-batch-environment-v4','joint-batch-environment-v6','port-environment-v5','component-environment-v21','component-environment-v22']:
    copy(source/name,Path('dependencies')/name)
copy(Path('.tmp/visualcross-ml-targets-20261003/port-environment-v4'),Path('dependencies/port-environment-v4'))
for version in [10,11,15]:
    name=f'leaf-component-policy-v{version}.npz';copy(Path('data/erd-poc/checkpoints')/name,Path('dependencies')/name)
for file in (source/'combined-node-port-valid').iterdir():
    if file.is_file():copy(file,Path('combined-best')/file.name,plain=True)
# Retain the failed loader input, but never label it as a verified candidate.
for file in (source/'combined-node-port-best').iterdir():
    if file.is_file():copy(file,Path('rejected-combined')/file.name)
for suffix in ['layout.json','audit.json','provenance.json']:
    name='captain-ml-independent-views.'+suffix
    copy(Path('data/erd-poc/candidates')/name,Path('previous-best')/name,plain=True)
assert digest(target/'previous-best/captain-ml-independent-views.layout.json')=='623b5aa794afdb3233fa541c9f4afe21ec3512d60457b6f510c1156e62224fe9'
manifest={'scope':'joint neural card/endpoint model and strict outward-boundary validation',
          'overviewTarget':150,'individualTarget':750,'overviewVisual':299,'individualVisual':combined['individualVisual'],
          'candidateSha256':combined['candidateSha256'],'targetMet':False,'promoted':False,
          'jointNeuralBatches':batches,'additionalPolicyActions':actions,'stages':stages,
          'priorArchive':str(prior/'manifest.json'),'priorArchiveSha256':digest(prior/'manifest.json'),
          'proposalAuthority':'trained frozen neural outputs; native geometry only decodes and measures',
          'rejectedBranchReason':'near-corner inward endpoint passed historical standalone checks but failed the product loader',
          'validation':'node-port-validation-summary.json','actualBrowserVerified':False,'files':records}
(target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'archive':str(target),'manifestSha256':digest(target/'manifest.json'),'stages':len(stages),
                  'jointNeuralBatches':batches,'additionalPolicyActions':actions,'files':len(records),
                  'storedBytes':sum(p.stat().st_size for p in target.rglob('*') if p.is_file())}))
