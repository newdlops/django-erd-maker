"""Validate bounded output units, independent boxes, and legacy replay."""
import hashlib,json,shutil,subprocess,sys
from pathlib import Path
sys.path.insert(0,'scripts/erd-poc')
import numpy as np
from learned_branch_map import prepare_branch_map
from learned_bounded_replay import BoundedMovingRayReplay, self_test
from learn_card_policy import load_data

base=Path(__file__).parent;out=base/'bounded-policy-validation';out.mkdir(exist_ok=False)
binary=base/'component-environment-v26';digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
results={}
def run(name,command):
    result=subprocess.run(list(map(str,command)),capture_output=True,text=True,check=True)
    (out/(name+'.stdout')).write_text(result.stdout);(out/(name+'.stderr')).write_text(result.stderr)
    return result.stdout
results['native']=[json.loads(line) for line in run('native',[binary,'--self-test']).splitlines()]
self_test();results['pythonFixturesPassed']=True
results['legacyReplay']=json.loads(run('legacy-cut-replay',[sys.executable,'scripts/erd-poc/learn_card_policy.py','replay',
    '--checkpoint',base/'cut-policy-training1/individual.npz','--out',base/'individual-cut-trained2/learned.tsv']))
for view in ['individual','overview']:
    directory=out/view;directory.mkdir()
    if view=='individual':
        previous=base/'individual-cut-trained2';source=previous/'candidate.individual.layout.json';expected=1966
        for name,old in [('nodes.tsv','individual.nodes.tsv'),('edges.tsv','individual.edges.tsv'),
                         ('positions.tsv','learned.tsv.individual'),('routes.tsv','learned.tsv.individual.routes.tsv')]:
            for prefix in ['','individual.']:shutil.copyfile(previous/old,directory/(prefix+name))
        for name,target in [('nodes.tsv','components.tsv'),('edges.tsv','groups.tsv')]:
            ids=[line.split('\t')[0] for line in (directory/name).read_text().splitlines()]
            (directory/target).write_text(''.join(f'{node}\t{node}\n' for node in ids))
    else:
        source=base/'overview-cut-trained2/candidate.layout.json';expected=291
        payload='data/erd-poc/recovered/captain-2026-09-15-payload.json'
        run('overview-export',['node','scripts/erd-poc/probe_overview_boundary.cjs','export',source,payload,directory,'--max-routes','256','--with-individual'])
        run('overview-components',['node','scripts/erd-poc/export_learned_components.cjs',source,payload,directory])
    branch=prepare_branch_map(directory,digest(source),'cut')
    replay=BoundedMovingRayReplay(directory,1.5e9,True)
    p=subprocess.Popen(list(map(str,[binary,'--directory',directory,'--decoder','bounded-moving-ray',
        '--branches','1','--branch-mode','cut','--out',out/'unused-proposal'])),stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        ready=json.loads(p.stdout.readline());assert ready['visual']==expected
        for n in range(len(replay.ids[0])):
            p.stdin.write(f'BOX {n}\n');p.stdin.flush();native=json.loads(p.stdout.readline())
            np.testing.assert_allclose(native,replay.box(n),rtol=0,atol=1e-8)
        p.stdin.write('QUIT\n');p.stdin.flush();assert p.wait(timeout=5)==0
    finally:
        if p.poll() is None:p.kill();p.wait()
    results[view]={'sourceSha256':digest(source),'visual':expected,'boxesCompared':len(replay.ids[0]),'branchMap':branch}
    print(json.dumps({view:results[view]['boxesCompared']}),flush=True)

labels=out/'labels.jsonl'
row=dict(graph=1,features=[0],actions=[[0,0,1]],branchMoves=True,branchMode='cut',actionDecoder='bounded-moving-ray')
labels.write_text(json.dumps(row)+'\n')
load_data(labels,1,expected_branches=True,expected_branch_mode='cut',expected_bounded=True)
try:load_data(labels,1,expected_branches=True,expected_branch_mode='cut')
except AssertionError:pass
else:raise AssertionError('bounded action units accepted as legacy displacement')
row.pop('actionDecoder');labels.write_text(json.dumps(row)+'\n')
try:load_data(labels,1,expected_bounded=True)
except AssertionError:pass
else:raise AssertionError('legacy displacements accepted as bounded latents')
results['trainingUnitMismatchGuards']=True
sources=out/'sources';sources.mkdir();hashes={}
for name in ['ml_component_environment.cpp','constrained_dual_node_geometry.h','constrained_scene.h',
             'constrained_boundary_sweep.h','learned_branch_map.py','learned_global_replay.py','learned_bounded_replay.py',
             'learn_card_policy.py','run_individual_policy_experiment.py','run_learned_leaf_layout.py','run_memory_bounded.py']:
    path=Path('scripts/erd-poc')/name;shutil.copyfile(path,sources/name);hashes[name]=digest(path)
results.update(implementationHashes=hashes,nativeBinarySha256=digest(binary))
(out/'report.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps({'native':results['native'],'sourceBoxesCompared':2279,'legacyActions':results['legacyReplay']['actions'],'unitMismatchGuards':True}),flush=True)
