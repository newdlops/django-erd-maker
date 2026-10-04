"""Preserve tested global neural ordering branches and their negative results."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import shutil

source=Path('.tmp/visualcross-ml-150-750-20261003')
target=Path('data/erd-poc/experiments/independent-views-global-order-20261003')
prior=Path('data/erd-poc/experiments/independent-views-active-loss-20261003')
peaks={'individual-ray-conditioned1':153.9,'individual-grid-order1':159.7,
       'individual-slot-reward1':141.5,'individual-grid-stress1':134.7,
       'overview-ray-conditioned1':174.9}


def digest(path, compressed=False):
    value=hashlib.sha256()
    with (gzip.open(path,'rb') if compressed else path.open('rb')) as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):value.update(chunk)
    return value.hexdigest()


records=[]
def copy(path,relative,plain=False):
    assert path.is_file() and not path.is_symlink()
    compressed=not plain and (path.suffix in ['.tsv','.jsonl']
        or path.stat().st_size>131072 and path.suffix in ['.json','.log'])
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
    records.append({'source':str(path),'stored':str(stored),'sha256':sha,
        'gzip':compressed,'bytes':path.stat().st_size,'restoredHashVerified':True})


stable=Path('data/erd-poc/candidates/captain-ml-independent-views.layout.json')
stable_sha='c5223b13a922bdc7f57b1273b41ea9b0889809a8d19c8f7dfc9dc98e79592c34'
assert digest(stable)==stable_sha
assert not target.exists(),'preserve existing archive'
reports={name:json.loads((source/name/('workflow.json' if name.startswith('overview') else 'workflow.audit.json')).read_text()) for name in peaks}
assert all(r['allChecksPassed'] and not r['improvesSource'] for r in reports.values())
assert all(r['trainingExitedBeforeProductAudit'] for r in reports.values())
ray=json.loads((source/'ray-conditioned-neural-validation.json').read_text())
for name,sha in ray['implementationHashes'].items():assert digest(source/'ray-conditioned-validation-sources'/name)==sha
protocol=json.loads((source/'native-measure-validation/audit.json').read_text())
assert protocol['allChecksPassed'] and protocol['measurementPreservedSourceState'] and not protocol['positiveControlSaved']
target.mkdir(parents=True)
stages=[];reward_measurements=0
for name,report in reports.items():
    directory=source/name
    stats=json.loads((directory/'learned.tsv.stats.json').read_text())
    assert digest(Path(report['candidate']))==report['candidateSha256']
    assert stats['policyActionsEvaluated']==report['frozenNetworkBatchesReplayed']
    for filename,sha in report['implementationHashes'].items():assert digest(directory/('source-'+filename))==sha
    for filename,sha in report['verifiedOutputHashes'].items():assert digest(directory/filename)==sha
    for row in map(json.loads,(directory/'joint-batches.jsonl').read_text().splitlines()):
        assert digest(Path(row['checkpoint']))==row['checkpointSha256']
    if report.get('exactRewardTraining'):
        assert digest(Path(report['rewardInitialCheckpoint']))==report['rewardInitialCheckpointSha256']
        assert digest(directory/'reward-measurements.jsonl')==report['rewardTraceSha256']
        probes=[sample for row in map(json.loads,(directory/'reward-measurements.jsonl').read_text().splitlines())
                for direction in row['directions'] for sample in direction['samples']]
        assert len(probes)==report['rewardMeasurementsReplayed']
        assert all(p['result']['measureOnly'] and not p['result']['accepted'] for p in probes)
        reward_measurements+=len(probes)
    legal=[row['geometry']['visual'] for row in report['history'] if row['geometry']['legal']]
    stages.append({'name':name,'savedVisual':stats['visual'],'sourceVisual':report['sourceVisual'],
        'minimumLegalProposalVisual':min(legal) if legal else None,
        'outcomes':dict(collections.Counter(row['geometry']['reason'] for row in report['history'])),
        'frozenNeuralBatches':stats['policyActionsEvaluated'],'updates':report['iterations'],
        'rewardMeasurements':report.get('rewardMeasurementsReplayed',0),
        'rewardAdamUpdatesReplayed':report.get('rewardAdamUpdatesReplayed',0),
        'temporaryRegressionLimit':report['temporaryRegressionLimit'],
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
validation={'actualRayConditionedNetworkDerivativeChecks':104,'rayConditionedPeakMiB':108.1,
    'zeroOutputEndpointsPreservedPerView':3454,'nativeFullGeometryComparisons':576,'nativeLegalFixtureBatches':335,
    'nativeMeasurementPurityFixture':'native-measure-validation/audit.json','nativeCompilePeakMiB':229.3,
    'finalUnitSuite':{'softRankSurrogateDerivativeChecks':24,'randomizedSeparatedFrameChecks':64,
        'randomizedRectangleMultisetChecks':64,'rewardFixtureProbesReplayed':768,'rewardFixtureAdamUpdatesReplayed':96,
        'rewardFixtureFinalSquaredError':.0001393481211660965,'graphStressNetworkDerivativeChecks':24,
        'frozenRoundtripsExact':True,'peakMiB':32.0,'allPassed':True},
    'finalImplementationHashes':{name:digest(Path('scripts/erd-poc')/name) for name in code_names},
    'actualTrainingRewardProbesReplayed':reward_measurements,'actualTrainingRewardAdamUpdatesReplayed':32,
    'gridStraightThroughGradientIsBiased':True,'lowerGraphStressDoesNotProveLowerVisualConflicts':True,
    'slotRectangleInvariantScope':'continuous model output, before native displacement rounding',
    'slotSavedCardPositionsChanged':0,'slotSavedCanonicalPortsVisual':2139,'slotBranchPromoted':False,
    'nativeAcceptanceUnchanged':True,'actualBrowserVerified':False,
    'resourcePolicy':{'singleNumericJob':True,'mathThreads':1,'niceIncrement':10,'rssGuardMiB':256,
        'hardCpuPercentageLimit':False,'maxObservedStagePeakMiB':max(peaks.values())},'stagePeakMiB':peaks}
(source/'global-order-validation-summary.json').write_text(json.dumps(validation,indent=2)+'\n')
for name in ['ray-conditioned-neural-validation.json','global-order-validation-summary.json',
    'global-training-structure.json','permutation-source-diagnosis.json','stress-coordinate-diagnosis.json',
    'check_permutation_source.py','validate_measure_protocol.py','measure_stress_coordinates.py','seal_global_order_experiments.py']:
    copy(source/name,Path(name))
for dirname in ['ray-conditioned-validation-sources','global-order-validation-sources','native-measure-validation']:
    for file in sorted((source/dirname).rglob('*')):
        if file.is_file():copy(file,file.relative_to(source))
for name in code_names:copy(Path('scripts/erd-poc')/name,Path('code')/name)
for name in ['joint-batch-environment-v6','joint-batch-environment-v7']:
    copy(source/name,Path('dependencies')/name)
for suffix in ['layout.json','audit.json','provenance.json']:
    name='captain-ml-independent-views.'+suffix
    copy(Path('data/erd-poc/candidates')/name,Path('retained-best')/name,plain=True)
assert digest(stable)==stable_sha
manifest={'scope':'ray-conditioned neural endpoints, global neural ordering, exact reward-head learning, and graph stress',
    'overviewTarget':150,'individualTarget':750,'overviewVisual':299,'individualVisual':1990,
    'targetMet':False,'promoted':False,'promotedCandidateChanged':False,'retainedCandidateSha256':stable_sha,
    'jointNeuralBatches':sum(s['frozenNeuralBatches'] for s in stages),'trainingRewardMeasurements':reward_measurements,
    'stages':stages,'priorArchive':str(prior/'manifest.json'),'priorArchiveSha256':digest(prior/'manifest.json'),
    'proposalAuthority':'trained frozen neural outputs; native geometry only decodes and measures',
    'validation':'global-order-validation-summary.json','actualBrowserVerified':False,
    'noNumericImprovement':True,'files':records}
(target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'archive':str(target),'manifestSha256':digest(target/'manifest.json'),
    'stages':len(stages),'frozenNeuralBatches':manifest['jointNeuralBatches'],
    'trainingRewardMeasurements':reward_measurements,'files':len(records),
    'storedBytes':sum(p.stat().st_size for p in target.rglob('*') if p.is_file())}))
