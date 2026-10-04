"""Seal exact bounded-output evidence before promoting the verified best."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import shutil

base=Path('.tmp/visualcross-ml-150-750-20261003')
target=Path('data/erd-poc/experiments/independent-views-bounded-policy-20261003')
prior=Path('data/erd-poc/experiments/independent-views-cut-policy-20261003')
stable=Path('data/erd-poc/candidates/captain-ml-independent-views.layout.json')
old_sha='86d3d2f057add7bde5df0ebb720120fb524fe380ff08521c8f0a95be658d82c7'
metrics=json.loads((base/'bounded-policy-run-metrics.json').read_text())

def digest(path,compressed=False):
    result=hashlib.sha256()
    with (gzip.open(path,'rb') if compressed else path.open('rb')) as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):result.update(chunk)
    return result.hexdigest()

assert digest(stable)==old_sha and not target.exists()
combined=json.loads((base/'combined-bounded-verified/audit.json').read_text())
assert digest(base/'combined-bounded-verified/candidate.layout.json')==combined['candidateSha256']
assert combined['overviewVisual']<291 and combined['individualVisual']<1966
assert (combined['overviewTarget'],combined['individualTarget'])==(150,750)
assert not combined['thresholdsMet']
for key in ['actualProductFileLoadVerified','completeIndividualGeometryPreserved','browserRouteFunctionParity','roundTripViewSwitchVerified']:
    assert combined[key]
assert combined['models']==1244 and combined['canonicalRelationships']==1727 and not combined['individualSpacingViolations']
result=json.loads((base/'bounded-policy-validation/report.json').read_text())
assert result['nativeBinarySha256']==digest(base/'component-environment-v26')
for name,sha in result['implementationHashes'].items():
    assert digest(base/'bounded-policy-validation/sources'/name)==sha
    assert digest(Path('scripts/erd-poc')/name)==sha

dependencies=[]
for name in ['port-environment-v5','leaf-component-policy-v11.npz']:
    path=Path('data/erd-poc/experiments/independent-views-node-port-20261003/dependencies')/name
    local=Path('data/erd-poc/checkpoints')/name if name.endswith('.npz') else base/name
    assert digest(path)==digest(local)
    dependencies.append({'file':str(path),'sha256':digest(path)})
stages=[];total_actions=grouped_actions=0
for name,peak in metrics['stagePeakMiB'].items():
    directory=base/name;overview=name.startswith('overview')
    workflow=json.loads((directory/('workflow.json' if overview else 'workflow.audit.json')).read_text())
    audit=json.loads((directory/('product.audit.json' if overview else 'individual.audit.json')).read_text())
    policy=json.loads((directory/'learned.tsv.policy.json').read_text())
    stats=json.loads((directory/'learned.tsv.stats.json').read_text())
    binary=base/('component-environment-v26' if workflow.get('branchMoves') else 'port-environment-v5')
    assert workflow.get('nativeBinarySha256',workflow.get('environmentSha256'))==digest(binary)
    assert digest(Path(audit['candidate']))==audit['candidateSha256']==workflow['candidateSha256']
    assert audit['spacingViolations']==0 and stats['hardConditions']==stats['individualHardConditions']==0
    assert audit['visualCrossings']==stats['visual']<=policy['initial']['visual']
    assert digest(Path(workflow['source']))==workflow['sourceSha256']
    assert not policy['untrainedControl'] and policy['heuristicSearchCalls']==0
    assert digest(Path(policy['checkpoint']))==policy['checkpointSha256']
    if overview:
        assert workflow['productFileLoadVerified'] and workflow['frozenActionReplay']
        assert audit['bboxB']<=1.5 and audit['canonicalCoverageExactlyOnce'] and audit['canonicalRelationships']==1727
        log=directory/'replay.stdout'
    else:
        assert workflow['allChecksPassed'] and audit['actualProductRendererVerified']
        assert audit['outwardBoundaryEndpointsVerified'] and audit['allSizesAndRelationsPreserved']
        assert audit['bboxArea']<=1.5e9 and audit['models']==1244 and audit['relations']==1727
        log=directory/'workflow.log'
    replay=[json.loads(line) for line in log.read_text().splitlines() if '"frozenCheckpointActionReplay": "pass"' in line]
    assert len(replay)==1 and replay[0]['actions']==stats['policyActionsEvaluated']
    for field,suffix in [('observationsSha256','.observations.jsonl'),('actionsSha256','.actions.jsonl')]:
        assert digest(directory/('learned.tsv'+suffix))==policy[field]
    ids=[line.split('\t')[0] for line in (directory/'nodes.tsv').read_text().splitlines()]
    groups={node:[node] for node in ids}
    if workflow.get('branchMoves'):
        mapping=json.loads((directory/'branch-map.json').read_text())
        assert mapping['sourceSha256']==workflow['sourceSha256'] and not mapping['positionsProposed']
        assert digest(directory/'branches.tsv')==mapping['branchMapSha256']
        for filename,sha in {**mapping['inputHashes'],**policy['branchInputHashes']}.items():assert digest(directory/filename)==sha
        groups={row[0]:row[1:] for row in (line.split('\t') for line in (directory/'branches.tsv').read_text().splitlines())}
        assert replay[0]['branchPositionsAndRoutesReplay'] and replay[0]['boundedLatentAndTranslationReplay'] and stats['boundedMoves']
        assert mapping['branchMode']==workflow['branchMode']==policy['branchMode']==stats['branchMode']=='cut'
    reasons=collections.Counter();group_reasons=collections.Counter();wins=[];count=0
    for line in (directory/'learned.tsv.actions.jsonl').open():
        row=json.loads(line);count+=1;result=row['result'];reasons[result['reason']]+=1
        detail={'actionIndex':count,'entityIndex':row['node'],'gain':result['gain']}
        if workflow.get('branchMoves'):
            key=ids[row['node']];cards=len(groups[key]);detail.update(root=key,cards=cards)
            if cards>1:group_reasons[result['reason']]+=1
        if result['accepted'] and result['gain']>0:wins.append(detail)
    assert sum(r['gain'] for r in wins)==policy['initial']['visual']-stats['visual']
    if stats.get('boundedMoves'):assert not reasons['spacing'] and not reasons['frame']
    assert count==stats['policyActionsEvaluated'];total_actions+=count
    grouped_actions+=sum(v for k,v in group_reasons.items() if k.startswith('accepted'))
    stages.append({'name':name,'sourceVisual':policy['initial']['visual'],'savedVisual':stats['visual'],
        'actualActions':count,'acceptedActions':stats['acceptedActions'],'branchMoves':workflow.get('branchMoves',False),
        'branchMode':workflow.get('branchMode'),'checkpointBranchMode':policy.get('checkpointBranchMode'),
        'outcomes':dict(reasons),'groupedOutcomes':dict(group_reasons),'improvements':wins,
        'peakMiB':peak,'candidateSha256':audit['candidateSha256'],'frozenActionAndGeometryReplay':True})

training=json.loads((base/'bounded-policy-training1/report.json').read_text())
assert training['nativeBinarySha256']==digest(base/'component-environment-v26')
assert training['learnerSha256']==digest(Path('scripts/erd-poc/learn_card_policy.py'))
for view,row in training['views'].items():
    assert digest(base/'bounded-policy-training1'/f'{view}.jsonl')==row['datasetSha256']
    assert digest(base/'bounded-policy-training1'/f'{view}.npz')==row['checkpointSha256']
    assert row['graphs']==64 and row['groupedPositive']>0
assert training['actualCaptainExamplesUsed']==0

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

for directory in [*metrics['stagePeakMiB'],'bounded-policy-training1','bounded-policy-validation']:
    for path in sorted((base/directory).rglob('*')):
        if path.is_file():copy(path,path.relative_to(base))
copy(base/'component-environment-v26',Path('dependencies/component-environment-v26'))
for name in ['train_bounded_policies.py','validate_bounded_policy.py','seal_bounded_policy_experiments.py',
             'summarize_bounded_rollouts.py',
             'bounded-policy-run-metrics.json',
             'bounded-trained-rollout-diagnosis.json']:
    copy(base/name,Path(name))
for name in ['apply_learned_components.cjs','audit_individual_policy_experiment.cjs','audit_leaf_card_connections.cjs',
             'probe_overview_boundary.cjs','export_learned_components.cjs','compose_independent_views.cjs']:
    copy(Path('scripts/erd-poc')/name,Path('code')/name)
for path in (base/'combined-bounded-verified').iterdir():
    if path.is_file():copy(path,Path('combined-best')/path.name,plain=True)
for suffix in ['layout.json','audit.json','provenance.json']:
    path=Path('data/erd-poc/candidates')/('captain-ml-independent-views.'+suffix)
    copy(path,Path('previous-best')/path.name,plain=True)
assert digest(stable)==old_sha
manifest={'scope':'spacing-bounded neural translation output, freshly trained policies, and learned endpoint refinement',
    'overviewTarget':150,'individualTarget':750,'overviewVisual':combined['overviewVisual'],'individualVisual':combined['individualVisual'],
    'targetMet':False,'promoted':False,'candidateSha256':combined['candidateSha256'],'previousCandidateSha256':old_sha,
    'policyActions':total_actions,'acceptedGroupedActions':grouped_actions,'stages':stages,
    'training':training['views'],'priorArchive':str(prior/'manifest.json'),'priorArchiveSha256':digest(prior/'manifest.json'),
    'dependencies':dependencies,'proposalAuthority':'trained neural outputs; native geometry only decodes and measures',
    'resourcePolicy':metrics,'actualBrowserVerified':False,'files':files}
(target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'archive':str(target),'manifestSha256':digest(target/'manifest.json'),'stages':len(stages),
    'policyActions':total_actions,'acceptedGroupedActions':grouped_actions,'files':len(files),
    'storedBytes':sum(p.stat().st_size for p in target.rglob('*') if p.is_file())}))
