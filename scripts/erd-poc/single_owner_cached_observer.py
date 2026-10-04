"""Read-only observation of explicitly supplied, Native-proven legal geometry.

This proposes no positions, repairs no routes, and cannot accept a move. It
decodes the existing wire and projects the same canonical groups as Native.
Conflict matrices are shared across singleton component feature contexts.
"""
import math
from pathlib import Path
import numpy as np
from joint_neural_ports import perimeter_points
from learned_global_replay import pairs, routes


def rounded(x):
    return np.copysign(np.floor(np.abs(x)*100+.5),x)/100


def crossing_matrix(lines,chunk=32):
    first=lines[:,0];last=lines[:,1];vectors=last-first
    low=np.minimum(first,last);high=np.maximum(first,last);out=np.empty((len(lines),len(lines)),dtype=bool)
    for start in range(0,len(lines),chunk):
        a=first[start:start+chunk,None];v=vectors[start:start+chunk,None]
        b=first[None];w=vectors[None]
        d=b-a;e=last[None]-a;f=a-b;g=last[start:start+chunk,None]-b
        o1=v[...,0]*d[...,1]-v[...,1]*d[...,0]
        o2=v[...,0]*e[...,1]-v[...,1]*e[...,0]
        o3=w[...,0]*f[...,1]-w[...,1]*f[...,0]
        o4=w[...,0]*g[...,1]-w[...,1]*g[...,0]
        mask=(o1*o2 < -1e-9)&(o3*o4 < -1e-9)
        mask&=np.all(low[start:start+chunk,None]<=high[None],axis=2)&np.all(low[None]<=high[start:start+chunk,None],axis=2)
        out[start:start+len(mask)]=mask
    assert not np.diag(out).any() and np.array_equal(out,out.T)
    return out


def hit_matrix(lines,positions,sizes,edges,chunk=32):
    low=positions-sizes/2-10;high=positions+sizes/2+10;out=np.empty((len(lines),len(positions)),dtype=bool)
    for start in range(0,len(lines),chunk):
        chosen=lines[start:start+chunk];origin=chosen[:,0,None];direction=(chosen[:,1]-chosen[:,0])[:,None]
        a=np.minimum(chosen[:,0],chosen[:,1])[:,None];b=np.maximum(chosen[:,0],chosen[:,1])[:,None]
        mask=np.all(b>low[None],axis=2)&np.all(a<high[None],axis=2)
        lo=np.zeros(mask.shape);hi=np.ones(mask.shape)
        for axis in (0,1):
            d=direction[...,axis];parallel=np.abs(d)<1e-9
            den=np.where(parallel,1.,d)
            left=(low[None,:,axis]-origin[...,axis])/den;right=(high[None,:,axis]-origin[...,axis])/den
            lo=np.maximum(lo,np.where(parallel,0.,np.minimum(left,right)))
            hi=np.minimum(hi,np.where(parallel,1.,np.maximum(left,right)))
            mask&=np.where(parallel,(origin[...,axis]>low[None,:,axis])&(origin[...,axis]<high[None,:,axis]),hi-lo>1e-9)
        mask&=(hi>1e-9)&(lo<1-1e-9)
        local=edges[start:start+len(chosen)];mask[np.arange(len(chosen))[:,None],local]=False
        out[start:start+len(chosen)]=mask
    return out


class CachedObserver:
    def __init__(self,source,decoder):
        self.source=Path(source);self.decoder=decoder;self.provider=decoder.provider
        self.positions=decoder.positions;self.sizes=decoder.sizes
        self.full_positions=np.array(list(pairs(self.source/'individual.positions.tsv').values()))
        # Preserve full node order directly from the immutable input.
        self.full_ids=[];self.full_sizes=[]
        for line in (self.source/'individual.nodes.tsv').read_text().splitlines():
            key,w,h=line.split('\t');self.full_ids.append(key);self.full_sizes.append((float(w),float(h)))
        self.full_sizes=np.array(self.full_sizes);idx={key:i for i,key in enumerate(self.full_ids)}
        self.edge_ids=[];edges=[]
        for line in (self.source/'individual.edges.tsv').read_text().splitlines():
            key,a,b=line.split('\t');self.edge_ids.append(key);edges.append((idx[a],idx[b]))
        self.edges=np.array(edges,dtype=np.int32);edge_idx={key:i for i,key in enumerate(self.edge_ids)}
        self.owner=self.provider.owner
        self.components=[np.flatnonzero(self.owner==n) for n in range(len(self.positions))]
        self.groups=[];self.group_contexts=[]
        for line in (self.source/'groups.tsv').read_text().splitlines():
            fields=line.split('\t');group=np.array([edge_idx[key] for key in fields[1:]],dtype=np.int32)
            self.groups.append(group);self.group_contexts.append(set(map(int,self.owner[self.edges[group]].ravel())))
        self.incident=[np.flatnonzero(np.any(self.owner[self.edges]==n,axis=1)) for n in range(len(self.positions))]
        self.node_groups=[np.array([g for g,context in enumerate(self.group_contexts) if n in context],dtype=np.int32) for n in range(len(self.positions))]
        self.full_edge_contexts=[set(map(int,self.owner[e])) for e in self.edges]
        low=self.positions-self.sizes/2;high=self.positions+self.sizes/2
        self.frame=(float(low[:,0].min()),float(high[:,0].max()),float(low[:,1].min()),float(high[:,1].max()))

    def decode(self,action):
        # Match the existing Native wire and cent decoder exactly.
        encoded=np.fromstring(' '.join(format(float(v),'.12g') for v in action.ravel()),sep=' ').reshape(-1,2)
        delta=rounded(encoded[:len(self.positions)])
        changed=perimeter_points(self.provider.phase+encoded[len(self.positions):],self.provider.endpoint_sizes)[0]-self.provider.base_boundary
        full_lines=rounded(self.provider.original_ports+changed+delta[self.provider.owner_edges])
        positions=self.positions+delta;full_positions=self.full_positions+delta[self.owner]
        physical=[];physical_edges=[]
        for group in self.groups:
            eligible=[int(e) for e in group if self.owner[self.edges[e,0]]!=self.owner[self.edges[e,1]]]
            e=min(eligible,key=lambda e:(math.hypot(*(full_lines[e,1]-full_lines[e,0])),self.edge_ids[e]))
            a,b=map(int,self.owner[self.edges[e]]);line=full_lines[e];params=[]
            for node,exiting in ((a,True),(b,False)):
                if len(self.components[node])==1:params.append(0. if exiting else 1.);continue
                enter=0.;leave=1.
                for axis in (0,1):
                    origin=float(line[0,axis]);direction=float(line[1,axis]-line[0,axis])
                    if abs(direction)<1e-12:continue
                    left=(float(positions[node,axis]-self.sizes[node,axis]/2)-origin)/direction
                    right=(float(positions[node,axis]+self.sizes[node,axis]/2)-origin)/direction
                    enter=max(enter,min(left,right));leave=min(leave,max(left,right))
                params.append(leave if exiting else enter)
            physical.append(rounded(line[0]+(line[1]-line[0])*np.array(params)[:,None]));physical_edges.append((a,b))
        return positions,full_positions,np.array(physical),full_lines,np.array(physical_edges,dtype=np.int32)

    @staticmethod
    def costs(cross,hits,edge_contexts,node_contexts,count):
        counts=np.zeros((count,2),dtype=np.int64)
        for e,f in zip(*np.nonzero(np.triu(cross,1))):
            for owner in edge_contexts[e]|edge_contexts[f]:counts[owner,0]+=1
        for e,n in zip(*np.nonzero(hits)):
            for owner in edge_contexts[e]|node_contexts[n]:counts[owner,1]+=1
        return counts

    def observe(self,action,legal_result):
        # Hard costs cannot be inferred from visual scores. A verified Native
        # legal result is mandatory for each supplied action, including zero.
        assert legal_result['legal'] and legal_result['hard']==legal_result['individualHard']==legal_result['spacing']==0
        pos,full_pos,physical,full,physical_edges=self.decode(action)
        ac=crossing_matrix(physical);ah=hit_matrix(physical,pos,self.sizes,physical_edges)
        bc=crossing_matrix(full);bh=hit_matrix(full,full_pos,self.full_sizes,self.edges)
        visual=int(np.count_nonzero(ac)//2+np.count_nonzero(ah));individual=int(np.count_nonzero(bc)//2+np.count_nonzero(bh))
        assert (visual,individual)==(legal_result['visual'],legal_result['individualVisual']),(visual,individual,legal_result)
        ca=self.costs(ac,ah,self.group_contexts,[{n} for n in range(len(pos))],len(pos))
        cb=self.costs(bc,bh,self.full_edge_contexts,[{int(n)} for n in self.owner],len(pos))
        nodes=[];all_ids=np.arange(len(pos));left,right,top,bottom=self.frame
        for n,p in enumerate(pos):
            raw_x,raw_y=map(float,p);w,h=map(float,self.sizes[n])
            l=raw_x-w/2;r=raw_x+w/2;t=raw_y-h/2;bottom_n=raw_y+h/2
            px=(l+r)/2;py=(t+bottom_n)/2
            members=self.components[n];es=self.incident[n];gs=self.node_groups[n]
            neighbors=[];lengths=[]
            for e in es:
                a,b=map(int,self.edges[e]);other=b if self.owner[a]==n else a
                if self.owner[other]==n:continue
                q=full_pos[other];neighbors.append(q);lengths.append(math.hypot(float(q[0])-px,float(q[1])-py))
            lengths.sort();scale=max(512.,lengths[len(lengths)//2] if lengths else 512.)
            values=[(r-l)/scale,(bottom_n-t)/scale,math.log1p(len(members)),math.log1p(len(es)),
                    math.log1p(int(ca[n,0])),math.log1p(int(ca[n,1])),math.log1p(int(cb[n,0])),math.log1p(int(cb[n,1])),0.,0.]
            x=y=xx=yy=ux=uy=0.
            for q in neighbors:
                dx=(float(q[0])-px)/scale;dy=(float(q[1])-py)/scale;length=max(.001,math.hypot(dx,dy))
                x+=dx;y+=dy;xx+=dx*dx;yy+=dy*dy;ux+=dx/length;uy+=dy/length
            count=max(1,len(neighbors));values.extend((x/count,y/count,xx/count,yy/count,ux/count,uy/count))
            dist=np.hypot(pos[:,0]-px,pos[:,1]-py);dist=np.floor(dist*1e6+.5)/1e6
            order=np.lexsort((all_ids,dist));near=order[order!=n][:4]
            for m in near:values.extend(((float(pos[m,0])-px)/scale,(float(pos[m,1])-py)/scale,float(self.sizes[m,0])/scale,float(self.sizes[m,1])/scale))
            values.extend([0.]*(4*(4-len(near))))
            hit=bh[:,members].any(axis=1);cross=bc[:,es].sum(axis=1)
            eligible=(self.owner[self.edges[:,0]]!=n)&(self.owner[self.edges[:,1]]!=n)&(hit|(cross>0))
            conflicts=[]
            for f in np.flatnonzero(eligible):
                a,b=full[f];dx=float(b[0]-a[0]);dy=float(b[1]-a[1])
                fraction=min(1.,max(0.,((px-float(a[0]))*dx+(py-float(a[1]))*dy)/max(.01,dx*dx+dy*dy)))
                distance=math.hypot(px-float(a[0])-fraction*dx,py-float(a[1])-fraction*dy)
                conflicts.append((math.floor(distance*1e6+.5),int(f)))
            conflicts.sort(key=lambda pair:pair[0])
            for _,f in conflicts[:4]:
                a,b=full[f]
                if a[0]>b[0] or (a[0]==b[0] and a[1]>b[1]):a,b=b,a
                values.extend(((float(a[0])-px)/scale,(float(a[1])-py)/scale,(float(b[0])-px)/scale,(float(b[1])-py)/scale,float(hit[f]),math.log1p(int(cross[f]))))
            values.extend([0.]*(6*(4-min(4,len(conflicts)))))
            values.extend(((l-left)/scale,(right-r)/scale,(t-top)/scale,(bottom-bottom_n)/scale,
                           lengths[0]/scale if lengths else 0.,lengths[-1]/scale if lengths else 0.,math.log1p(scale/512),math.log1p(len(gs))))
            assert len(values)==64
            nodes.append({'id':n,'features':[float(format(min(8.,max(-8.,v)),'.12g')) for v in values]})
        return {'observed':True,'visual':visual,'individualVisual':individual,'reason':'ok','nodes':nodes}
