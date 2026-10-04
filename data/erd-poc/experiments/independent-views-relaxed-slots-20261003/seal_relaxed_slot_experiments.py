"""Preserve both relaxation versions, actual trials, and diagnostic evidence."""
import collections,gzip,hashlib,json,shutil,stat
from pathlib import Path
base=Path(__file__).parent
target=Path('data/erd-poc/experiments/independent-views-relaxed-slots-20261003')
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
validations=[]
for name in ['relaxed-slot-validation1','relaxed-slot-validation2']:
    r=read(base/name/'report.json');assert r['allChecksPassed']
    assert sum(x['finiteDifferences'] for x in r['assignmentChecks'])==23
    assert sum(len(v['actionDerivatives']) for v in r['views'].values())==22
    assert sum(len(v['geometryDerivatives']) for v in r['views'].values())==6
    for filename,sha in r['implementationHashes'].items():
        assert digest(base/name/'sources'/filename)==sha
        if name.endswith('2'):assert digest(Path('scripts/erd-poc')/filename)==sha
    validations.append(r)
assert len(validations[1]['legacyRelaxedOutputsReplayed'])==4
gap1=read(base/'slot-relaxation-gap1/report.json')
gap2=read(base/'slot-relaxation-gap2/report.json')
assert gap1['sourceSha256']==validations[0]['implementationHashes']['joint_relaxed_slot_policy.py']
assert gap2['sourceSha256']==validations[1]['implementationHashes']['joint_relaxed_slot_policy.py']
assert gap2['hardInferenceUnchanged'] and not gap2['newTraining'] and gap2['newNativeMeasurements']==0
for view in ['individual','overview']:
    row=next(row for row in gap2['views'][view]['results'] if row['version']==2 and row['temperature']==.05)
    assert row['maximumNodePositionGap']<.001
coverage=read(base/'slot-context-coverage1/report.json')
for view,expected_fixed in [('individual',6),('overview',4)]:
    r=coverage['views'][view]
    assert r['initialCountMatchesNative'] and r['fixedVisualConflicts']==expected_fixed
    assert not r['targetUnreachableWithThisActionSpaceAlone']
    assert digest(Path(r['witnessFile']))==r['witnessSha256']

prior=Path('data/erd-poc/experiments/independent-views-slot-anchor-20261003/manifest.json')
assert digest(prior)=='5625bc792b0c4595909682225f7cc5d06d2f18b019c1d826114e184f6012ca16'
native_manifest=Path('data/erd-poc/experiments/independent-views-global-order-20261003/manifest.json')
assert digest(native_manifest)=='b9fa65014a331faf993beda3ad6a8241400637bdc7df4072519a47ef2ea07e3b'
binary=base/'joint-batch-environment-v7'
assert digest(binary)=='e2a58da4e295d6fd26f9473d1ff371f7cea7ad7e4685b786cefa3838b58d3082'
native_entry=next(row for row in read(native_manifest)['files'] if row['sha256']==digest(binary))
assert digest(native_manifest.parent/native_entry['stored'],native_entry['gzip'])==digest(binary)

specs=[('individual-relaxed-slot1',210.1),('overview-relaxed-slot1',176.9),
       ('individual-relaxed-free1',224.9),('overview-relaxed-free1',182.3),
       ('individual-sorted-relaxed1',232.6),('overview-sorted-relaxed1',182.2)]
stages=[]
for name,peak in specs:
    directory=base/name;individual=name.startswith('individual')
    workflow=read(directory/('workflow.audit.json' if individual else 'workflow.json'))
    product=read(directory/('individual.audit.json' if individual else 'product.audit.json'))
    stats=read(directory/'learned.tsv.stats.json');policy=read(directory/'learned.tsv.policy.json')
    assert workflow['allChecksPassed'] and workflow['unchangedSourceVerified'] and not workflow['improvesSource']
    assert workflow['slotPermutation'] and workflow['slotRelaxation'] and workflow['anchorRays']
    assert not workflow['exactRewardTraining'] and not workflow['quantizedLoss']
    assert workflow['sharedSourceEndpoints'] and not workflow['trainableEndpointHead']
    assert workflow['parameterCount']==6273 and workflow['visualLossChunkSize']==4096
    assert digest(Path(workflow['source']))==workflow['sourceSha256']
    assert digest(Path(product['candidate']))==workflow['candidateSha256']==product['candidateSha256']
    assert product['visualCrossings']==(1964 if individual else 289)
    assert policy['heuristicSearchCalls']==0 and policy['retainedSource'] and not policy['untrainedControl']
    assert stats['acceptedActions']==0 and stats['temporaryRegressionBudget']==0
    for key in ['hardConditions','individualHardConditions','overlap','spacing']:assert stats[key]==0
    for field in ['inputHashes','verifiedOutputHashes']:
        for filename,sha in workflow[field].items():assert digest(directory/filename)==sha
    for filename,sha in workflow['implementationHashes'].items():
        assert digest(directory/('source-'+filename))==sha
    if individual:
        assert product['models']==1244 and product['relations']==1727
        assert product['validBoundaryEndpoints']==3454 and product['bboxArea']<=1.5e9
        assert product['allSizesAndRelationsPreserved'] and product['outwardBoundaryEndpointsVerified']
        assert product['actualProductRendererVerified']
    else:
        assert product['realModels']==1244 and product['canonicalRelationships']==1727
        assert product['canonicalCoverageExactlyOnce'] and product['bboxB']<=1.5
        assert product['productCoordinatesPreserved'] and workflow['productFileLoadVerified']
    assert product['spacingViolations']==0 and not workflow['browserVerified']
    proposals=[json.loads(line) for line in (directory/'joint-batches.jsonl').open()]
    assert len(proposals)==workflow['frozenNetworkBatchesReplayed']==stats['policyActionsEvaluated']
    zero_count=2*(len((directory/'nodes.tsv').read_text().splitlines())+len((directory/'individual.edges.tsv').read_text().splitlines()))
    zero_hash=hashlib.sha256(('TRY '+' '.join(['0']*zero_count)).encode()).hexdigest()
    for row in proposals:
        assert digest(Path(row['checkpoint']))==row['checkpointSha256']
        assert row['result']['legal'] and not row['result']['accepted']
    stages.append({'stage':name,'view':workflow['view'],'trainedUpdates':workflow['iterations'],
        'slotRelaxationVersion':workflow.get('slotRelaxationVersion',1),
        'effectiveSpacingWeight':workflow.get('effectiveSpacingWeight',workflow['spacingWeight']),
        'frozenHardBatchesReplayed':len(proposals),'identityOutputBatches':sum(row['actionSha256']==zero_hash for row in proposals),
        'nativeVisualCounts':dict(collections.Counter(row['result']['visual'] for row in proposals)),
        'allNativeProposalsLegal':True,'minimumRelaxedTrainingLoss':min(row['preUpdateProxy']['total'] for row in workflow['history']),
        'savedVisual':product['visualCrossings'],'candidateSha256':product['candidateSha256'],
        'promoted':False,'peakMiB':peak})
assert sum(s['trainedUpdates'] for s in stages)==1019
assert sum(s['frozenHardBatchesReplayed'] for s in stages)==262

files=[];identical={};target.mkdir(parents=True)
def preserve(path,relative,plain=False):
    assert path.is_file() and not path.is_symlink()
    compressed=not plain and (path.suffix in ['.tsv','.jsonl'] or
        path.stat().st_size>131072 and path.suffix in ['.json','.log','.cpp'])
    stored=Path(str(relative)+('.gz' if compressed else ''));destination=target/stored
    destination.parent.mkdir(parents=True,exist_ok=True)
    sha=digest(path);mode=stat.S_IMODE(path.stat().st_mode);key=(sha,compressed,mode)
    shared=identical.get(key)
    if shared is not None:
        # Link only copies inside this new archive, never mutable source files.
        destination.hardlink_to(shared)
    else:
        with path.open('rb') as src,destination.open('xb') as dst:
            if compressed:
                with gzip.GzipFile(filename='',mode='wb',fileobj=dst,mtime=0,compresslevel=1) as archive:
                    shutil.copyfileobj(src,archive,1024*1024)
            else:shutil.copyfileobj(src,dst,1024*1024)
        destination.chmod(mode);identical[key]=destination
    assert sha==digest(path)==digest(destination,compressed)
    files.append({'source':str(path),'stored':str(stored),'sha256':sha,'gzip':compressed,
        'bytes':path.stat().st_size,'restoredHashVerified':True,
        'sameArchiveHardlinkTo':str(shared.relative_to(target)) if shared is not None else None})

for name in ['relaxed-slot-validation1','relaxed-slot-validation2','slot-relaxation-gap1',
             'slot-relaxation-gap2','slot-context-coverage1']+[name for name,peak in specs]:
    for path in sorted((base/name).rglob('*')):
        if path.is_file() and '__pycache__' not in path.parts:preserve(path,Path(name)/path.relative_to(base/name))
for name in ['validate_relaxed_slot_policy.py','validate_relaxed_slot_policy_v2.py',
             'diagnose_slot_relaxation_gap.py','compare_slot_relaxation_versions.py',
             'analyze_slot_context_coverage.py','seal_relaxed_slot_experiments.py']:
    preserve(base/name,Path(name))
preserve(binary,Path('dependencies')/binary.name,True)
for suffix in ['layout.json','audit.json','provenance.json']:
    path=stable.with_name('captain-ml-independent-views.'+suffix)
    preserve(path,Path('retained-best')/path.name,True)
manifest={'scope':'finite differentiable source-slot training and correction of hard/soft assignment mismatch',
    'overviewTarget':150,'individualTarget':750,'overviewVisual':289,'individualVisual':1964,
    'targetMet':False,'promotedCandidateChanged':False,'retainedCandidateSha256':expected,
    'newNetworkTrainingUpdates':1019,'frozenHardBatchesReplayed':262,
    'allNativeProposalsLegal':True,'actualTrainingRewardMeasureCalls':0,
    'proposalAuthority':'trained shared neural weights; unchanged hard slot sorting and source-anchor endpoints; no coordinate search or repair',
    'stages':stages,'validations':validations,'finiteDerivativeChecks':102,
    'relaxationGapBefore':gap1,'sameWeightRelaxationComparison':gap2,'fixedSupportAnalysis':coverage,
    'reference':'https://arxiv.org/abs/1802.08665',
    'referenceScope':'continuous permutation relaxation background; custom deterministic scalar-score model, not a reproduction or performance claim',
    'resourcePolicy':{'serialized':True,'mathThreads':1,'nice':10,'processGroupRssGuardMiB':256,
        'hardCpuPercentageQuota':False,'trainingSecondsLimitPerStage':20,'laterReplayAndProductAuditOutsideTrainingLimit':True,
        'validationPeaksMiB':[176.5,183.8],'coveragePeakMiB':44.3,'gapAnalysisPeaksMiB':[91.6,109.2],
        'maximumStoredPairEntries':262144,'activeLossChunkSize':4096,'nativeRecompiled':False},
    'validationScope':'51 finite differences per version; actual view source outputs, loading and replay. These do not differentiate hard assignments or prove target attainability.',
    'v1Limitation':'Eight normalizations toward fixed uniform ranks can keep a large soft/hard gap even at lower temperatures. Version 1 is retained explicitly for reproducibility.',
    'v2Change':'Kernel columns use sorted current score values and differentiate those values locally. At temperature .05, the measured same-weight gaps fell below .001 scene units; arbitrary nearly tied scores remain a limitation.',
    'comparisonCaveat':'Wall-clock temperature schedules differ; stage results are not a controlled causal performance ablation. Gap comparison uses exactly identical weights.',
    'nextResearch':'Do not repeat these six stages unchanged. All 262 hard proposals were legal and most were identity outputs; improve learned nonlocal proposal diversity or its hard-objective training signal, rather than weaken validity checks. Fixed slot support alone does not rule out either target.',
    'globalTargetImpossibleProved':False,'actualBrowserVerified':False,
    'nativeBuildArchive':str(native_manifest),'nativeBuildArchiveSha256':digest(native_manifest),
    'nativeBuildSourceCaveat':'Later component source snapshots are not build inputs to native v7; actual build inputs remain in the global-order archive.',
    'priorArchive':str(prior),'priorArchiveSha256':digest(prior),'files':files}
assert digest(stable)==expected
with (target/'manifest.json').open('x') as stream:stream.write(json.dumps(manifest,indent=2)+'\n')
unique={}
for path in target.rglob('*'):
    if path.is_file():
        info=path.stat();unique[(info.st_dev,info.st_ino)]=info.st_size
print(json.dumps({'archive':str(target),'manifestSha256':digest(target/'manifest.json'),
    'files':len(files),'logicalStoredBytes':sum(p.stat().st_size for p in target.rglob('*') if p.is_file()),
    'uniqueStoredBytes':sum(unique.values()),'sameArchiveHardlinks':sum(row['sameArchiveHardlinkTo'] is not None for row in files),
    'updates':1019,'frozenHardBatches':262}),flush=True)
