#pragma once
// Observe every component's local score in one pass over the supplied scene.
// This cannot create coordinates, rank proposals, change the source or repair it.
namespace {
struct ExactComponentObservationCosts {
  std::vector<NodeCost> physical,individual;
  static void increment(std::vector<NodeCost>& out,std::initializer_list<int> owners,bool crossing) {
    std::vector<int> uniqueOwners(owners);std::sort(uniqueOwners.begin(),uniqueOwners.end());
    uniqueOwners.erase(std::unique(uniqueOwners.begin(),uniqueOwners.end()),uniqueOwners.end());
    for(int owner:uniqueOwners) {
      if(crossing)out.at(owner).score.cross++;else out.at(owner).score.hit++;
    }
  }
  static std::vector<NodeCost> observe(const State& state,const std::vector<int>& owner,int count) {
    std::vector<NodeCost> out(count);
    for(int a=0;a<int(state.nodes.size());a++)for(int b=a+1;b<int(state.nodes.size());b++) {
      const auto pair=state.pair(a,b);
      if(pair.overlap||pair.spacing)throw std::runtime_error("observation cache requires a legal spacing state");
    }
    for(int e=0;e<int(state.edges.size());e++) {
      const auto a=state.edges[e];
      for(int f=e+1;f<int(state.edges.size());f++)if(crosses(state.routes[e],state.routes[f])) {
        const auto b=state.edges[f];increment(out,{owner[a.s],owner[a.t],owner[b.s],owner[b.t]},true);
      }
      for(int n=0;n<int(state.nodes.size());n++)if(n!=a.s&&n!=a.t&&hits(state.routes[e],state.pos[n],state.nodes[n]))
        increment(out,{owner[a.s],owner[a.t],owner[n]},false);
    }
    // Hard costs are nonnegative. Full hard zero implies local hard zero.
    if(policyHardScore(state)!=0)throw std::runtime_error("observation cache requires full hard zero");
    return out;
  }
  explicit ExactComponentObservationCosts(const ComponentEnvironment& state) {
    if(state.branchMoves||state.jointMoves||state.neighborMoves)
      throw std::runtime_error("observation cache only supports individual component contexts");
    std::vector<int> owners(state.a.nodes.size());std::iota(owners.begin(),owners.end(),0);
    physical=observe(state.a,owners,int(state.components.size()));
    individual=observe(state.b,state.owner,int(state.components.size()));
  }
};
bool verifyObservationCache=false;
long observationCacheParityRows=0;
}
