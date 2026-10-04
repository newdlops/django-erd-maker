"""Retain exact failed and successful evidence without replacing the best."""
import collections,gzip,hashlib,json,shutil
from pathlib import Path

base=Path('.tmp/visualcross-ml-150-750-20261003')
target=Path('data/erd-poc/experiments/independent-views-separation-dag-20261003')
stable=Path('data/erd-poc/candidates/captain-ml-independent-views.layout.json')
expected='da2317d3d9fe72181587f543843bfdde3cc7030ca328e93c4f627ae21ec356e7'
metrics=json.loads((base/'separation-dag-run-metrics.json').read_text())
def digest(path,compressed=False):
    value=hashlib.sha256()
    with (gzip.open(path,'rb') if compressed else path.open('rb')) as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):value.update(chunk)
    return value.hexdigest()
def read(path):return json.loads(path.read_text())
assert not target.exists() and digest(stable)==expected
stable_audit=read(stable.with_name('captain-ml-independent-views.audit.json'))
stable_provenance=read(stable.with_name('captain-ml-independent-views.provenance.json'))
assert stable_audit['candidateSha256']==stable_provenance['candidateSha256']==expected
assert (stable_audit['overviewVisual'],stable_audit['individualVisual'])==(289,1964)
assert stable_provenance['targetMet'] is False
for key in ['actualProductFileLoadVerified','completeIndividualGeometryPreserved','browserRouteFunctionParity','roundTripViewSwitchVerified']:
    assert stable_audit[key]

dependencies=[]
for archive,filename,local in [
    ('independent-views-global-order-20261003','joint-batch-environment-v7',base/'joint-batch-environment-v7'),
    ('independent-views-bounded-policy-20261003','component-environment-v26',base/'component-environment-v26'),
    ('independent-views-node-port-20261003','port-environment-v5',base/'port-environment-v5'),
    ('independent-views-node-port-20261003','leaf-component-policy-v11.npz',Path('data/erd-poc/checkpoints/leaf-component-policy-v11.npz'))]:
    directory=Path('data/erd-poc/experiments')/archive;manifest=directory/'manifest.json'
    entry=next(r for r in read(manifest)['files'] if r['stored']=='dependencies/'+filename)
    assert digest(local)==entry['sha256']==digest(directory/entry['stored'],entry['gzip'])
    dependencies.append({'file':str(directory/entry['stored']),'sha256':entry['sha256'],
        'archiveManifest':str(manifest),'archiveManifestSha256':digest(manifest)})

diagnosis=read(base/'route-constraint-diagnosis1/report.json')
assert digest(base/'route-constraint-diagnostic-v1')==diagnosis['nativeBinarySha256']
for name,sha in diagnosis['implementationHashes'].items():
    assert digest(base/'route-constraint-diagnosis1/sources'/name)==sha
for row in diagnosis['views'].values():
    d=Path(row['sourceDirectory']);policy=read(d/'learned.tsv.policy.json')
    assert digest(d/'learned.tsv.actions.jsonl')==row['traceSha256']
    assert digest(Path(policy['checkpoint']))==row['checkpointSha256']
    assert row['sampledRoots']==row['positiveGainHardRejectedActions']==0
assert sum(r['actionsReplayed'] for r in diagnosis['views'].values())==14666

validations={}
for name in ['separation-dag-validation2','separation-node-validation','separation-overview-validation']:
    data=read(base/name/'report.json');validations[name]=data
    assert data['nativeBinarySha256']==digest(base/'joint-batch-environment-v7')
    for filename,sha in data['implementationHashes'].items():assert digest(base/name/'sources'/filename)==sha
assert (base/'separation-dag-validation/failure.json').is_file()
assert 'No module named' in (base/'individual-separation-port2/workflow.log').read_text()

stages=[];joint_batches=port_actions=updates=0
for name,peak in metrics['stagePeakMiB'].items():
    directory=base/name;individual=name.startswith('individual')
    workflow=read(directory/('workflow.audit.json' if individual else 'workflow.json'))
    audit=read(directory/('individual.audit.json' if individual else 'product.audit.json'))
    stats=read(directory/'learned.tsv.stats.json')
    assert digest(Path(workflow['source']))==workflow['sourceSha256']
    candidate=directory/('candidate.individual.layout.json' if individual else 'candidate.layout.json')
    assert digest(candidate)==audit['candidateSha256']==workflow['candidateSha256']
    assert not stats['hardConditions'] and not stats['individualHardConditions']
    assert not stats['spacing'] and not stats['overlap'] and not audit['spacingViolations']
    assert stats['visual']==audit['visualCrossings']
    assert audit['visualCrossings']>=(1964 if individual else 289)
    if individual:
        assert workflow['allChecksPassed'] and audit['actualProductRendererVerified']
        assert audit['models']==1244 and audit['relations']==1727 and audit['validBoundaryEndpoints']==3454
        assert audit['allSizesAndRelationsPreserved'] and audit['outwardBoundaryEndpointsVerified']
        assert audit['bboxArea']<=1.5e9
    else:
        assert workflow['productFileLoadVerified'] and audit['productCoordinatesPreserved']
        assert audit['canonicalCoverageExactlyOnce'] and audit['canonicalRelationships']==1727
        assert audit['realModels']==1244 and audit['bboxB']<=1.5
    outcomes=collections.Counter();count=0
    if (directory/'joint-batches.jsonl').is_file():
        assert workflow['allChecksPassed'] and workflow['neuralChecksPassed'] and workflow['separationDag']
        assert workflow['frozenWinningGeometryReplayed'] or workflow['unchangedSourceVerified']
        assert workflow['environmentSha256']==digest(base/'joint-batch-environment-v7')
        for filename,sha in workflow['implementationHashes'].items():assert digest(directory/('source-'+filename))==sha
        for filename,sha in {**workflow['inputHashes'],**workflow['verifiedOutputHashes']}.items():assert digest(directory/filename)==sha
        for line in (directory/'joint-batches.jsonl').open():
            row=json.loads(line);count+=1;outcomes[row['result']['reason']]+=1
            assert digest(Path(row['checkpoint']))==row['checkpointSha256']
        assert count==workflow['frozenNetworkBatchesReplayed']==stats['policyActionsEvaluated']
        assert not outcomes['spacing'] and not outcomes['frame']
        if workflow['bestCheckpoint']:assert digest(Path(workflow['bestCheckpoint']))==workflow['bestCheckpointSha256']
        source_visual=workflow['sourceVisual'];joint_batches+=count;updates+=workflow['iterations']
        assert audit['visualCrossings']<=source_visual+workflow['temporaryRegressionLimit']
        kind='joint neural';extra={'newTrainingUpdates':workflow['iterations'],
            'frozenBatchesReplayed':count,'bestCheckpoint':workflow['bestCheckpoint']}
    else:
        policy=read(directory/'learned.tsv.policy.json')
        assert not policy['untrainedControl'] and policy['heuristicSearchCalls']==0
        assert digest(Path(policy['checkpoint']))==policy['checkpointSha256']
        assert workflow.get('nativeBinarySha256',workflow.get('environmentSha256'))==digest(base/'port-environment-v5')
        for field,suffix in [('observationsSha256','.observations.jsonl'),('actionsSha256','.actions.jsonl')]:
            assert digest(directory/('learned.tsv'+suffix))==policy[field]
        for line in (directory/'learned.tsv.actions.jsonl').open():
            row=json.loads(line);count+=1;outcomes[row['result']['reason']]+=1
        assert count==stats['policyActionsEvaluated']
        log=directory/('workflow.log' if individual else 'replay.stdout')
        replay=[json.loads(line) for line in log.read_text().splitlines() if '"frozenCheckpointActionReplay": "pass"' in line]
        assert len(replay)==1 and replay[0]['actions']==count
        source_visual=policy['initial']['visual'];port_actions+=count
        assert audit['visualCrossings']<=source_visual
        kind='frozen endpoint policy';extra={'frozenActionsReplayed':count}
    stages.append({'name':name,'kind':kind,'sourceVisual':source_visual,'savedVisual':audit['visualCrossings'],
        'candidateSha256':audit['candidateSha256'],'outcomes':dict(outcomes),'peakMiB':peak,
        'promoted':False,'independentStableImproved':False,**extra})

files=[];target.mkdir(parents=True)
def copy(path,relative,plain=False):
    assert path.is_file() and not path.is_symlink()
    compressed=not plain and (path.suffix in ['.tsv','.jsonl'] or path.stat().st_size>131072 and path.suffix in ['.json','.log','.cpp'])
    stored=Path(str(relative)+('.gz' if compressed else ''));destination=target/stored
    destination.parent.mkdir(parents=True,exist_ok=True);sha=digest(path)
    with path.open('rb') as src,destination.open('xb') as dst:
        if compressed:
            with gzip.GzipFile(filename='',mode='wb',fileobj=dst,mtime=0,compresslevel=6) as archive:shutil.copyfileobj(src,archive,1024*1024)
        else:shutil.copyfileobj(src,dst,1024*1024)
    if not compressed:destination.chmod(path.stat().st_mode&0o777)
    assert sha==digest(path)==digest(destination,compressed)
    files.append({'source':str(path),'stored':str(stored),'sha256':sha,'gzip':compressed,
        'bytes':path.stat().st_size,'restoredHashVerified':True})

directories=[*metrics['stagePeakMiB'],'route-constraint-diagnosis1','separation-dag-validation',
    'separation-dag-validation2','separation-node-validation','separation-overview-validation','individual-separation-port2']
for directory in directories:
    for path in sorted((base/directory).rglob('*')):
        if path.is_file() and '__pycache__' not in path.parts:copy(path,Path(directory)/path.relative_to(base/directory))
for name in ['separation-dag-run-metrics.json','diagnose_route_constraints.py','validate_separation_dag.py',
    'validate_separation_dag2.py','validate_separation_nodes.py','validate_separation_overview.py','seal_separation_dag_experiments.py']:
    copy(base/name,Path(name))
copy(base/'route-constraint-diagnostic-v1',Path('dependencies/route-constraint-diagnostic-v1'),plain=True)
for name in ['joint_separation_policy.py','joint_neural_ports.py','run_joint_neural_layout.py','diagnose_learned_route_constraints.cpp',
    'run_individual_policy_experiment.py','run_learned_leaf_layout.py','learn_card_policy.py','run_memory_bounded.py']:
    copy(Path('scripts/erd-poc')/name,Path('current-code')/name)
for suffix in ['layout.json','audit.json','provenance.json']:
    path=stable.with_name('captain-ml-independent-views.'+suffix);copy(path,Path('retained-best')/path.name,plain=True)
prior=Path('data/erd-poc/experiments/independent-views-bounded-policy-20261003/manifest.json')
manifest={'scope':'source-order separation DAG neural outputs and canonical/grouped route experiments',
    'overviewTarget':150,'individualTarget':750,'overviewVisual':289,'individualVisual':1964,
    'targetMet':False,'promoted':False,'promotedCandidateChanged':False,'retainedCandidateSha256':expected,
    'noNumericImprovement':True,'jointNeuralBatches':joint_batches,'newNetworkTrainingUpdates':updates,
    'additionalPolicyActions':port_actions,'diagnosticFrozenActionsReplayed':14666,'stages':stages,
    'proposalAuthority':'trained neural outputs; native geometry only decodes and measures',
    'canonicalBaselines':{'individual':2190,'overview':365},
    'bestIsolatedNewLayouts':{'individual':2010,'overview':303},
    'diagnosticLimit':'zero positive-gain hard-rejected actions; hard subtype preview branch was not exercised',
    'decoderLimit':'retains one separating axis per source pair; no proof targets are reachable under this order',
    'gradientLimit':'continuous surrogate verified; cent quantization uses a straight-through estimator',
    'priorArchive':str(prior),'priorArchiveSha256':digest(prior),'dependencies':dependencies,
    'nativeSourceProvenance':'reused joint-batch-v7 comes from global-order archive; current component source snapshots are not its build inputs',
    'validation':validations,'failedValidationPreserved':'separation-dag-validation/failure.json',
    'resourcePolicy':metrics,'actualBrowserVerified':False,'files':files}
assert digest(stable)==expected
(target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'archive':str(target),'manifestSha256':digest(target/'manifest.json'),'stages':len(stages),
    'jointNeuralBatches':joint_batches,'newNetworkTrainingUpdates':updates,'additionalPolicyActions':port_actions,
    'files':len(files),'storedBytes':sum(p.stat().st_size for p in target.rglob('*') if p.is_file())}),flush=True)
