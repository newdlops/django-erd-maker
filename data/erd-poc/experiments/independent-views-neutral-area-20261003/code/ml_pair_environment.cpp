// Observes a bounded, uniformly sampled vocabulary of card pairs. The trained
// network selects the pair; geometry evaluates only that selected swap.
#define ERD_COMPONENT_HELPERS_ONLY
#include "ml_component_environment.cpp"

namespace {
struct PairEnvironment:ComponentEnvironment {
  std::vector<std::vector<double>> cached;
  std::vector<double> scales;
  std::vector<std::set<int>> neighbors;
  explicit PairEnvironment(ComponentEnvironment base):ComponentEnvironment(std::move(base)) {
    swapSlots=true;pairActions=true;cached.resize(a.nodes.size());scales.resize(a.nodes.size());neighbors.resize(a.nodes.size());
    for(auto e:b.edges){const int n=owner[e.s],m=owner[e.t];if(n!=m){neighbors[n].insert(m);neighbors[m].insert(n);}}
  }
  const std::vector<double>& nodeFeatures(int n) {
    if(cached[n].empty())cached[n]=features(n,scales[n]);return cached[n];
  }
  std::vector<double> observe(int n,int m) {
    if(n==m||n<0||m<0||n>=int(a.nodes.size())||m>=int(a.nodes.size()))throw std::runtime_error("invalid pair observation");
    const auto& x=nodeFeatures(n);const auto& y=nodeFeatures(m);std::vector<double> f;
    f.insert(f.end(),x.begin(),x.begin()+24);f.insert(f.end(),y.begin(),y.begin()+24);
    const Point delta{a.pos[m].x-a.pos[n].x,a.pos[m].y-a.pos[n].y};
    for(double value:{delta.x/scales[n],delta.y/scales[n],-delta.x/scales[m],-delta.y/scales[m]})f.push_back(std::clamp(value,-8.,8.));
    f.insert(f.end(),x.begin()+56,x.begin()+60);f.insert(f.end(),y.begin()+56,y.begin()+60);
    long common=0;for(int other:neighbors[n])common+=neighbors[m].count(other);
    f.push_back(std::clamp(std::log(scales[n]/scales[m]),-8.,8.));f.push_back(neighbors[n].count(m));
    f.push_back(std::log1p(common));f.push_back(overviewOnly);if(f.size()!=64)throw std::runtime_error("pair feature schema");return f;
  }
  Result propose(int n,int m,bool commit) {
    if(n==m||n<0||m<0||n>=int(a.nodes.size())||m>=int(a.nodes.size()))throw std::runtime_error("invalid model-selected pair");
    const auto r=trial(n,{a.pos[m].x-a.pos[n].x,a.pos[m].y-a.pos[n].y},commit);
    if(r.decodedTarget!=m)throw std::runtime_error("pair decoder selected a different slot");
    if(commit&&r.accepted)for(auto& f:cached)f.clear();return r;
  }
};
void pairDataset(int argc,char** argv) {
  const bool adaptation=arg(argc,argv,"--directory","none")!="none";
  const int graphs=number(argc,argv,"--graphs",64),samples=number(argc,argv,"--samples",16),budget=number(argc,argv,"--budget",5000);
  const double seconds=number(argc,argv,"--seconds",20);const uint64_t seed=number(argc,argv,"--seed",78101);
  if(graphs<8||graphs>96||samples<1||samples>32||budget<1||budget>10000||seconds<=0||seconds>20)throw std::runtime_error("pair data budget");
  std::ofstream out(arg(argc,argv,"--dataset"));out<<std::setprecision(10);std::mt19937_64 rng(seed);
  long actions=0,positive=0,totalGain=0;std::map<std::string,long> reasons;const auto start=std::chrono::steady_clock::now();
  for(int g=0;g<(adaptation?1:graphs);g++) {
    if(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=seconds)break;
    PairEnvironment env(adaptation?load(arg(argc,argv,"--directory")):synthetic(seed+g));
    env.overviewOnly=adaptation?arg(argc,argv,"--overview-only","0")=="1":g%3==0;
    const int count=env.a.nodes.size(),draws=adaptation?budget:count*samples;
    const auto originalA=env.a.pos,originalB=env.b.pos;
    for(int k=0;k<draws;k++) {
      if(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=seconds)break;
      const int n=adaptation?rng()%count:k/samples;int m=rng()%(count-1);if(m>=n)m++;
      const auto f=env.observe(n,m);const auto r=env.propose(n,m,false);
      const long gain=r.accepted?r.gain+(env.overviewOnly?0:r.individualGain):0;
      out<<"{\"graph\":"<<(adaptation?100000:g)<<",\"features\":";array(out,f);
      out<<",\"gain\":"<<gain<<",\"source\":"<<n<<",\"target\":"<<m<<"}\n";
      actions++;positive+=gain>0;totalGain+=gain;reasons[r.reason]++;
    }
    if(env.accepted||env.a.full().visual()!=env.initialVisual||env.b.full().visual()!=env.initialIndividualVisual)throw std::runtime_error("pair collection changed source metrics");
    for(int n=0;n<int(originalA.size());n++)if(distance(originalA[n],env.a.pos[n])>1e-10)throw std::runtime_error("pair collection changed overview positions");
    for(int n=0;n<int(originalB.size());n++)if(distance(originalB[n],env.b.pos[n])>1e-10)throw std::runtime_error("pair collection changed individual positions");
  }
  std::cout<<"{\"domain\":\""<<(adaptation?"Captain non-committing random pair rewards":"synthetic random pair rewards")<<"\",\"actions\":"<<actions<<",\"positive\":"<<positive<<",\"totalPositiveGain\":"<<totalGain
    <<",\"seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<",\"reasons\":{";
  bool first=true;for(auto [key,value]:reasons){if(!first)std::cout<<',';first=false;std::cout<<'"'<<key<<"\":"<<value;}std::cout<<"}}\n";
}
void pairSelfTest() {
  long checks=0,accepted=0;std::mt19937_64 rng(78003);
  for(int g=0;g<8;g++) {
    PairEnvironment env(synthetic(47000+g));env.overviewOnly=g%2;
    for(int k=0;k<32;k++) {
      const int n=rng()%env.a.nodes.size();int m=rng()%(env.a.nodes.size()-1);if(m>=n)m++;
      const auto a=env.a.pos,b=env.b.pos;const long va=env.visual,vb=env.individualVisual;env.observe(n,m);
      const auto r=env.propose(n,m,true);checks++;accepted+=r.accepted;
      if(env.a.full().visual()!=env.visual||env.b.full().visual()!=env.individualVisual||env.visual>va||(!env.overviewOnly&&env.individualVisual>vb))throw std::runtime_error("pair global delta mismatch");
      if(r.accepted){if(distance(env.a.pos[n],a[m])>1e-8||distance(env.a.pos[m],a[n])>1e-8)throw std::runtime_error("pair did not exchange centers");}
      else {for(int i=0;i<int(a.size());i++)if(distance(a[i],env.a.pos[i])>1e-10)throw std::runtime_error("rejected pair moved overview");
        for(int i=0;i<int(b.size());i++)if(distance(b[i],env.b.pos[i])>1e-10)throw std::runtime_error("rejected pair moved members");}
    }
    auto shifted=env;for(auto& f:shifted.cached)f.clear();
    for(State* s:{&shifted.a,&shifted.b}){for(auto& p:s->pos){p.x+=10000;p.y+=20000;}for(auto& r:s->routes)r=segment({r.a.x+10000,r.a.y+20000},{r.b.x+10000,r.b.y+20000});}
    shifted.left+=10000;shifted.right+=10000;shifted.top+=20000;shifted.bottom+=20000;
    const auto x=env.observe(0,1),y=shifted.observe(0,1);for(size_t i=0;i<x.size();i++)if(std::abs(x[i]-y[i])>1e-8)throw std::runtime_error("pair features are not translation invariant");
  }
  if(!accepted)throw std::runtime_error("no accepted pair fixtures");std::cout<<"{\"selfTest\":\"pass\",\"globalComparisons\":"<<checks<<",\"acceptedSwaps\":"<<accepted<<",\"translationInvariant\":true}\n";
}
}
int main(int argc,char**argv) {
  try {
    if(argc==2&&std::string(argv[1])=="--self-test"){pairSelfTest();return 0;}
    if(arg(argc,argv,"--dataset","none")!="none"){pairDataset(argc,argv);return 0;}
    PairEnvironment env(load(arg(argc,argv,"--directory")));env.overviewOnly=arg(argc,argv,"--overview-only","0")=="1";
    const auto output=arg(argc,argv,"--out");std::mt19937_64 rng(number(argc,argv,"--seed",123));
    std::cout<<std::setprecision(10)<<"{\"ready\":true,\"eligible\":"<<env.a.nodes.size()<<",\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<"}"<<std::endl;
    std::string line;while(std::getline(std::cin,line)) {
      std::istringstream command(line);std::string op;command>>op;
      if(op=="OBS") {
        int count=1024;command>>count;if(count<1||count>4096)throw std::runtime_error("pair observation budget");
        count=std::min(count,int(env.a.nodes.size()*(env.a.nodes.size()-1)/2));std::set<std::pair<int,int>> seen;
        std::cout<<"{\"pairs\":[";bool first=true;while(int(seen.size())<count) {
          int n=rng()%env.a.nodes.size(),m=rng()%(env.a.nodes.size()-1);if(m>=n)m++;if(n>m)std::swap(n,m);if(!seen.insert({n,m}).second)continue;
          if(!first)std::cout<<',';first=false;std::cout<<"{\"source\":"<<n<<",\"target\":"<<m<<",\"features\":";array(std::cout,env.observe(n,m));std::cout<<'}';
        }
        std::cout<<"],\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<"}"<<std::endl;
      }else if(op=="TRY") {
        int n,m;if(!(command>>n>>m))throw std::runtime_error("invalid categorical action");const auto r=env.propose(n,m,true);
        std::cout<<"{\"accepted\":"<<(r.accepted?"true":"false")<<",\"legal\":"<<(r.legal?"true":"false")<<",\"gain\":"<<r.gain<<",\"individualGain\":"<<r.individualGain
          <<",\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<",\"reason\":\""<<r.reason<<"\",\"decodedTarget\":"<<r.decodedTarget<<"}"<<std::endl;
      }else if(op=="SAVE"){env.save(output);std::cout<<"{\"saved\":true}"<<std::endl;}
      else if(op=="QUIT")return 0;else throw std::runtime_error("unknown pair command");
    }
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
