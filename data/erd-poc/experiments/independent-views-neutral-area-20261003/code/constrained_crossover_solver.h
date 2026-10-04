// Binary scene choices have useful implications: if choosing a card from
// parent B would overlap a card from A, both must change together. Propagate
// these implications before judging a move, including cyclic exchanges.
namespace {
FactorSolution solveCrossoverFactors(const FiniteLabelFactors& model,const std::vector<std::vector<int>>& groups,
                                     int steps,uint64_t seed,const std::function<bool()>& expired,bool relax) {
  const int count=model.domains.size();
  std::vector<std::vector<int>> forward(count),reverse(count);
  for(const auto& factor:model.factors)if(factor.variables.size()==2&&!factor.spacing.empty()) {
    int a=factor.variables[0],b=factor.variables[1];
    if(factor.spacing[1]){forward[b].push_back(a);reverse[a].push_back(b);}
    if(factor.spacing[2]){forward[a].push_back(b);reverse[b].push_back(a);}
  }
  for(auto* graph:{&forward,&reverse})for(auto& row:*graph){std::sort(row.begin(),row.end());row.erase(std::unique(row.begin(),row.end()),row.end());}
  std::mt19937_64 rng(seed);std::uniform_real_distribution<double> uniform(0,1);
  const auto pick=[&](int n){return int(rng()%uint64_t(n));};
  std::vector<int> labels(count,0),ones(count,1);
  FactorSolution best{labels,model.energy(labels),0};
  if(model.energy(ones)<best.energy)best={ones,model.energy(ones),0};
  const int period=std::max(1,steps/4);
  long penalty=relax?4:1000000000L,current=model.energy(labels,penalty);
  std::vector<int> seen(count,-1),factorSeen(model.factors.size(),-1);int wins=0;
  for(int step=0;step<steps&&!expired();step++) {
    if(step%period==0) {
      int cycle=step/period;labels=cycle==1?ones:best.labels;
      penalty=relax?(cycle<3?4L*(1L<<(2*cycle)):1000000000L):1000000000L;
      current=model.energy(labels,penalty);
    }
    double progress=double(step%period)/period,temperature=12*std::pow(.02/12,progress);
    std::vector<int> pending;int target=pick(2);
    if(step%7==0&&!groups.empty())pending=groups[pick(groups.size())];
    else if(step%5==0&&!model.factors.empty()) {
      int chosen=pick(model.factors.size());
      for(int trial=0;trial<8;trial++) {
        int f=pick(model.factors.size());
        if(model.factors[f].cost(model.factors[f].index(labels),penalty)>model.factors[chosen].cost(model.factors[chosen].index(labels),penalty))chosen=f;
      }
      pending=model.factors[chosen].variables;
    } else {int n=pick(count);pending={n};target=1-labels[n];}
    const auto& implication=target?forward:reverse;
    for(int n:pending)seen[n]=step;
    for(size_t i=0;i<pending.size();i++)for(int other:implication[pending[i]])if(seen[other]!=step){seen[other]=step;pending.push_back(other);}
    std::vector<int> moving,factors;
    for(int n:pending)if(labels[n]!=target) {
      moving.push_back(n);
      for(int f:model.incident[n])if(factorSeen[f]!=step){factorSeen[f]=step;factors.push_back(f);}
    }
    if(moving.empty())continue;
    const auto local=[&]{long cost=0;for(int f:factors)cost+=model.factors[f].cost(model.factors[f].index(labels),penalty);return cost;};
    long before=local();for(int n:moving)labels[n]=target;
    long delta=local()-before;
    if(delta<=0||uniform(rng)<std::exp(-double(delta)/temperature)) {
      current+=delta;if(delta<0&&moving.size()>1)wins++;
      if(current<best.energy&&model.spacingCount(labels)==0){best.energy=current;best.labels=labels;}
    } else for(int n:moving)labels[n]=1-target;
    if(step%2048==0&&current!=model.energy(labels,penalty))throw std::runtime_error("crossover solver score drift");
  }
  if(best.energy!=model.energy(best.labels)||model.spacingCount(best.labels))throw std::runtime_error("invalid crossover solver best");
  best.blockWins=wins;return best;
}
}
