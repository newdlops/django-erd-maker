"""Confirm the promoted bytes and preserve final metadata/documents separately."""
from pathlib import Path
import hashlib
import json
import shutil

root=Path('.tmp/visualcross-ml-150-750-20261004/single-owner-learning1')
def digest(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as s:
        while b:=s.read(65536):h.update(b)
    return h.hexdigest()

seal=json.loads(Path('.tmp/single-owner-learning-archive-seal-receipt.json').read_text())
restore=json.loads(Path('.tmp/single-owner-learning-archive-independent-verification.json').read_text())
promotion=json.loads(Path('.tmp/single-owner-learning-promotion-receipt.json').read_text())
manifest=Path(seal['manifest']);assert digest(manifest)==seal['manifestSha256']==restore['manifestSha256']
assert restore['status']=='pass' and promotion['promoted']
base=Path('data/erd-poc/candidates/captain-ml-independent-views')
layout=Path(str(base)+'.layout.json');audit_path=Path(str(base)+'.audit.json');prov_path=Path(str(base)+'.provenance.json')
assert layout.read_bytes()==(root/'combined-best1/candidate.layout.json').read_bytes()
assert digest(layout)==promotion['candidateSha256']==seal['candidateSha256']
audit=json.loads(audit_path.read_text());provenance=json.loads(prov_path.read_text())
for r in (audit,provenance):
    assert r['candidateSha256']==digest(layout) and (r['overviewVisual'],r['individualVisual'])==(288,1963)
    assert (r['overviewTarget'],r['individualTarget'])==(150,750) and not r['browserVerified']
assert not audit['thresholdsMet'] and not provenance['targetMet']
assert provenance['archiveManifestSha256']==seal['manifestSha256'] and provenance['modelsTrainedInLatestArchive']==1
assert provenance['newNetworkTrainingUpdatesInLatestContinuation']==20512
assert provenance['selectedCheckpointUpdatesInLatestContinuation']==13461
assert provenance['syntheticRewardMeasurementsArchived']==4096 and provenance['trainingRewardMeasurementsArchived']==6144
assert provenance['actualCaptainRewardMeasurementsUsedForTraining']==1615
assert provenance['latestCriticTrainedOnSingleOwnerMoves'] and not provenance['learningCausedCaptainGainEstablished']
assert provenance['individualCriticParentTraining']['checkpointSha256']=='915a84051eedea56c9695a479737706772eef6213ac8b986118ce952c37a7a10'
assert provenance['overviewCriticTraining']['checkpointSha256']=='ef92608e90a15f9d8b467f3a7ee964b900ea57d740da4503f83199d57e107df1'
before=json.loads((root/'promotion-before'/layout.name).read_text());after=json.loads(layout.read_text())
assert before['individualView']==after['individualView']
old={n['modelId']:n for n in before['nodes']};new={n['modelId']:n for n in after['nodes']}
assert old.keys()==new.keys() and len(new)==1244
assert all(old[k]['size']==n['size'] for k,n in new.items())
changed=[k for k,n in new.items() if old[k]['position']!=n['position']]
assert changed==['db.EmployeeStockGuideAudience']
assert len(after['routedEdges'])==1727 and {e['edgeId'] for e in after['routedEdges']}=={e['edgeId'] for e in before['routedEdges']}
overview=json.loads((root/'overview-product1/candidate.layout.json').read_text())
assert after['nodes']==overview['nodes'] and after['routedEdges']==overview['routedEdges'] and after['engineMetadata']==overview['engineMetadata']
context=Path('context.md').read_text();readme=Path('data/erd-poc/candidates/README.md').read_text()
assert 'promoted best 288 / 1,963' in context.splitlines()[2]
assert '개요 288 / 개별 보기 1,963' in readme.split('\n\n')[1]
assert seal['manifestSha256'] in context
assert provenance['resourceLimitMiB']==128 and not provenance['resourceLimitRaised']
result={'status':'pass','candidateSha256':digest(layout),'archiveManifestSha256':seal['manifestSha256'],
 'promotedBytesIdenticalToProductAuditedCandidate':True,'overviewVisual':288,'individualVisual':1963,
 'individualViewExactlyPreserved':True,'overviewChangedModels':changed,'all1244SizesPreserved':True,'all1727RelationshipIdsPreserved':True,
 'overviewCoordinatesAndRoutesExactlyMatchModelAppliedProduct':True,'newTrainingCountersAndViewSpecificCriticLineageVerified':True,
 'resourceLimitMiB':128,'resourceLimitRaised':False,'targetMet':False,'browserVerified':False,'verifierSha256':digest(__file__)}
output=Path('.tmp/single-owner-learning-final-state-verification.json');assert not output.exists();output.write_text(json.dumps(result,indent=2)+'\n')
supplement=manifest.parent/'promotion';supplement.mkdir(exist_ok=False)
files=[layout,audit_path,prov_path,Path('context.md'),Path('data/erd-poc/candidates/README.md'),output,
 Path('.tmp/single-owner-learning-archive-seal-receipt.json'),Path('.tmp/single-owner-learning-archive-independent-verification.json'),
 Path('.tmp/single-owner-learning-promotion-receipt.json'),Path('.tmp/single-owner-learning-archive-seal.guard.log'),
 Path('.tmp/single-owner-learning-archive-verification.guard.log'),Path('.tmp/single-owner-learning-promotion.guard.log'),Path(__file__)]
bindings=[]
for source in files:
    dest=supplement/source.name;assert not dest.exists();shutil.copyfile(source,dest)
    assert digest(source)==digest(dest)
    bindings.append({'source':str(source),'archive':str(dest),'sha256':digest(dest),'bytes':dest.stat().st_size})
record={'baseManifest':str(manifest),'baseManifestSha256':seal['manifestSha256'],'baseArchiveIndependentRestoreStatus':'pass',
 'supplementScope':'post-promotion exact state, metadata, documentation and guard telemetry; base manifest stays immutable',
 'finalVerification':result,'files':bindings,'allSupplementBytesCopyHashVerified':True}
(supplement/'artifact-record.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(result),flush=True)
