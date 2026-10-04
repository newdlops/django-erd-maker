"""Seal bounded active-loss trials without changing the promoted geometry."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import shutil

source=Path('.tmp/visualcross-ml-150-750-20261003')
target=Path('data/erd-poc/experiments/independent-views-active-loss-20261003')
prior=Path('data/erd-poc/experiments/independent-views-node-port-20261003')
peaks={'individual-outward-joint1':234.9,'individual-wide-port1':150.7,
       'individual-wide-port2':163.7,'individual-active-port1':152.6,
       'individual-active-depth1':142.0,'overview-active-depth1':174.2,
       'individual-shared-depth1':154.7,'overview-shared-depth1':175.8,
       'individual-native-gap1':137.9,'overview-native-gap1':178.9}


def digest(path, compressed=False):
    value=hashlib.sha256()
    with (gzip.open(path,'rb') if compressed else path.open('rb')) as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):value.update(chunk)
    return value.hexdigest()


records=[]
def copy(path, relative, plain=False):
    assert path.is_file() and not path.is_symlink()
    compressed=not plain and (path.suffix in ['.tsv','.jsonl']
               or path.stat().st_size>131072 and path.suffix in ['.json','.log'])
    stored=Path(str(relative)+('.gz' if compressed else ''))
    destination=target/stored
    destination.parent.mkdir(parents=True,exist_ok=True)
    sha=digest(path)
    with path.open('rb') as input, destination.open('xb') as output:
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
assert all(r['allChecksPassed'] and r['unchangedSourceVerified'] and not r['improvesSource'] for r in reports.values())
assert all(r['trainingExitedBeforeProductAudit'] for r in reports.values())
final_validation=json.loads((source/'shared-native-gap-validation.json').read_text())
assert all(r['allChecksPassed'] and r['baselineHardPenalty']==0 for r in final_validation['views'])
for name,sha in final_validation['implementationHashes'].items():
    assert digest(source/'final-active-validation-sources'/name)==sha
target.mkdir(parents=True)
stages=[]
for name,report in reports.items():
    directory=source/name
    stats=json.loads((directory/'learned.tsv.stats.json').read_text())
    assert digest(Path(report['candidate']))==report['candidateSha256']
    assert stats['policyActionsEvaluated']==report['frozenNetworkBatchesReplayed']
    assert stats['visual']==report['sourceVisual']
    for filename,sha in report['implementationHashes'].items():assert digest(directory/('source-'+filename))==sha
    for filename,sha in report['verifiedOutputHashes'].items():assert digest(directory/filename)==sha
    for row in map(json.loads,(directory/'joint-batches.jsonl').read_text().splitlines()):
        assert digest(Path(row['checkpoint']))==row['checkpointSha256']
    legal=[row['geometry']['visual'] for row in report['history'] if row['geometry']['legal']]
    stages.append({'name':name,'retainedVisual':stats['visual'],
                   'minimumLegalProposalVisual':min(legal) if legal else None,
                   'outcomes':dict(collections.Counter(row['geometry']['reason'] for row in report['history'])),
                   'frozenNeuralBatches':stats['policyActionsEvaluated'],'updates':report['iterations'],
                   'peakMiB':peaks[name],'allFrozenBatchesReplayed':True,'unchangedSourceVerified':True,
                   'sourceSha256':report['sourceSha256'],'candidateSha256':report['candidateSha256'],
                   'promoted':False})
    for file in sorted(directory.rglob('*')):
        if file.is_file():copy(file,file.relative_to(source))

validation={'finalActualViewGradientChecks':sum(r['routeGradientChecks']+r['networkWeightGradientChecks'] for r in final_validation['views']),
            'finalActualViewValidation':'shared-native-gap-validation.json',
            'finalActualViewPeakMiB':108.2,'zeroOutputEndpointsPreservedPerView':3454,
            'sharedSourceGroupsPreserved':[r['sharedGroupsPreserved'] for r in final_validation['views']],
            'genericLoaderDetectsSharedEndpointNetwork':True,'frozenRoundtripExact':True,
            'activeLossFixture':{'gradientComparisons':96,'fullPopulationParity':True,
                'tinyChunkAccumulationChecked':True,'depthWeight':.5,'spacingPadding':0.,'peakMiB':41.0,'passed':True},
            'continuousOutwardFixture':{'peerBoundaryCases':6,'peerGradientChecks':4,
                'cornerCutDetectedWithoutOwnCardHit':True,'peakMiB':26.1,'passed':True},
            'resourcePolicy':{'singleNumericJob':True,'mathThreads':1,'niceIncrement':10,
                'rssGuardMiB':256,'maxObservedStagePeakMiB':max(peaks.values()),'hardCpuPercentageLimit':False},
            'intermediateActualViewReports':{'continuous-outward-validation.json':75.1,
                'active-neural-validation.json':112.4,'active-depth-neural-validation.json':109.3,
                'shared-endpoint-neural-validation.json':107.0},
            'nativeAcceptanceUnchanged':True,'actualBrowserVerified':False,
            'stagePeakMiB':peaks,'measurementsSource':'completed command output and saved stage audits'}
(source/'active-loss-validation-summary.json').write_text(json.dumps(validation,indent=2)+'\n')
for name in ['continuous-outward-validation.json','active-neural-validation.json','active-depth-neural-validation.json',
             'shared-endpoint-neural-validation.json','shared-native-gap-validation.json',
             'active-loss-validation-summary.json','active-depth-constraint-diagnosis.json',
             'analyze_active_depth.py','seal_active_loss_experiments.py']:
    copy(source/name,Path(name))
for file in sorted((source/'final-active-validation-sources').iterdir()):
    copy(file,Path('final-active-validation-sources')/file.name)
for name in ['run_joint_neural_layout.py','joint_layout_proxy.py','joint_active_proxy.py','joint_sampled_proxy.py',
             'joint_neural_ports.py','joint_grouped_routes.py','joint_hard_geometry.py','joint_graph_features.py',
             'validate_joint_neural_ports.py','validate_outward_loss.py','learned_global_replay.py','run_memory_bounded.py',
             'ml_joint_batch_environment.cpp','ml_component_environment.cpp','constrained_boundary_sweep.h',
             'constrained_dual_node_geometry.h','constrained_scene.h','apply_learned_components.cjs',
             'audit_individual_policy_experiment.cjs','audit_leaf_card_connections.cjs',
             'probe_overview_boundary.cjs','export_learned_components.cjs','compose_independent_views.cjs']:
    copy(Path('scripts/erd-poc')/name,Path('code')/name)
copy(source/'joint-batch-environment-v6',Path('dependencies/joint-batch-environment-v6'))
for suffix in ['layout.json','audit.json','provenance.json']:
    name='captain-ml-independent-views.'+suffix
    copy(Path('data/erd-poc/candidates')/name,Path('retained-best')/name,plain=True)
assert digest(stable)==stable_sha
manifest={'scope':'continuous outward loss, chunked active-pair loss, depth signal, and shared neural endpoints',
          'overviewTarget':150,'individualTarget':750,'overviewVisual':299,'individualVisual':1990,
          'targetMet':False,'promoted':False,'promotedCandidateChanged':False,'retainedCandidateSha256':stable_sha,
          'jointNeuralBatches':sum(s['frozenNeuralBatches'] for s in stages),'stages':stages,
          'priorArchive':str(prior/'manifest.json'),'priorArchiveSha256':digest(prior/'manifest.json'),
          'proposalAuthority':'trained frozen neural outputs; native geometry only decodes and measures',
          'validation':'active-loss-validation-summary.json','actualBrowserVerified':False,
          'noNumericImprovement':True,'files':records}
(target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'archive':str(target),'manifestSha256':digest(target/'manifest.json'),
                  'stages':len(stages),'frozenNeuralBatches':manifest['jointNeuralBatches'],
                  'files':len(records),'storedBytes':sum(p.stat().st_size for p in target.rglob('*') if p.is_file())}))
