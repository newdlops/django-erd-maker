#!/usr/bin/env python3
"""Fine-tune single-owner outcomes with streamed replay and preserved folds."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import zipfile
import numpy as np
from directed_cycle_model import KEYS
from owner_amplitude_cycle_model import SCHEMA
from single_owner_cycle_model import ACTION_SCHEMA,MODEL_KIND,load
from synthetic_single_owner_curriculum_v2 import code_hashes as collection_hashes
from run_single_owner_cycle_policy import hashes as captain_hashes
def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        while block:=stream.read(65536):h.update(block)
    return h.hexdigest()


def hashes():
    return collection_hashes()|{name:digest(Path(__file__).parent/name) for name in ['train_single_owner_cycle_model.py','train_single_owner_cycle_model_v2.py']}

def word(view,cycle):return (str(view),*sorted(map(int,cycle)))
def fold(key):return int(hashlib.sha256(repr(key).encode()).hexdigest(),16)%5==0

def collect(args,parent):
    old_path=Path(parent['trainingDatasetPath']);assert digest(old_path)==parent['trainingDatasetSha256']
    old_count=17408;count=old_count+2048+4096
    x=np.lib.format.open_memmap(args.out/'x.npy',mode='w+',dtype=np.float32,shape=(count,3,141))
    # Read compressed replay features once through bounded row buffers.
    with zipfile.ZipFile(old_path) as archive,archive.open('x.npy') as stream:
        version=np.lib.format.read_magic(stream)
        shape,fortran,dtype=(np.lib.format.read_array_header_1_0 if version==(1,0) else np.lib.format.read_array_header_2_0)(stream)
        assert shape==(old_count,3,141) and dtype==np.float32 and not fortran
        for first in range(0,old_count,64):
            size=min(64,old_count-first);block=stream.read(size*3*141*4);assert len(block)==size*3*141*4
            x[first:first+size]=np.frombuffer(block,dtype=np.float32).reshape(size,3,141)
        assert not stream.read(1)
    data={'x':x,'y':np.empty(count),'actual_gain':np.empty(count),
          'cycles':np.empty((count,3),dtype=np.int32),'amplitudes':np.empty((count,3)),
          'view':np.empty(count,dtype='<U10'),'kind':np.empty(count,dtype='<U16'),
          'graph':np.full(count,-1,dtype=np.int32)}
    parts={}
    with np.load(old_path,allow_pickle=False) as saved:
        for name in ('y','actual_gain','cycles','amplitudes','view','kind','graph'):data[name][:old_count]=saved[name]
        old_train=saved['training'].copy()
        for name in ('captain','positive','negative','mixed'):parts[name]=saved[name+'_validation'].copy()
        graph_fold=saved['uniform_validation_graphs'].copy()+128
    val_words={word(data['view'][i],data['cycles'][i]) for i in parts['captain']}
    seen_words={word(data['view'][i],data['cycles'][i]) for i in old_train if data['kind'][i]=='captain'}
    assert not val_words&seen_words
    stages=[];offset=old_count;captain_validation=[];new_train=[]
    for category in ('captain_single','single_synthetic'):
        locations=[(args.captain/(v+'-frozen1'),-1,v) for v in ('individual','overview')] if category=='captain_single' else [
            (args.synthetic/f'graph-{g:02}'/v,g,v) for g in range(128,160) for v in ('overview','individual')]
        for stage,graph,view in locations:
            report=json.loads((stage/'report.json').read_text());expected=1024 if graph<0 else 64
            assert report['attempts']==expected and digest(stage/'labels.npz')==report['labelsSha256']
            assert report['codeSha256']==(captain_hashes() if graph<0 else collection_hashes())
            assert report['checkpointSha256']==digest(args.parent)
            assert all(digest(Path(report['sourceDirectory'])/n)==s for n,s in report['sourceInputs'].items())
            with np.load(stage/'labels.npz',allow_pickle=False) as saved:
                assert saved['x'].shape==(expected,3,141) and saved['x'].dtype==np.float32
                for name in ('x','y','actual_gain','cycles','amplitudes'):data[name][offset:offset+expected]=saved[name]
            data['view'][offset:offset+expected]=view;data['kind'][offset:offset+expected]=category;data['graph'][offset:offset+expected]=graph
            for i in range(offset,offset+expected):
                key=word(view,data['cycles'][i])
                if graph<0:
                    if key in val_words or (fold(key) and key not in seen_words):captain_validation.append(i)
                    else:new_train.append(i)
                elif graph not in graph_fold:new_train.append(i)
            stages.append({'path':str(stage/'report.json'),'sha256':digest(stage/'report.json'),'labelsSha256':report['labelsSha256'],
                           'kind':category,'graph':graph,'view':view})
            offset+=expected
    assert offset==count
    parts['captain_single']=np.array(captain_validation,dtype=np.int64)
    parts['single_synthetic']=np.flatnonzero((data['kind']=='single_synthetic')&np.isin(data['graph'],graph_fold))
    assert len(parts['single_synthetic'])==1024 and len(parts['captain_single'])>0
    training=np.r_[old_train,np.array(new_train,dtype=np.int64)]
    validation=np.r_[*parts.values()];assert not np.intersect1d(training,validation).size
    hold_words={word(data['view'][i],data['cycles'][i]) for i in np.r_[parts['captain'],parts['captain_single']]}
    assert all(word(data['view'][i],data['cycles'][i]) not in hold_words for i in training if data['kind'][i] in ('captain','captain_single'))
    assert not np.isin(data['graph'][training],graph_fold).any()
    single=training[np.isin(data['kind'][training],['captain_single','single_synthetic'])]
    positive=training[data['actual_gain'][training]>0]
    sampling=np.r_[training,np.tile(single,2),np.tile(positive,7)]
    data.update(training=training,sampling=sampling,single_validation_graphs=graph_fold,
                **{name+'_validation':ids for name,ids in parts.items()})
    for first in range(0,count,64):assert np.isfinite(x[first:first+64]).all()
    assert np.isfinite(data['y']).all();x.flush()
    for name,value in data.items():
        if name!='x':np.save(args.out/(name+'.npy'),value,allow_pickle=False)
    ledger={p.name:{'bytes':p.stat().st_size,'sha256':digest(p)} for p in args.out.glob('*.npy')}
    (args.out/'dataset-index.json').write_text(json.dumps(ledger,indent=2)+'\n')
    return data,parts,stages

def reuse_dataset(args,parent):
    index_path=args.reuse_dataset/'dataset-index.json';ledger=json.loads(index_path.read_text())
    for name,item in ledger.items():
        p=args.reuse_dataset/name;assert p.stat().st_size==item['bytes'] and digest(p)==item['sha256']
    data={p.stem:np.load(p,allow_pickle=False,mmap_mode='r' if p.stem=='x' else None) for p in args.reuse_dataset.glob('*.npy')}
    assert data['x'].shape==(23552,3,141) and data['x'].dtype==np.float32
    parts={name:data[name+'_validation'] for name in ['captain','positive','negative','mixed','captain_single','single_synthetic']}
    stages=[]
    for category in ['captain_single','single_synthetic']:
        locations=[(args.captain/(v+'-frozen1'),-1,v) for v in ['individual','overview']] if category=='captain_single' else [
            (args.synthetic/f'graph-{g:02}'/v,g,v) for g in range(128,160) for v in ['overview','individual']]
        for stage,graph,view in locations:
            r=json.loads((stage/'report.json').read_text())
            assert digest(stage/'labels.npz')==r['labelsSha256'] and r['checkpointSha256']==digest(args.parent)
            stages.append({'path':str(stage/'report.json'),'sha256':digest(stage/'report.json'),'labelsSha256':r['labelsSha256'],
                           'kind':category,'graph':graph,'view':view})
    return data,parts,stages


def validation_loss(model,data,parts):
    values={}
    for name,ids in parts.items():
        values[name]=sum(float(model.loss(data['x'][chunk],data['y'][chunk]))*len(chunk)
            for first in range(0,len(ids),256) for chunk in [ids[first:first+256]])/len(ids)
    return float(sum(values.values())/len(values)),values

def train(args):
    args.out.mkdir(parents=True,exist_ok=False);model,parent=load(args.parent)
    assert parent['kind']=='owner-amplitude-synthetic-cycle-critic-v1'
    data,parts,stages=reuse_dataset(args,parent);initial={k:v.copy() for k,v in model.p.items()}
    first={k:np.zeros_like(v) for k,v in initial.items()};second={k:v.copy() for k,v in first.items()}
    initial_loss,initial_parts=validation_loss(model,data,parts);best_loss=initial_loss;best=initial
    updates=selected_updates=selected_epoch=0;history=[];rng=np.random.default_rng(args.seed);started=time.monotonic()
    for epoch in range(32):
        order=rng.permutation(data['sampling'])
        for offset in range(0,len(order),64):
            ids=order[offset:offset+64];_,gradients=model.loss(data['x'][ids],data['y'][ids],True)
            norm=np.sqrt(sum(np.sum(g*g) for g in gradients.values()));updates+=1
            for key in KEYS:
                g=gradients[key]*min(1.,5/max(norm,1e-12));first[key]=.9*first[key]+.1*g;second[key]=.999*second[key]+.001*g*g
                model.p[key]-=.0005*(first[key]/(1-.9**updates))/(np.sqrt(second[key]/(1-.999**updates))+1e-8)
            if time.monotonic()-started>=10:break
        loss,values=validation_loss(model,data,parts);history.append({'epoch':epoch+1,'updates':updates,'loss':loss,'parts':values})
        if loss<best_loss:best_loss=loss;best={k:v.copy() for k,v in model.p.items()};selected_updates=updates;selected_epoch=epoch+1
        if time.monotonic()-started>=10:break
    assert selected_updates>0;model.p=best;final_loss,final_parts=validation_loss(model,data,parts);assert final_loss==best_loss
    changes={k:float(np.linalg.norm(best[k]-initial[k])) for k in KEYS};assert all(v>0 for v in changes.values())
    single=np.isin(data['kind'][data['training']],['captain_single','single_synthetic'])
    metadata={'schema':SCHEMA,'kind':MODEL_KIND,'actionSchema':ACTION_SCHEMA,'parent':str(args.parent),'parentSha256':digest(args.parent),
        'sourceInputsByView':parent['sourceInputsByView'],'sourceStages':stages,'trainingDatasetDirectory':str(args.reuse_dataset),
        'trainingDatasetIndexSha256':digest(args.reuse_dataset/'dataset-index.json'),'parentDatasetPath':parent['trainingDatasetPath'],
        'parentDatasetSha256':parent['trainingDatasetSha256'],'singleOwnerTrainingRows':int(single.sum()),
        'rows':len(data['x']),'trainingRows':len(data['training']),
        'rowsByKind':{str(k):int((data['kind']==k).sum()) for k in np.unique(data['kind'])},
        'improvingTrainingExamplesByKind':{str(k):int(((data['kind'][data['training']]==k)&(data['actual_gain'][data['training']]>0)).sum()) for k in np.unique(data['kind'])},
        'validationRowsByPart':{k:len(v) for k,v in parts.items()},'singleSyntheticValidationGraphs':data['single_validation_graphs'].tolist(),
        'newCaptainWordFoldSeed':None,'newCaptainWordFold':'sha256(repr(view,sorted triple)) modulo 5; parent-seen training words excluded from new validation',
        'originalCaptainWordFoldPreserved':True,'validationScope':'whole words/graphs excluded from fine-tuning; not an independent Captain-scene test',
        'singleTrainingSamplingMultiplicity':3,'additionalImprovingTrainingCopies':7,'normalizationUnchanged':True,'completedInterruptedDatasetReused':True,'allDatasetFilesStreamHashVerified':True,
        'trainingFeatureDtype':'float32','inferenceFeatureInputDtype':'float32','innerNetworkArithmeticDtype':'float64',
        'newTrainingUpdatesExecuted':updates,'selectedCheckpointUpdates':selected_updates,'selectedEpoch':selected_epoch,
        'seed':args.seed,'learningRate':.0005,'batchSize':64,'epochsLimit':32,'secondsLimit':10,
        'initialValidationLoss':initial_loss,'bestValidationLoss':best_loss,'initialValidationParts':initial_parts,'selectedValidationParts':final_parts,
        'parameterGroupChangesL2':changes,'codeSha256':hashes(),'seconds':time.monotonic()-started}
    np.savez_compressed(args.out/'model.npz',**best,**{'initial__'+k:v for k,v in initial.items()},mean=model.mean,scale=model.scale,metadata=json.dumps(metadata))
    restored,_=load(args.out/'model.npz')
    for start in range(0,len(data['x']),256):assert np.array_equal(restored.forward(data['x'][start:start+256])[0],model.forward(data['x'][start:start+256])[0])
    report=metadata|{'checkpointSha256':digest(args.out/'model.npz'),'history':history}
    (args.out/'training.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['rows','trainingRows','singleOwnerTrainingRows','improvingTrainingExamplesByKind','newTrainingUpdatesExecuted',
        'selectedCheckpointUpdates','initialValidationLoss','bestValidationLoss','initialValidationParts','selectedValidationParts','checkpointSha256','seconds']}),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--parent',type=Path,required=True);p.add_argument('--captain',type=Path,required=True)
    p.add_argument('--synthetic',type=Path,required=True);p.add_argument('--reuse-dataset',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--seed',type=int,default=95707);train(p.parse_args())
