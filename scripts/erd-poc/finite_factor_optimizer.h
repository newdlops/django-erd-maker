// Bounded discrete search with exact factors of up to four variables.
namespace {
struct DenseLabelFactor {
  std::vector<int> variables,strides,values,spacing;
  int index(const std::vector<int>& labels) const {
    int result=0;for(int i=0;i<int(variables.size());i++)result+=strides[i]*labels[variables[i]];
    return result;
  }
  long cost(int index,long penalty) const {return values[index]+(spacing.empty()?0L:penalty*spacing[index]);}
};
struct FiniteLabelFactors {
  std::vector<int> domains;
  std::vector<DenseLabelFactor> factors;
  std::vector<std::vector<int>> incident;
  long energy(const std::vector<int>& labels,long penalty=1000000000L) const {
    long result=0;for(const auto& factor:factors)result+=factor.cost(factor.index(labels),penalty);return result;
  }
  long spacingCount(const std::vector<int>& labels) const {
    long result=0;for(const auto& factor:factors)if(!factor.spacing.empty())result+=factor.spacing[factor.index(labels)];return result;
  }
};
struct FactorSolution {std::vector<int> labels;long energy=0;int blockWins=0;};

FactorSolution solveFiniteFactors(const FiniteLabelFactors& model,int steps,uint64_t seed,
                                 const std::function<bool()>& expired,bool relaxSpacing=false) {
  std::mt19937_64 rng(seed);
  std::uniform_real_distribution<double> uniform(0,1);
  const auto pick=[&](int n){return int(rng()%uint64_t(n));};
  std::vector<int> labels(model.domains.size(),0);
  long penalty=relaxSpacing?4:1000000000L;
  auto current=model.energy(labels,penalty);
  FactorSolution best{labels,model.energy(labels),0};
  int wins=0;
  const auto local=[&](const std::vector<int>& factors) {
    long sum=0;for(int f:factors)sum+=model.factors[f].cost(model.factors[f].index(labels),penalty);return sum;
  };
  for(int step=0;step<steps&&!expired();step++) {
    if(step&&step%std::max(1,steps/4)==0) {
      if(relaxSpacing){const long schedule[]={4,16,64,1000000000L};penalty=schedule[std::min(3,step/std::max(1,steps/4))];}
      else labels=best.labels;
      current=model.energy(labels,penalty);
    }
    const double progress=double(step%std::max(1,steps/4))/std::max(1,steps/4);
    const double temperature=6*std::pow(.02/6,progress);
    int blockSize=step%1000==0?4:step%100==0?3:step%8==0?2:1;
    blockSize=std::min(blockSize,int(labels.size()));
    std::vector<int> block;
    if(blockSize>1&&!model.factors.empty()) {
      int chosen=-1;long pressure=-1;
      for(int trial=0;trial<20;trial++) {
        int f=pick(model.factors.size());const auto& factor=model.factors[f];
        if(factor.variables.size()<2||factor.variables.size()>size_t(blockSize))continue;
        long cost=factor.cost(factor.index(labels),penalty);
        if(cost>pressure){chosen=f;pressure=cost;}
      }
      if(chosen>=0)block=model.factors[chosen].variables;
    }
    while(int(block.size())<blockSize) {
      int n=pick(labels.size());if(std::find(block.begin(),block.end(),n)==block.end())block.push_back(n);
    }
    std::vector<int> factors;
    for(int n:block)factors.insert(factors.end(),model.incident[n].begin(),model.incident[n].end());
    std::sort(factors.begin(),factors.end());factors.erase(std::unique(factors.begin(),factors.end()),factors.end());
    const long before=local(factors);
    const auto old=labels;
    if(blockSize==1&&step%3) {
      int n=block[0];labels[n]=pick(model.domains[n]);
      long delta=local(factors)-before;
      if(delta<=0||uniform(rng)<std::exp(-double(delta)/temperature))current+=delta;
      else labels=old;
    } else {
      long minimum=before;auto destination=labels;
      int evaluations=0;
      std::function<void(int)> visit=[&](int level) {
        if(level==int(block.size())) {
          long value=local(factors);evaluations++;
          if(value<minimum){minimum=value;destination=labels;}
          return;
        }
        int n=block[level];
        for(int value=0;value<model.domains[n];value++) {
          if(evaluations%1024==0&&expired())break;
          labels[n]=value;visit(level+1);
        }
      };
      visit(0);labels=destination;current+=minimum-before;
      if(minimum<before&&blockSize>1)wins++;
    }
    if(current<best.energy&&model.spacingCount(labels)==0){best.energy=current;best.labels=labels;}
    if(step%512==0&&current!=model.energy(labels,penalty))throw std::runtime_error("finite factor score drift");
  }
  if(best.energy!=model.energy(best.labels))throw std::runtime_error("finite factor best score drift");
  best.blockWins=wins;return best;
}
}
