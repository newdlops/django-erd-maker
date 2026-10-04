"""Scope inherited counters to the new continuation without losing history."""
from pathlib import Path
import hashlib
import json
import os
import shutil

sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
base=Path('data/erd-poc/candidates/captain-ml-independent-views')
provenance=Path(str(base)+'.provenance.json');layout=Path(str(base)+'.layout.json')
archive=Path('data/erd-poc/experiments/independent-views-single-owner-20261004')
doc=json.loads(provenance.read_text());assert doc['candidateSha256']==sha(layout)=='7aa76f957e12361b8e72add0e12e08d97494d78cc087309d9d15448f237cfcda'
parent=Path('data/erd-poc/experiments/independent-views-owner-amplitudes-20261004-v2/manifest.json')
training_path=Path('.tmp/visualcross-ml-150-750-20261004/owner-amplitude-cycles1/training2/training.json')
training=json.loads(training_path.read_text());assert training['checkpointSha256']==doc['latestFrozenCriticCheckpointSha256']
captain_training=training['rowsByKind']['captain']-training['captainValidationRows'];assert captain_training==4513
parent_doc=json.loads(parent.read_text())
items=parent_doc['files']+parent_doc['referencedFiles']
assert any(i['source']==str(training_path) and i['sha256']==sha(training_path) for i in items)
supplement=archive/'promotion';supplement.mkdir(exist_ok=False)
shutil.copyfile(provenance,supplement/'provenance.before-reconciliation.json')
shutil.copyfile(Path(__file__),supplement/'reconcile_single_owner_promotion_metadata.py')
legacy={key:doc[key] for key in ('acceptedGraphBranchActions','syntheticRewardMeasurementsArchived','graphBranchMode','actualCaptainRewardMeasurementsUsedForTraining')}
changes={'acceptedGraphBranchActions':0,'syntheticRewardMeasurementsArchived':0,'graphBranchMode':'none',
    'trainingMeasurementCountsScope':'latest single-owner continuation only; zero new training updates',
    'newNetworkTrainingUpdatesInLatestContinuation':0,'inheritedBoundedPolicyCounters':legacy,
    'individualCriticParentTraining':{'checkpointSha256':training['checkpointSha256'],'kind':training['kind'],
        'sourceArchiveManifest':str(parent),'sourceArchiveManifestSha256':sha(parent),
        'trainingReportSha256':sha(training_path),'datasetRowsByKind':training['rowsByKind'],
        'trainingRows':training['trainingRows'],'captainDatasetRows':training['rowsByKind']['captain'],
        'captainValidationRows':training['captainValidationRows'],'captainTrainingRows':captain_training,
        'newTrainingUpdatesExecutedInParent':training['newTrainingUpdatesExecuted'],
        'selectedCheckpointUpdatesInParent':training['selectedCheckpointUpdates'],
        'trainedOnSingleOwnerMoves':False}}
record={'kind':'single-owner-promotion-provenance-counter-scope-reconciliation-v1',
    'candidateSha256':doc['candidateSha256'],'candidateGeometryChanged':False,'mainArchiveManifestSha256':doc['archiveManifestSha256'],
    'provenanceBeforeSha256':sha(provenance),'changes':changes,'toolSha256':sha(Path(__file__))}
record_path=supplement/'metadata-reconciliation.json';record_path.write_text(json.dumps(record,indent=2)+'\n')
doc.update(changes);doc['metadataReconciliationRecord']=str(record_path);doc['metadataReconciliationRecordSha256']=sha(record_path)
temporary=provenance.with_name(provenance.name+'.reconciling')
with temporary.open('x') as stream:stream.write(json.dumps(doc,indent=2)+'\n')
os.replace(temporary,provenance);shutil.copyfile(provenance,supplement/'provenance.after-reconciliation.json')
assert sha(layout)==record['candidateSha256'] and sha(provenance)==sha(supplement/'provenance.after-reconciliation.json')
observed=json.loads(provenance.read_text());assert all(observed[k]==v for k,v in changes.items())
assert sha(record_path)==observed['metadataReconciliationRecordSha256']
receipt={'metadataReconciled':True,'candidateGeometryChanged':False,'candidateSha256':sha(layout),
    'provenanceSha256':sha(provenance),'reconciliationRecordSha256':sha(record_path),'captainParentTrainingRows':captain_training}
Path('.tmp/single-owner-metadata-reconciliation-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt),flush=True)
