"""Seal source-preserving slot models and bounded exact measurement caching."""
import collections
import copy
import gzip
import hashlib
import json
import shutil
from pathlib import Path

base=Path(__file__).parent
target=Path('data/erd-poc/experiments/independent-views-slot-anchor-20261003')
stable=Path('data/erd-poc/candidates/captain-ml-independent-views.layout.json')
expected='da2317d3d9fe72181587f543843bfdde3cc7030ca328e93c4f627ae21ec356e7'
def read(path):return json.loads(path.read_text())
def digest(path,compressed=False):
    result=hashlib.sha256()
    with (gzip.open(path,'rb') if compressed else path.open('rb')) as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):result.update(chunk)
    return result.hexdigest()

assert not target.exists() and digest(stable)==expected
audit=read(stable.with_name('captain-ml-independent-views.audit.json'))
provenance=read(stable.with_name('captain-ml-independent-views.provenance.json'))
assert audit['candidateSha256']==provenance['candidateSha256']==expected
assert (audit['overviewVisual'],audit['individualVisual'])==(289,1964)
validation=read(base/'slot-anchor-validation3/report.json')
assert validation['allChecksPassed'] and len(validation['legacyAnchorActionReplays'])==4
assert digest(Path(validation['completedChecksReusedFrom']))==validation['completedChecksReportSha256']
for name,sha in validation['implementationHashes'].items():
    assert digest(base/'slot-anchor-validation3/sources'/name)==sha
    if name not in ['joint_reward_training.py','run_joint_neural_layout.py']:
        assert digest(Path('scripts/erd-poc')/name)==sha
cache_validation=read(base/'reward-cache-validation1/report.json')
assert cache_validation['allChecksPassed'] and cache_validation['newNativeGeometryEvaluations']==0
for name,sha in cache_validation['sourceHashes'].items():
    assert digest(Path('scripts/erd-poc')/name)==sha
    assert digest(base/'reward-cache-validation1'/('source-'+name))==sha

native_manifest=Path('data/erd-poc/experiments/independent-views-global-order-20261003/manifest.json')
assert digest(native_manifest)=='b9fa65014a331faf993beda3ad6a8241400637bdc7df4072519a47ef2ea07e3b'
binary=base/'joint-batch-environment-v7'
assert digest(binary)==validation['nativeBinarySha256']
native_entry=next(row for row in read(native_manifest)['files'] if row['sha256']==digest(binary))
assert digest(native_manifest.parent/native_entry['stored'],native_entry['gzip'])==digest(binary)
prior=Path('data/erd-poc/experiments/independent-views-pair-contexts-20261003/manifest.json')
assert digest(prior)=='0db970ab5c40b8aae5ff387a5f002ebbd420eeba77fb6bbffb342d8fb9b87004'

specs=[('individual-slot-anchor1',143.8),('overview-slot-anchor1',182.1),
       ('individual-slot-graph1',140.7),('individual-slot-cached1',145.6),
       ('overview-slot-cached1',175.6),('overview-slot-graph1',180.5)]
stages=[]
for name,peak in specs:
    directory=base/name
    individual=name.startswith('individual')
    workflow=read(directory/('workflow.audit.json' if individual else 'workflow.json'))
    product=read(directory/('individual.audit.json' if individual else 'product.audit.json'))
    policy=read(directory/'learned.tsv.policy.json')
    stats=read(directory/'learned.tsv.stats.json')
    assert workflow['allChecksPassed'] and workflow['unchangedSourceVerified'] and not workflow['improvesSource']
    assert workflow['slotPermutation'] and workflow['anchorRays'] and not workflow['trainableEndpointHead']
    assert workflow['sharedSourceEndpoints'] and workflow['exactRewardTraining']
    assert digest(Path(workflow['source']))==workflow['sourceSha256']
    assert digest(Path(product['candidate']))==workflow['candidateSha256']==product['candidateSha256']
    assert product['visualCrossings']==(1964 if individual else 289)
    assert policy['heuristicSearchCalls']==0 and policy['retainedSource'] and not policy['untrainedControl']
    assert stats['acceptedActions']==0 and stats['temporaryRegressionBudget']==0
    for k in ['hardConditions','individualHardConditions','overlap','spacing']:assert stats[k]==0
    for field in ['inputHashes','verifiedOutputHashes']:
        for filename,sha in workflow[field].items():assert digest(directory/filename)==sha
    for filename,sha in workflow['implementationHashes'].items():
        assert digest(directory/('source-'+filename))==sha
    if individual:
        assert product['models']==1244 and product['relations']==1727
        assert product['validBoundaryEndpoints']==3454 and product['bboxArea']<=1.5e9
        assert product['outwardBoundaryEndpointsVerified'] and product['allSizesAndRelationsPreserved']
        assert product['actualProductRendererVerified']
    else:
        assert product['realModels']==1244 and product['canonicalRelationships']==1727
        assert product['canonicalCoverageExactlyOnce'] and product['bboxB']<=1.5
        assert product['productCoordinatesPreserved'] and workflow['productFileLoadVerified']
    assert product['spacingViolations']==0 and not workflow['browserVerified']
    proposals=[json.loads(line) for line in (directory/'joint-batches.jsonl').open()]
    for proposal in proposals:
        assert digest(Path(proposal['checkpoint']))==proposal['checkpointSha256']
        assert not proposal['result']['accepted']
    assert len(proposals)==workflow['frozenNetworkBatchesReplayed']==stats['policyActionsEvaluated']
    measurements=directory/'reward-measurements.jsonl'
    assert digest(measurements)==workflow['rewardTraceSha256']
    records=[json.loads(line) for line in measurements.open()]
    samples=[sample for record in records for direction in record['directions'] for sample in direction['samples']]
    assert len(samples)==workflow['rewardMeasurementsReplayed']
    assert len(records)==workflow['rewardAdamUpdatesReplayed']==workflow['iterations']
    cache_hits=sum(sample.get('cacheHit',False) for sample in samples)
    native_calls=len(samples)-cache_hits
    assert native_calls==workflow.get('rewardNativeEvaluations',len(samples))
    assert cache_hits==workflow.get('rewardMeasurementCacheHits',0)
    results={}
    for sample in samples:
        key=sample['actionSha256']
        if key in results:assert results[key]==sample['result']
        results[key]=sample['result']
    reasons=collections.Counter(s['result']['reason'] for s in samples)
    legal=collections.Counter(s['result']['visual'] for s in samples if s['result']['legal'])
    stages.append({'stage':name,'view':workflow['view'],'trainedUpdates':len(records),
        'logicalRewardProbes':len(samples),'nativeRewardEvaluations':native_calls,
        'reusedMeasurementResults':cache_hits,'distinctWireActions':len(results),
        'cacheSize':workflow.get('rewardMeasurementCacheSize',0),'graphChannels':workflow['graphChannels'],
        'frozenBatchesReplayed':len(proposals),'legalLogicalProbeVisualCounts':dict(legal),
        'logicalProbeReasons':dict(reasons),'savedVisual':product['visualCrossings'],
        'candidateSha256':product['candidateSha256'],'promoted':False,'peakMiB':peak})

comparisons=[]
for view in ['individual','overview']:
    old_dir=base/(view+'-slot-anchor1');new_dir=base/(view+'-slot-cached1')
    old=[json.loads(line) for line in (old_dir/'reward-measurements.jsonl').open()]
    new=[json.loads(line) for line in (new_dir/'reward-measurements.jsonl').open()]
    for a,b in zip(old,new):
        stripped=copy.deepcopy(b);stripped.pop('measurementCacheSize')
        for direction in stripped['directions']:
            for sample in direction['samples']:sample.pop('cacheHit')
        assert a==stripped
    assert len(new)>len(old)
    comparisons.append({'view':view,'sourceStage':old_dir.name,'cachedStage':new_dir.name,
        'originalNativeTrainingPrefixExactlyMatched':len(old),'originalUpdates':len(old),
        'cachedUpdates':len(new),'trainingSecondsLimit':20,
        'limitation':'Elapsed budgets include model forwarding and checkpoint work; total product audit time is outside this training limit.'})

files=[]
target.mkdir(parents=True)
def preserve(path,relative,plain=False):
    assert path.is_file() and not path.is_symlink()
    compressed=not plain and (path.suffix in ['.tsv','.jsonl'] or
        path.stat().st_size>131072 and path.suffix in ['.json','.log','.cpp'])
    stored=Path(str(relative)+('.gz' if compressed else ''))
    destination=target/stored;destination.parent.mkdir(parents=True,exist_ok=True)
    sha=digest(path)
    with path.open('rb') as src,destination.open('xb') as dst:
        if compressed:
            with gzip.GzipFile(filename='',mode='wb',fileobj=dst,mtime=0,compresslevel=1) as archive:
                shutil.copyfileobj(src,archive,1024*1024)
        else:shutil.copyfileobj(src,dst,1024*1024)
    if not compressed:destination.chmod(path.stat().st_mode&0o777)
    assert sha==digest(path)==digest(destination,compressed)
    files.append({'source':str(path),'stored':str(stored),'sha256':sha,'gzip':compressed,
        'bytes':path.stat().st_size,'restoredHashVerified':True})

for name in ['slot-anchor-validation1','slot-anchor-validation2','slot-anchor-validation3',
             'reward-cache-validation1']+[name for name,peak in specs]:
    for path in sorted((base/name).rglob('*')):
        if path.is_file() and '__pycache__' not in path.parts:preserve(path,Path(name)/path.relative_to(base/name))
for name in ['validate_slot_anchor_policy.py','validate_reward_measurement_cache.py','seal_slot_anchor_experiments.py']:
    preserve(base/name,Path(name))
preserve(binary,Path('dependencies')/binary.name,plain=True)
for suffix in ['layout.json','audit.json','provenance.json']:
    path=stable.with_name('captain-ml-independent-views.'+suffix)
    preserve(path,Path('retained-best')/path.name,plain=True)
totals={key:sum(row[key] for row in stages) for key in ['trainedUpdates','logicalRewardProbes',
    'nativeRewardEvaluations','reusedMeasurementResults','frozenBatchesReplayed']}
assert totals=={'trainedUpdates':475,'logicalRewardProbes':3672,'nativeRewardEvaluations':817,
               'reusedMeasurementResults':2855,'frozenBatchesReplayed':120}
manifest={'scope':'learned equal-size card-slot permutations with source-preserving interior-anchor endpoints and opt-in exact reward caching',
    'overviewTarget':150,'individualTarget':750,'overviewVisual':289,'individualVisual':1964,
    'targetMet':False,'promotedCandidateChanged':False,'retainedCandidateSha256':expected,
    'proposalAuthority':'trained shared neural head; source-bound deterministic sorting and endpoint decoding; no coordinate search or repair',
    'stages':stages,'totals':totals,'slotAnchorValidation':validation,'cacheValidation':cache_validation,
    'actualCachedTrainingComparisons':comparisons,
    'cacheContract':'Per trainer, full identical MEASURE wire text and immutable source; bounded LRU, never TRY or persistent state-dependent acceptance.',
    'nativeCallsAccounting':'817 actual training MEASURE calls; 2855 additional logical probes reuse their exact results. 120 uncached TRY submissions are counted separately. Validation geometry fixtures are separate.',
    'failedValidationAttempts':[
        {'directory':'slot-anchor-validation1','cause':'bitwise floating equality at zero endpoints exposed max 7.105427357601002e-15 cancellation; actual quantized endpoint tolerance is 1e-8'},
        {'directory':'slot-anchor-validation2','cause':'raw lexicographic rectangle sorting mispaired equal-x points after last-bit cancellation; coordinate-key matching preserved the same 1e-8 pairwise tolerance'}],
    'resourcePolicy':{'serialized':True,'mathThreads':1,'nice':10,'processGroupRssGuardMiB':256,
        'hardCpuPercentageQuota':False,'trainingSecondsLimitPerStage':20,
        'slotValidationAttemptPeaksMiB':[52.7,64.3,56.6],'cacheValidationPeakMiB':54.2,
        'nativeRecompiled':False},
    'nativeBuildArchive':str(native_manifest),'nativeBuildArchiveSha256':digest(native_manifest),
    'nativeBuildSourceCaveat':'The v7 binary build inputs are in the global-order archive; later component source snapshots are not its build inputs.',
    'priorArchive':str(prior),'priorArchiveSha256':digest(prior),
    'nextResearch':'Retain the exact cache for compatible immutable-source reward jobs. Do not repeat these slot/head stages unchanged: extra updates and graph features produced no improvement. Investigate learned coordinated graph reorderings or endpoint changes that escape the current valid-layout plateau.',
    'globalTargetImpossibleProved':False,'actualBrowserVerified':False,'files':files}
assert digest(stable)==expected
with (target/'manifest.json').open('x') as stream:stream.write(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'archive':str(target),'manifestSha256':digest(target/'manifest.json'),
    'files':len(files),'storedBytes':sum(p.stat().st_size for p in target.rglob('*') if p.is_file()),'totals':totals}),flush=True)
