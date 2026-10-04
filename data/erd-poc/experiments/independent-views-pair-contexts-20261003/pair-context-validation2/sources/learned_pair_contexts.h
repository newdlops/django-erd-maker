// Topology-only counterpart of learned_pair_contexts.py. No geometry is read.
std::vector<std::vector<int>> pairSeparatorContexts(const State& state) {
  const int count=state.nodes.size();
  std::set<std::pair<int,int>> unique;
  for(const auto e:state.edges)if(e.s!=e.t)unique.insert({std::min(e.s,e.t),std::max(e.s,e.t)});
  std::vector<std::pair<int,int>> edges(unique.begin(),unique.end());
  std::vector<std::vector<std::pair<int,int>>> adjacency(count);
  std::vector<std::vector<int>> graph(count);
  for(int e=0;e<int(edges.size());e++) {
    const auto [a,b]=edges[e];adjacency[a].push_back({b,e});adjacency[b].push_back({a,e});
    graph[a].push_back(b);graph[b].push_back(a);
  }
  std::vector<int> discovered(count,-1),low(count),stack;
  std::vector<std::vector<int>> blocks;int clock=0;
  std::function<void(int,int)> visit=[&](int n,int parentEdge) {
    discovered[n]=low[n]=clock++;
    for(auto [m,e]:adjacency[n]) {
      if(e==parentEdge)continue;
      if(discovered[m]<0) {
        stack.push_back(e);visit(m,e);low[n]=std::min(low[n],low[m]);
        if(low[m]>=discovered[n]) {
          std::vector<int> block;
          for(;;){int f=stack.back();stack.pop_back();block.push_back(f);if(f==e)break;}
          std::sort(block.begin(),block.end());blocks.push_back(std::move(block));
        }
      }else if(discovered[m]<discovered[n]){stack.push_back(e);low[n]=std::min(low[n],discovered[m]);}
    }
  };
  for(int n=0;n<count;n++)if(discovered[n]<0)visit(n,-1);
  if(!stack.empty())throw std::runtime_error("pair context edge stack not empty");
  if(blocks.empty())return {};
  const auto largest=std::min_element(blocks.begin(),blocks.end(),[](const auto& a,const auto& b){
    return a.size()!=b.size()?a.size()>b.size():a.front()<b.front();
  });
  std::vector<bool> core(count,false);
  for(int e:*largest){core[edges[e].first]=true;core[edges[e].second]=true;}
  std::vector<int> anchors,degree(count);
  for(int n=0;n<count;n++)if(core[n]) {
    anchors.push_back(n);for(int m:graph[n])degree[n]+=core[m];
  }
  std::stable_sort(anchors.begin(),anchors.end(),[&](int a,int b){return degree[a]>degree[b];});
  if(anchors.size()>32)anchors.resize(32);
  const auto flood=[&](int seed,const std::vector<bool>& allowed) {
    std::vector<bool> found(count,false);found[seed]=true;std::vector<int> pending{seed};
    for(size_t k=0;k<pending.size();k++)for(int m:graph[pending[k]])if(allowed[m]&&!found[m]) {
      found[m]=true;pending.push_back(m);
    }
    std::sort(pending.begin(),pending.end());return pending;
  };
  std::set<std::vector<int>> contexts;
  for(size_t i=0;i<anchors.size();i++)for(size_t j=i+1;j<anchors.size();j++) {
    const int a=anchors[i],b=anchors[j];auto unseen=core;unseen[a]=unseen[b]=false;
    std::vector<std::vector<int>> parts;
    for(int n=0;n<count;n++)if(unseen[n]) {
      auto part=flood(n,unseen);for(int m:part)unseen[m]=false;parts.push_back(std::move(part));
    }
    if(parts.size()<2)continue;
    const auto trunk=std::min_element(parts.begin(),parts.end(),[](const auto& x,const auto& y){
      return x.size()!=y.size()?x.size()>y.size():x.front()<y.front();
    });
    std::vector<bool> allowed(count,true);allowed[a]=allowed[b]=false;
    for(auto part=parts.begin();part!=parts.end();++part)if(part!=trunk) {
      auto full=flood(part->front(),allowed);std::vector<int> inside;
      for(int m:full)if(core[m])inside.push_back(m);
      if(inside!=*part)throw std::runtime_error("pair context escaped its block component");
      if(full.size()>1)contexts.insert(std::move(full));
    }
  }
  return {contexts.begin(),contexts.end()};
}
