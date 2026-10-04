#pragma once
// Measure local costs for exactly the contexts selected by features().
// It observes supplied coordinates and cannot propose, rank or repair them.
namespace {
struct ExactComponentObservationCosts {
  std::vector<NodeCost> physical,individual;
  using Contexts=std::vector<std::vector<int>>;
  static std::vector<NodeCost> observe(const State& state,const Contexts& nodes,const Contexts& edges,int count) {
    std::vector<NodeCost> out(count);
    std::vector<int> seen(count,-1);int event=0;
    const auto increment=[&](const std::vector<int>& a,const std::vector<int>& b,bool crossing) {
      ++event;
      for(const auto* row:{&a,&b})for(int owner:*row)if(seen.at(owner)!=event) {
        seen[owner]=event;
        if(crossing)out[owner].score.cross++;else out[owner].score.hit++;
      }
    };
    for(int a=0;a<int(state.nodes.size());a++)for(int b=a+1;b<int(state.nodes.size());b++) {
      const auto pair=state.pair(a,b);
      if(pair.overlap||pair.spacing)throw std::runtime_error("observation cache requires legal spacing");
    }
    for(int e=0;e<int(state.edges.size());e++) {
      const auto endpoints=state.edges[e];
      for(int f=e+1;f<int(state.edges.size());f++)if(crosses(state.routes[e],state.routes[f]))
        increment(edges[e],edges[f],true);
      for(int n=0;n<int(state.nodes.size());n++)if(n!=endpoints.s&&n!=endpoints.t
          &&hits(state.routes[e],state.pos[n],state.nodes[n]))increment(edges[e],nodes[n],false);
    }
    // Full hard costs are nonnegative, so full zero proves all local hard zero.
    if(policyHardScore(state)!=0)throw std::runtime_error("observation cache requires full hard zero");
    return out;
  }
  explicit ExactComponentObservationCosts(const ComponentEnvironment& state) {
    if(state.branchMoves||state.jointMoves||state.neighborMoves)
      throw std::runtime_error("observation cache only supports singleton component contexts");
    const int count=state.components.size();
    Contexts an(state.a.nodes.size()),ae(state.a.edges.size()),bn(state.b.nodes.size()),be(state.b.edges.size());
    for(int n=0;n<count;n++) {
      const auto c=state.context(n);
      for(int m:c.physical)an.at(m).push_back(n);
      for(int e:c.groups)ae.at(e).push_back(n);
      for(int m:c.nodes)bn.at(m).push_back(n);
      for(int e:c.edges)be.at(e).push_back(n);
    }
    physical=observe(state.a,an,ae,count);individual=observe(state.b,bn,be,count);
  }
};
bool verifyObservationCache=false;
long observationCacheParityRows=0;
}
