"""Preserve source-bound neural patch experiments and negative evidence."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import shutil

source=Path('.tmp/visualcross-ml-150-750-20261003')
target=Path('data/erd-poc/experiments/independent-views-local-patch-20261003')
prior=Path('data/erd-poc/experiments/independent-views-global-order-20261003')
peaks={'individual-patch-port1':144.2,'overview-patch-port1':177.3,
       'individual-sparse-patch1':147.2,'overview-sparse-patch1':180.5,
       'individual-patch-reward1':134.6,'overview-patch-reward1':185.6}


def digest(path, compressed=False):
    value=hashlib.sha256()
    with (gzip.open(path,'rb') if compressed else path.open('rb')) as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):value.update(chunk)
    return value.hexdigest()


files=[]
def copy(path,relative):
    assert path.is_file() and not path.is_symlink()
    compressed=path.suffix in ['.tsv','.jsonl'] or path.stat().st_size>131072 and path.suffix in ['.json','.log','.cpp']
    stored=Path(str(relative)+('.gz' if compressed else ''))
    destination=target/stored;destination.parent.mkdir(parents=True,exist_ok=True)
    sha=digest(path)
    with path.open('rb') as input,destination.open('xb') as output:
        if compressed:
            with gzip.GzipFile(filename='',mode='wb',fileobj=output,mtime=0,compresslevel=6) as archive:
                shutil.copyfileobj(input,archive,1024*1024)
        else:shutil.copyfileobj(input,output,1024*1024)
    if not compressed:destination.chmod(path.stat().st_mode&0o777)
    assert sha==digest(path)==digest(destination,compressed)
    files.append({'source':str(path),'stored':str(stored),'sha256':sha,'gzip':compressed,
                  'bytes':path.stat().st_size,'restoredHashVerified':True})


stable=Path('data/erd-poc/candidates/captain-ml-independent-views.layout.json')
stable_sha='c5223b13a922bdc7f57b1273b41ea9b0889809a8d19c8f7dfc9dc98e79592c34'
assert digest(stable)==stable_sha
assert not target.exists(),'preserve the existing archive'
reports={name:json.loads((source/name/('workflow.json' if name.startswith('overview') else 'workflow.audit.json')).read_text()) for name in peaks}
assert all(r['allChecksPassed'] and not r['improvesSource'] and r['unchangedSourceVerified'] for r in reports.values())
validation=json.loads((source/'patch-port-validation.json').read_text())
assert all(v['allChecksPassed'] and v['inactiveOutputsAndGradientsExactlyZero'] for v in validation['views'])
for name,sha in validation['implementationHashes'].items():
    assert digest(source/'patch-port-validation-sources'/name)==sha
environment=source/'joint-batch-environment-v7'
dependency=prior/'dependencies/joint-batch-environment-v7'
assert digest(environment)==digest(dependency)
assert all(r['environmentSha256']==digest(environment) for r in reports.values())
target.mkdir(parents=True)
stages=[]
for name,report in reports.items():
    directory=source/name
    stats=json.loads((directory/'learned.tsv.stats.json').read_text())
    assert digest(Path(report['candidate']))==report['candidateSha256']
    assert stats['policyActionsEvaluated']==report['frozenNetworkBatchesReplayed']
    for filename,sha in report['implementationHashes'].items():assert digest(directory/('source-'+filename))==sha
    for filename,sha in report['verifiedOutputHashes'].items():assert digest(directory/filename)==sha
    assert digest(directory/'patch-nodes.json')==report['patchNodesSha256']
    for row in map(json.loads,(directory/'joint-batches.jsonl').read_text().splitlines()):
        assert digest(Path(row['checkpoint']))==row['checkpointSha256']
        assert not row['result']['accepted']
    probes=[]
    if report.get('exactRewardTraining'):
        assert digest(Path(report['rewardInitialCheckpoint']))==report['rewardInitialCheckpointSha256']
        assert digest(directory/'reward-measurements.jsonl')==report['rewardTraceSha256']
        probes=[sample['result'] for row in map(json.loads,(directory/'reward-measurements.jsonl').read_text().splitlines())
                for direction in row['directions'] for sample in direction['samples']]
        assert len(probes)==report['rewardMeasurementsReplayed']
        assert all(p['measureOnly'] and not p['accepted'] for p in probes)
    legal=[row['geometry']['visual'] for row in report['history'] if row['geometry']['legal']]
    legal_probes=[p['visual'] for p in probes if p['legal']]
    stages.append({'name':name,'savedVisual':stats['visual'],'sourceVisual':report['sourceVisual'],
        'minimumLegalProposalVisual':min(legal) if legal else None,
        'outcomes':dict(collections.Counter(row['geometry']['reason'] for row in report['history'])),
        'frozenNeuralBatches':stats['policyActionsEvaluated'],'updates':report['iterations'],
        'activeNodes':report['activeNodeOutputs'],'activeEndpoints':report['activeEndpointOutputs'],
        'rewardMeasurements':len(probes),'rewardAdamUpdatesReplayed':report.get('rewardAdamUpdatesReplayed',0),
        'minimumLegalRewardMeasurement':min(legal_probes) if legal_probes else None,
        'rewardMeasurementOutcomes':dict(collections.Counter(p['reason'] for p in probes)),
        'peakMiB':peaks[name],'allFrozenBatchesReplayed':True,'promoted':False,
        'candidateSha256':report['candidateSha256']})
    for file in sorted(directory.rglob('*')):
        if file.is_file():copy(file,file.relative_to(source))

code_names=['run_joint_neural_layout.py','joint_layout_proxy.py','joint_active_proxy.py','joint_sampled_proxy.py',
    'joint_neural_ports.py','joint_grid_policy.py','joint_slot_policy.py','joint_reward_training.py','joint_graph_stress.py',
    'joint_grouped_routes.py','joint_hard_geometry.py','joint_graph_features.py','validate_joint_neural_ports.py',
    'learned_global_replay.py','run_memory_bounded.py','ml_joint_batch_environment.cpp','ml_component_environment.cpp',
    'constrained_boundary_sweep.h','constrained_dual_node_geometry.h','constrained_scene.h',
    'apply_learned_components.cjs','audit_individual_policy_experiment.cjs','audit_leaf_card_connections.cjs',
    'probe_overview_boundary.cjs','export_learned_components.cjs','compose_independent_views.cjs']
summary={'actualViewDerivativeChecks':sum(v['routeGradientChecks']+v['networkWeightGradientChecks'] for v in validation['views']),
    'validationPeakMiB':110.9,'inactiveOutputsAndGradientsExactlyZero':True,
    'zeroOutputEndpointsPreservedPerView':3454,'frozenGenericLoaderRoundtripsExact':True,
    'nativeBinaryChanged':False,'nativeAcceptanceUnchanged':True,'actualBrowserVerified':False,
    'resourcePolicy':{'singleNumericJob':True,'mathThreads':1,'niceIncrement':10,'rssGuardMiB':256,
        'hardCpuPercentageLimit':False,'maxObservedStagePeakMiB':max(peaks.values())},
    'stagePeakMiB':peaks,'finalImplementationHashes':{name:digest(Path('scripts/erd-poc')/name) for name in code_names}}
(source/'local-patch-validation-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
for name in ['patch-port-validation.json','local-patch-validation-summary.json','analyze_current_patches.py',
             'make_sparse_patch_masks.py','seal_local_patch_experiments.py']:
    copy(source/name,Path(name))
for dirname in ['localized-conflict-analysis','patch-port-validation-sources']:
    for file in sorted((source/dirname).rglob('*')):
        if file.is_file():copy(file,file.relative_to(source))
for name in code_names:copy(Path('scripts/erd-poc')/name,Path('code')/name)
for suffix in ['layout.json','audit.json','provenance.json']:
    name='captain-ml-independent-views.'+suffix
    copy(Path('data/erd-poc/candidates')/name,Path('retained-best')/name)
assert digest(stable)==stable_sha
manifest={'scope':'localized masked neural node/port learning and exact reward-head training',
    'overviewTarget':150,'individualTarget':750,'overviewVisual':299,'individualVisual':1990,
    'targetMet':False,'promoted':False,'promotedCandidateChanged':False,'retainedCandidateSha256':stable_sha,
    'jointNeuralBatches':sum(s['frozenNeuralBatches'] for s in stages),
    'trainingRewardMeasurements':sum(s['rewardMeasurements'] for s in stages),
    'stages':stages,'priorArchive':str(prior/'manifest.json'),'priorArchiveSha256':digest(prior/'manifest.json'),
    'nativeDependency':{'file':str(dependency),'sha256':digest(dependency)},
    'proposalAuthority':'trained frozen neural outputs; native geometry only decodes and measures',
    'validation':'local-patch-validation-summary.json','actualBrowserVerified':False,
    'noNumericImprovement':True,'files':files}
(target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'archive':str(target),'manifestSha256':digest(target/'manifest.json'),
    'stages':len(stages),'frozenNeuralBatches':manifest['jointNeuralBatches'],
    'trainingRewardMeasurements':manifest['trainingRewardMeasurements'],'files':len(files),
    'storedBytes':sum(p.stat().st_size for p in target.rglob('*') if p.is_file()),'stageSummary':stages}))
