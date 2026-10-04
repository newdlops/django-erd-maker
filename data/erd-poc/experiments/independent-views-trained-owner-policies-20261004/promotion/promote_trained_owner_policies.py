"""Promote independently restored 286/1963 and retain scoped model history."""
import hashlib
import json
import os
from pathlib import Path
sha=lambda data:hashlib.sha256(data).hexdigest()
root=Path('.tmp/visualcross-ml-150-750-20261004/single-owner-trained-walk1')
receipt=json.loads(Path('.tmp/trained-owner-archive-seal-receipt.json').read_text())
verified=json.loads(Path('.tmp/trained-owner-archive-independent-verification.json').read_text())
p=Path(receipt['manifest']);assert sha(p.read_bytes())==receipt['manifestSha256']==verified['manifestSha256'] and verified['status']=='pass'
m=json.loads(p.read_text());base=Path('data/erd-poc/candidates/captain-ml-independent-views')
paths={key:Path(str(base)+'.'+key+'.json') for key in ('layout','audit','provenance')}
for path in paths.values():assert path.read_bytes()==(root/'promotion-before'/path.name).read_bytes()
assert sha(paths['layout'].read_bytes())==m['previousCandidateSha256']
candidate=Path(m['candidateSource']).read_bytes();assert sha(candidate)==m['candidateSha256']
before=json.loads(paths['layout'].read_bytes());after=json.loads(candidate)
assert before['individualView']==after['individualView']
old={n['modelId']:n for n in before['nodes']};new={n['modelId']:n for n in after['nodes']}
assert old.keys()==new.keys() and len(new)==1244 and all(n['size']==old[k]['size'] for k,n in new.items())
changed=[k for k,n in new.items() if n['position']!=old[k]['position']];assert len(changed)==1
assert len(after['routedEdges'])==1727 and {e['edgeId'] for e in after['routedEdges']}=={e['edgeId'] for e in before['routedEdges']}
assert before['engineMetadata']['leafBundles']==after['engineMetadata']['leafBundles']
audit=json.loads((root/'combined-best1/audit.json').read_text());assert audit==m['productAudit']
assert (audit['overviewVisual'],audit['individualVisual'])==(286,1963) and not audit['thresholdsMet']
for k in ('actualProductFileLoadVerified','completeIndividualGeometryPreserved','browserRouteFunctionParity','roundTripViewSwitchVerified'):assert audit[k]
previous=json.loads(paths['provenance'].read_text());assert previous['candidateSha256']==m['previousCandidateSha256']
overview=json.loads((root/'overview-product1/product.audit.json').read_text());peak=next(x['peakMiB'] for x in m['resources']['logs'] if x['path'].endswith('/combined-product.guard.log'))
audit.update(candidate=str(paths['layout']),promoted=True,viewSpecificGeometry=True,browserVerified=False,
             overviewSpacingViolations=0,overviewBboxArea=overview['bboxB']*1e9,dimensionsPreserved=True,canonicalRelationshipsPreserved=True,nativeHardConditions=0)
history=list(previous['subsequentResearchArchives']);history.append({'archiveManifest':str(p),'archiveManifestSha256':receipt['manifestSha256'],
 'promotedCandidateChanged':True,'previousCandidateSha256':m['previousCandidateSha256'],'candidateSha256':m['candidateSha256'],
 'overviewVisual':286,'individualVisual':1963,'nativeModelActions':2771,'newNetworkTrainingUpdates':0,
 'ordinaryWalkActions':82,'cachedWalkActions':129,'batchedActions':512,'globalActions':2048,
 'readOnlyFullScoringComparisons':133,'fullNativeFeatureControls':88,'globalStrictImprovements':1,
 'learningCausedCaptainGainEstablished':False,'targetMet':False,'browserVerified':False})
provenance={**previous,'candidateSha256':m['candidateSha256'],'previousCandidateSha256':m['previousCandidateSha256'],
 'overviewVisual':286,'individualVisual':1963,'archiveManifest':str(p),'archiveManifestSha256':receipt['manifestSha256'],
 'priorArchives':list(dict.fromkeys(previous['priorArchives']+[Path(previous['archiveManifest']).parent.name])),
 'subsequentResearchArchives':history,'stagesInLatestArchive':8,'sourceStagesUsedForLatestTraining':0,
 'additionalActionsArchived':2771,'modelsTrainedInLatestArchive':0,'coordinatedNetworkBatchesArchived':0,
 'trainingRewardMeasurementsArchived':0,'syntheticRewardMeasurementsArchived':0,'actualCaptainRewardMeasurementsUsedForTraining':0,
 'newCaptainInferenceMeasurementsNotUsedForTraining':2771,
 'trainingMeasurementCountsScope':'latest trained-owner policy continuation: inference only; zero new optimizer updates; selected critic training belongs to the prior single-owner-learning archive',
 'newNetworkTrainingUpdatesInLatestContinuation':0,'selectedCheckpointUpdatesInLatestContinuation':0,
 'latestFrozenCriticCheckpointSha256':m['frozenTrainedCriticSha256'],'latestCriticTrainedOnSingleOwnerMoves':True,
 'overviewSource':audit['overviewSource'],'individualSource':audit['individualSource'],
 'neuralTranslationDecoder':'view-specific: trained single-owner critic samples global all-size directions with conflicting isolated cards retained; previous individual geometry preserved; Native only decodes and accepts/rejects',
 'neuralTranslationSchema':'owner-amplitude-cycle-mean-moment-v1-3x141','overviewNeuralTranslationSchema':'owner-amplitude-cycle-mean-moment-v1-3x141',
 'validationEvidence':str(root/'verification/static.audit.json'),'trajectoryValidationEvidence':str(root/'verification/walks.audit.json'),
 'exactCachedObserverExecuted':True,'cachedModelInputStateNativeControls':82,'allCachedModelInputsNativeVerifiedExactly':True,
 'observerSourceSha256':sha(Path('scripts/erd-poc/single_owner_cached_observer_v2.py').read_bytes()),
 'productAuditPeakMiB':peak,'resourceLimitMiB':128,'resourceLimitRaised':False,'nativeCoordinateSearchOrRepairAdded':False,
 'outwardBoundaryContractVerified':True,'causalAblationVerified':False,'learningCausedCaptainGainEstablished':False,
 'winningNeuralScore':m['diagnosis']['winningNeuralScore'],'winnerSampledViaNeuralWeightedExploration':True,
 'overviewTarget':150,'individualTarget':750,'targetMet':False,'browserVerified':False,
 'latestResearchArchiveIndependentVerification':'.tmp/trained-owner-archive-independent-verification.json'}
if 'latestSingleOwnerDataset' in provenance:provenance['inheritedSingleOwnerTrainingDataset']=provenance.pop('latestSingleOwnerDataset')
if 'overviewCriticTraining' in provenance:provenance['overviewCriticParentTraining']=provenance.pop('overviewCriticTraining')
provenance['overviewCriticParentTraining'].update(sourceArchiveManifest=previous['archiveManifest'],sourceArchiveManifestSha256=previous['archiveManifestSha256'],newUpdatesInThisContinuation=0)
pending=[]
for key,value in (('layout',candidate),('audit',(json.dumps(audit,indent=2)+'\n').encode()),('provenance',(json.dumps(provenance,indent=2)+'\n').encode())):
    path=paths[key];tmp=path.with_name(path.name+'.promoting')
    with tmp.open('xb') as s:s.write(value)
    pending.append((tmp,path))
for tmp,path in pending:os.replace(tmp,path)
assert sha(paths['layout'].read_bytes())==m['candidateSha256']
result={'promoted':True,'overviewVisual':286,'individualVisual':1963,'candidateSha256':m['candidateSha256'],
 'previousCandidatePreserved':True,'individualViewExactlyPreserved':True,'overviewMovedModels':changed,
 'sizesAnd1727RelationshipsPreserved':True,'archiveManifestSha256':receipt['manifestSha256'],'targetMet':False,'browserVerified':False}
out=Path('.tmp/trained-owner-promotion-receipt.json');assert not out.exists();out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
