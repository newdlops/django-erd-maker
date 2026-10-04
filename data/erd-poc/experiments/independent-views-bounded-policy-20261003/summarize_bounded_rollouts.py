import collections,json
from pathlib import Path
base=Path(__file__).parent
peaks={'individual-bounded-trained1':132.4,'overview-bounded-trained1':192.7,
       'individual-bounded-port1':135.2,'overview-bounded-port1':188.1,
       'individual-bounded-trained2':136.8,'overview-bounded-trained2':184.6}
rows={}
for name in peaks:
    directory=base/name;policy=json.loads((directory/'learned.tsv.policy.json').read_text());stats=policy['final']
    ids=[line.split('\t')[0] for line in (directory/'nodes.tsv').read_text().splitlines()]
    groups={row[0]:row[1:] for row in (line.split('\t') for line in (directory/'branches.tsv').read_text().splitlines())} if policy['branchMoves'] else {}
    reasons=collections.Counter();wins=[];grouped=0
    for line in (directory/'learned.tsv.actions.jsonl').open():
        action=json.loads(line);result=action['result'];reasons[result['reason']]+=1
        if result['accepted'] and groups:grouped+=len(groups[ids[action['node']]])>1
        if result['accepted'] and result['gain']>0:
            wins.append({'nodeIndex':action['node'],'root':ids[action['node']] if groups else None,
                         'cards':len(groups[ids[action['node']]]) if groups else None,
                         'gain':result['gain'],'delta':result.get('decodedDelta')})
    assert sum(reasons.values())==stats['policyActionsEvaluated']
    if stats.get('boundedMoves'):assert reasons['spacing']==reasons['frame']==0
    rows[name]={'initial':policy['initial']['visual'],'visual':stats['visual'],
                'actualActions':stats['policyActionsEvaluated'],'rejectionsAndAdmissions':dict(reasons),
                'acceptedGroupActions':grouped,'improvements':wins}
metrics={'singleNumericJob':True,'mathThreads':1,'niceIncrement':10,'rssGuardMiB':256,
         'hardCpuPercentageLimit':False,'stagePeakMiB':peaks,'nativeCompilePeakMiB':248.1,
         'validationPeakMiB':180.9,'trainingPeakMiB':184.8,'productAuditPeakMiB':190.1,
         'nativeGeometryComparisons':13720,'boundedGeometryComparisons':1152,
         'acceptedBoundedFixtureGroupedMoves':71,'legacyFrozenActionsReplayed':20000,
         'sourceBoxesCrossChecked':2279,'trainingActionUnitContractsVerified':True,
         'nativeAcceptanceUnchanged':True,'trainedModels':2,'syntheticRewardMeasurements':276480,
         'actualCaptainExamplesUsed':0,'actualBrowserVerified':False}
for name,value in [('bounded-policy-run-metrics.json',metrics),('bounded-trained-rollout-diagnosis.json',rows)]:
    with (base/name).open('x') as output:output.write(json.dumps(value,indent=2)+'\n')
print(json.dumps({'stages':rows,'actualActions':sum(row['actualActions'] for row in rows.values()),
                  'acceptedGroupActions':sum(row['acceptedGroupActions'] for row in rows.values())}))
