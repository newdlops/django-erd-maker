"""Preserve coupled-head research and the fully verified rigid-branch gain."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import shutil

source=Path('.tmp/visualcross-ml-150-750-20261003')
target=Path('data/erd-poc/experiments/independent-views-rigid-branches-20261003')
prior=Path('data/erd-poc/experiments/independent-views-local-patch-20261003')
peaks={'individual-coupled-reward1':132.6,'overview-coupled-reward1':177.5,
       'individual-coupled-wide1':136.2,'individual-rigid-branch1':153.6,
       'overview-rigid-branch1':178.8,'overview-rigid-file1':178.2,
       'individual-rigid-port1':139.9,'individual-rigid-port2':134.2,
       'individual-rigid-node1':133.6,'individual-rigid-port3':140.4}


def digest(path, compressed=False):
    result=hashlib.sha256()
    with (gzip.open(path,'rb') if compressed else path.open('rb')) as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):result.update(chunk)
    return result.hexdigest()


files=[]
def copy(path,relative,plain=False):
    assert path.is_file() and not path.is_symlink()
    compressed=not plain and (path.suffix in ['.tsv','.jsonl'] or path.stat().st_size>131072 and path.suffix in ['.json','.log','.cpp'])
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
old_sha='c5223b13a922bdc7f57b1273b41ea9b0889809a8d19c8f7dfc9dc98e79592c34'
assert digest(stable)==old_sha
assert not target.exists(),'preserve the existing archive'
combined=json.loads((source/'combined-rigid-verified/audit.json').read_text())
assert digest(source/'combined-rigid-verified/candidate.layout.json')==combined['candidateSha256']
assert (combined['overviewVisual'],combined['individualVisual'])==(299,1981)
assert (combined['overviewTarget'],combined['individualTarget'])==(150,750) and not combined['thresholdsMet']
for key in ['actualProductFileLoadVerified','completeIndividualGeometryPreserved','browserRouteFunctionParity','roundTripViewSwitchVerified']:
    assert combined[key]
assert combined['individualSpacingViolations']==0 and combined['models']==1244 and combined['canonicalRelationships']==1727
reports={name:json.loads((source/name/('workflow.json' if name.startswith('overview') else 'workflow.audit.json')).read_text()) for name in peaks}
assert all(r['allChecksPassed'] for r in reports.values())
for filename,dirname in [('coupled-reward-validation.json','coupled-reward-validation-sources'),
                         ('rigid-patch-validation.json','rigid-patch-validation-sources')]:
    validation=json.loads((source/filename).read_text())
    for name,sha in validation['implementationHashes'].items():assert digest(source/dirname/name)==sha
dependencies=[]
for name in ['joint-batch-environment-v7','port-environment-v5','component-environment-v22',
             'leaf-component-policy-v10.npz','leaf-component-policy-v11.npz','leaf-component-policy-v15.npz']:
    previous='independent-views-global-order-20261003' if name=='joint-batch-environment-v7' else 'independent-views-node-port-20261003'
    artifact=Path('data/erd-poc/experiments')/previous/'dependencies'/name
    local=Path('data/erd-poc/checkpoints')/name if name.endswith('.npz') else source/name
    assert digest(local)==digest(artifact)
    dependencies.append({'file':str(artifact),'sha256':digest(artifact)})
dependency_hashes={d['sha256'] for d in dependencies}
target.mkdir(parents=True)
stages=[];batches=actions=measurements=updates=0
for name,report in reports.items():
    directory=source/name
    stats=json.loads((directory/'learned.tsv.stats.json').read_text())
    assert digest(Path(report['candidate']))==report['candidateSha256']
    assert report['environmentSha256'] in dependency_hashes
    neural='frozenNetworkBatchesReplayed' in report
    row={'name':name,'savedVisual':stats['visual'],'actualActions':stats['policyActionsEvaluated'],
         'jointNetwork':neural,'peakMiB':peaks[name],'candidateSha256':report['candidateSha256'],
         'historicalChecksPassed':True,'promoted':False}
    if neural:
        assert stats['policyActionsEvaluated']==report['frozenNetworkBatchesReplayed'];batches+=stats['policyActionsEvaluated']
        for filename,sha in report['implementationHashes'].items():assert digest(directory/('source-'+filename))==sha
        for filename,sha in report['verifiedOutputHashes'].items():assert digest(directory/filename)==sha
        assert digest(directory/'patch-nodes.json')==report['patchNodesSha256']
        for proposal in map(json.loads,(directory/'joint-batches.jsonl').read_text().splitlines()):
            assert digest(Path(proposal['checkpoint']))==proposal['checkpointSha256']
        legal=[x['geometry']['visual'] for x in report['history'] if x['geometry']['legal']]
        row.update(sourceVisual=report['sourceVisual'],updates=report['iterations'],rigidPatch=report.get('rigidPatch',False),
            activeNodes=report['activeNodeOutputs'],minimumLegalProposal=min(legal) if legal else None,
            outcomes=dict(collections.Counter(x['geometry']['reason'] for x in report['history'])))
        if report.get('exactRewardTraining'):
            assert report['rewardTrainableKeys']==['wo','ewo']
            assert digest(Path(report['rewardInitialCheckpoint']))==report['rewardInitialCheckpointSha256']
            assert digest(directory/'reward-measurements.jsonl')==report['rewardTraceSha256']
            probes=[s['result'] for x in map(json.loads,(directory/'reward-measurements.jsonl').read_text().splitlines()) for d in x['directions'] for s in d['samples']]
            assert len(probes)==report['rewardMeasurementsReplayed']
            assert all(p['measureOnly'] and not p['accepted'] for p in probes)
            measurements+=len(probes);updates+=report['rewardAdamUpdatesReplayed']
            legal=[p['visual'] for p in probes if p['legal']]
            row.update(rewardMeasurements=len(probes),rewardAdamUpdates=report['rewardAdamUpdatesReplayed'],
                       minimumLegalRewardMeasurement=min(legal) if legal else None,
                       rewardProbeOutcomes=dict(collections.Counter(p['reason'] for p in probes)))
    else:
        actions+=stats['policyActionsEvaluated']
        assert report['checkpointSha256'] in dependency_hashes
        row['sourceVisual']=report['sourceIndividualVisual']
    stages.append(row)
    for file in sorted(directory.rglob('*')):
        if file.is_file():copy(file,file.relative_to(source))

code_names=['run_joint_neural_layout.py','joint_layout_proxy.py','joint_active_proxy.py','joint_sampled_proxy.py',
    'joint_neural_ports.py','joint_grid_policy.py','joint_slot_policy.py','joint_reward_training.py','joint_graph_stress.py',
    'joint_grouped_routes.py','joint_hard_geometry.py','joint_graph_features.py','validate_joint_neural_ports.py',
    'learned_global_replay.py','run_memory_bounded.py','ml_joint_batch_environment.cpp','ml_component_environment.cpp',
    'ml_port_environment.cpp','learn_card_policy.py','run_individual_policy_experiment.py',
    'constrained_boundary_sweep.h','constrained_dual_node_geometry.h','constrained_scene.h',
    'apply_learned_components.cjs','audit_individual_policy_experiment.cjs','audit_leaf_card_connections.cjs',
    'probe_overview_boundary.cjs','export_learned_components.cjs','compose_independent_views.cjs']
summary={'actualRigidNetworkDerivativeChecks':104,'rigidValidationPeakMiB':118.3,
    'rigidTranslationsExactlyEqual':True,'inactiveOutputsAndGradientsExactlyZero':True,
    'zeroOutputEndpointsPreservedPerView':3454,'frozenGenericLoaderRoundtripsExact':True,
    'coupledRewardValidation':'coupled-reward-validation.json','coupledRewardValidationPeakMiB':51.5,
    'coupledFixtureMeasurements':1280,'coupledFixtureAdamUpdates':160,
    'singleHeadFixtureMeasurements':768,'singleHeadFixtureAdamUpdates':96,
    'legacyActualMeasurementsReplayed':536,'legacyActualAdamUpdatesReplayed':134,
    'newActualRewardMeasurementsReplayed':measurements,'newActualRewardAdamUpdatesReplayed':updates,
    'leafConflictAnalysisPeakMiB':30.5,'nativeAcceptanceUnchanged':True,
    'actualProductFileLoadVerified':True,'browserRouteFunctionParity':True,'roundTripViewSwitchVerified':True,
    'actualBrowserVerified':False,'productAuditPeakMiB':166.6,'causalAblationVerified':False,
    'resourcePolicy':{'singleNumericJob':True,'mathThreads':1,'niceIncrement':10,'rssGuardMiB':256,
        'hardCpuPercentageLimit':False,'maxObservedStagePeakMiB':max(peaks.values())},
    'stagePeakMiB':peaks,'finalImplementationHashes':{name:digest(Path('scripts/erd-poc')/name) for name in code_names}}
(source/'rigid-branch-validation-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
for name in ['coupled-reward-validation.json','validate_coupled_reward.py','rigid-patch-validation.json',
    'rigid-branch-validation-summary.json','rigid-branch-winning-diagnosis.json','analyze_leaf_conflicts.py',
    'make_rigid_branch_masks.py','seal_rigid_branch_experiments.py']:
    copy(source/name,Path(name))
for dirname in ['coupled-reward-validation-sources','rigid-patch-validation-sources','leaf-conflict-analysis','rigid-branch-masks']:
    for file in sorted((source/dirname).rglob('*')):
        if file.is_file():copy(file,file.relative_to(source))
for name in code_names:copy(Path('scripts/erd-poc')/name,Path('code')/name)
for file in (source/'combined-rigid-verified').iterdir():
    if file.is_file():copy(file,Path('combined-best')/file.name,plain=True)
for suffix in ['layout.json','audit.json','provenance.json']:
    name='captain-ml-independent-views.'+suffix
    copy(Path('data/erd-poc/candidates')/name,Path('previous-best')/name,plain=True)
assert digest(stable)==old_sha
manifest={'scope':'coupled neural reward heads, rigid graph-branch network, and learned refinement',
    'overviewTarget':150,'individualTarget':750,'overviewVisual':299,'individualVisual':1981,
    'targetMet':False,'promoted':False,'candidateSha256':combined['candidateSha256'],'previousCandidateSha256':old_sha,
    'jointNeuralBatches':batches,'additionalPolicyActions':actions,'trainingRewardMeasurements':measurements,
    'stages':stages,'priorArchive':str(prior/'manifest.json'),'priorArchiveSha256':digest(prior/'manifest.json'),
    'dependencies':dependencies,'proposalAuthority':'trained frozen neural outputs; native geometry only decodes and measures',
    'validation':'rigid-branch-validation-summary.json','actualBrowserVerified':False,'files':files}
(target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'archive':str(target),'manifestSha256':digest(target/'manifest.json'),'stages':len(stages),
    'frozenNeuralBatches':batches,'additionalPolicyActions':actions,'rewardMeasurements':measurements,
    'files':len(files),'storedBytes':sum(p.stat().st_size for p in target.rglob('*') if p.is_file())}))
