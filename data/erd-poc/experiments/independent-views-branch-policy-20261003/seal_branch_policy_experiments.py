"""Seal exact branch-policy evidence before promoting the verified best."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import shutil

base=Path('.tmp/visualcross-ml-150-750-20261003')
target=Path('data/erd-poc/experiments/independent-views-branch-policy-20261003')
prior=Path('data/erd-poc/experiments/independent-views-rigid-branches-20261003')
stable=Path('data/erd-poc/candidates/captain-ml-independent-views.layout.json')
old_sha='38952a65b48a05e3cfe12bba4f0f284d8e71672498c3bc5ada73e204cc417598'
metrics=json.loads((base/'branch-policy-run-metrics.json').read_text())

def digest(path,compressed=False):
    result=hashlib.sha256()
    with (gzip.open(path,'rb') if compressed else path.open('rb')) as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):result.update(chunk)
    return result.hexdigest()

assert digest(stable)==old_sha and not target.exists()
combined=json.loads((base/'combined-branch-verified/audit.json').read_text())
assert digest(base/'combined-branch-verified/candidate.layout.json')==combined['candidateSha256']
assert combined['overviewVisual']<299 and combined['individualVisual']<1981
assert (combined['overviewTarget'],combined['individualTarget'])==(150,750)
assert not combined['thresholdsMet']
for key in ['actualProductFileLoadVerified','completeIndividualGeometryPreserved','browserRouteFunctionParity','roundTripViewSwitchVerified']:
    assert combined[key]
assert combined['models']==1244 and combined['canonicalRelationships']==1727 and not combined['individualSpacingViolations']
for version in [23,24]:
    result=json.loads((base/f'branch-policy-v{version}-validation.json').read_text())
    assert result['nativeBinarySha256']==digest(base/f'component-environment-v{version}')
    for name,sha in result['implementationHashes'].items():
        assert digest(base/f'branch-policy-v{version}-sources'/name)==sha
        if version==24:assert digest(Path('scripts/erd-poc')/name)==sha

dependencies=[]
for name in ['port-environment-v5','leaf-component-policy-v11.npz','leaf-component-policy-v15.npz']:
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
        assert replay[0]['branchPositionsAndRoutesReplay']
    reasons=collections.Counter();group_reasons=collections.Counter();wins=[];count=0
    for line in (directory/'learned.tsv.actions.jsonl').open():
        row=json.loads(line);count+=1;result=row['result'];reasons[result['reason']]+=1
        # Port-policy node indices refer to edges, so only inspect node groups in branch mode.
        if workflow.get('branchMoves'):
            key=ids[row['node']];cards=len(groups[key])
            if cards>1:group_reasons[result['reason']]+=1
            if result['accepted'] and result['gain']>0:wins.append({'root':key,'cards':cards,'gain':result['gain']})
    assert count==stats['policyActionsEvaluated'];total_actions+=count
    grouped_actions+=sum(v for k,v in group_reasons.items() if k.startswith('accepted'))
    stages.append({'name':name,'sourceVisual':policy['initial']['visual'],'savedVisual':stats['visual'],
        'actualActions':count,'acceptedActions':stats['acceptedActions'],'branchMoves':workflow.get('branchMoves',False),
        'outcomes':dict(reasons),'groupedOutcomes':dict(group_reasons),'improvements':wins,
        'peakMiB':peak,'candidateSha256':audit['candidateSha256'],'frozenActionAndGeometryReplay':True})

training=json.loads((base/'branch-policy-training1/report.json').read_text())
for view,row in training['views'].items():
    for kind,info in row['datasets'].items():assert digest(base/'branch-policy-training1'/f'{view}-{kind}.jsonl')==info['sha256']
    for info in row['models'].values():assert digest(Path(info['path']))==info['sha256']

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

for directory in [*metrics['stagePeakMiB'],'branch-policy-training1','branch-policy-v23-sources','branch-policy-v24-sources']:
    for path in sorted((base/directory).rglob('*')):
        if path.is_file():copy(path,path.relative_to(base))
for version in [23,24]:
    copy(base/f'component-environment-v{version}',Path('dependencies')/f'component-environment-v{version}')
    copy(base/f'branch-policy-v{version}-validation.json',Path(f'branch-policy-v{version}-validation.json'))
for name in ['train_branch_policies.py','validate_branch_training.py','seal_branch_policy_experiments.py',
             'branch-policy-run-metrics.json','overview-branch-policy1-diagnosis.json','individual-branch-trained1-diagnosis.json']:
    copy(base/name,Path(name))
for name in ['apply_learned_components.cjs','audit_individual_policy_experiment.cjs','audit_leaf_card_connections.cjs',
             'probe_overview_boundary.cjs','export_learned_components.cjs','compose_independent_views.cjs']:
    copy(Path('scripts/erd-poc')/name,Path('code')/name)
for path in (base/'combined-branch-verified').iterdir():
    if path.is_file():copy(path,Path('combined-best')/path.name,plain=True)
for suffix in ['layout.json','audit.json','provenance.json']:
    path=Path('data/erd-poc/candidates')/('captain-ml-independent-views.'+suffix)
    copy(path,Path('previous-best')/path.name,plain=True)
assert digest(stable)==old_sha
manifest={'scope':'frozen graph-branch contexts, branch-trained neural policies, and learned refinement',
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
