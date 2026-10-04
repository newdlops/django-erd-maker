// A finite unary/pairwise energy. Exhaustive two- and three-variable moves
// can cross barriers that no single-variable improving move can cross.
// The search is bounded and does not claim a global optimum.
namespace {
struct PairwiseLabels {
  std::vector<std::vector<int>> unary;
  std::vector<std::vector<int>> pairs;
  int count() const {return int(unary.size());}
  int pair(int i,int a,int j,int b) const {
    if(i>j){std::swap(i,j);std::swap(a,b);}
    return pairs[i*count()+j][a*unary[j].size()+b];
  }
  long energy(const std::vector<int>& labels) const {
    long value=0;
    for(int i=0;i<count();i++) {
      value+=unary[i][labels[i]];
      for(int j=i+1;j<count();j++)value+=pair(i,labels[i],j,labels[j]);
    }
    return value;
  }
  std::vector<long> site(int i,const std::vector<int>& labels,int skip=-1,int skip2=-1) const {
    std::vector<long> costs(unary[i].begin(),unary[i].end());
    for(int a=0;a<int(costs.size());a++)for(int j=0;j<count();j++)if(j!=i&&j!=skip&&j!=skip2)
      costs[a]+=pair(i,a,j,labels[j]);
    return costs;
  }
};

struct LabelSolution {
  std::vector<int> labels;
  long energy=0;
  int pairWins=0,tripleWins=0;
};

LabelSolution solvePairwiseLabels(const PairwiseLabels& model,int steps,uint64_t seed,
                                 const std::function<bool()>& expired) {
  std::mt19937_64 rng(seed);
  std::uniform_real_distribution<double> uniform(0,1);
  const auto pick=[&](int size){return int(rng()%uint64_t(size));};
  std::vector<int> labels(model.count(),0);
  long current=model.energy(labels);
  LabelSolution best{labels,current,0,0};
  int pairWins=0,tripleWins=0;
  for(int step=0;step<steps&&!expired();step++) {
    if(step&&step%std::max(1,steps/4)==0){labels=best.labels;current=best.energy;}
    double progress=double(step%std::max(1,steps/4))/std::max(1,steps/4);
    double temperature=6*std::pow(.02/6,progress);
    int i=pick(model.count()),j=pick(model.count()),k=pick(model.count());
    if(model.count()>=3&&step%160==0) {
      while(j==i)j=pick(model.count());
      while(k==i||k==j)k=pick(model.count());
      const auto u=model.site(i,labels,j,k),v=model.site(j,labels,i,k),w=model.site(k,labels,i,j);
      const auto cost=[&](int a,int b,int c) {return u[a]+v[b]+w[c]+model.pair(i,a,j,b)+model.pair(i,a,k,c)+model.pair(j,b,k,c);};
      int ai=labels[i],bj=labels[j],ck=labels[k];
      long old=cost(ai,bj,ck),minimum=old;
      for(int a=0;a<int(u.size());a++)for(int b=0;b<int(v.size());b++) {
        long base=u[a]+v[b]+model.pair(i,a,j,b);
        for(int c=0;c<int(w.size());c++) {
          long value=base+w[c]+model.pair(i,a,k,c)+model.pair(j,b,k,c);
          if(value<minimum){minimum=value;ai=a;bj=b;ck=c;}
        }
      }
      if(minimum<old){labels[i]=ai;labels[j]=bj;labels[k]=ck;current+=minimum-old;tripleWins++;}
    } else if(model.count()>=2&&step%8==0) {
      while(j==i)j=pick(model.count());
      const auto u=model.site(i,labels,j),v=model.site(j,labels,i);
      long old=u[labels[i]]+v[labels[j]]+model.pair(i,labels[i],j,labels[j]),minimum=old;
      int ai=labels[i],bj=labels[j];
      for(int a=0;a<int(u.size());a++)for(int b=0;b<int(v.size());b++) {
        long value=u[a]+v[b]+model.pair(i,a,j,b);
        if(value<minimum){minimum=value;ai=a;bj=b;}
      }
      if(minimum<old){labels[i]=ai;labels[j]=bj;current+=minimum-old;pairWins++;}
    } else {
      auto costs=model.site(i,labels);
      int a=pick(costs.size());
      if(step%3==0)a=int(std::min_element(costs.begin(),costs.end())-costs.begin());
      long delta=costs[a]-costs[labels[i]];
      if(delta<=0||uniform(rng)<std::exp(-double(delta)/temperature)){labels[i]=a;current+=delta;}
    }
    if(current<best.energy){best.labels=labels;best.energy=current;}
    if(step%512==0&&current!=model.energy(labels))throw std::runtime_error("pairwise label score drift");
  }
  if(best.energy!=model.energy(best.labels))throw std::runtime_error("pairwise best score drift");
  best.pairWins=pairWins;best.tripleWins=tripleWins;
  return best;
}
}
