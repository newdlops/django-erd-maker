// Recombine two complete scenes using sparse, exact binary geometry factors.
// Each real card selects one parent's position and incident boundary ports.
// Mixed-parent routes retain the original relationship and have no bends.
#include <array>
#include "constrained_crossover_solver.h"

namespace {
class SceneCrossover {
  State& s;
  const State& other;
  std::vector<std::array<Segment,4>> routes;
  std::vector<std::array<Point,2>> positions;
  std::vector<int> labels;
  std::function<bool()> expired;
  bool repairPorts;
  long constant=0;
  size_t cells=0;
  FiniteLabelFactors model;
  struct Value {int visual=0,invalid=0;};
  Point point(int n) const{return positions[n][labels[n]];}
  const Segment& route(int e) const{return routes[e][labels[s.edges[e].s]*2+labels[s.edges[e].t]];}
  template<class Evaluate> bool add(std::vector<int> variables,Evaluate evaluate) {
    std::sort(variables.begin(),variables.end());variables.erase(std::unique(variables.begin(),variables.end()),variables.end());
    DenseLabelFactor factor;factor.variables=std::move(variables);
    int size=1;factor.strides.resize(factor.variables.size());
    for(int i=int(factor.variables.size())-1;i>=0;i--){factor.strides[i]=size;size*=2;}
    factor.values.resize(size);factor.spacing.resize(size);bool varying=false,invalid=false;
    for(int index=0;index<size;index++) {
      for(size_t i=0;i<factor.variables.size();i++)labels[factor.variables[i]]=(index/factor.strides[i])%2;
      auto value=evaluate();factor.values[index]=value.visual;factor.spacing[index]=value.invalid;invalid=invalid||value.invalid;
      varying=varying||value.visual!=factor.values[0]||value.invalid!=factor.spacing[0];
    }
    if(!varying) {
      if(invalid)throw std::runtime_error("both crossover parents violate a hard constraint");
      constant+=factor.values[0];return true;
    }
    if(cells+size>2000000||model.factors.size()>=150000)return false;
    cells+=size;if(!invalid)factor.spacing.clear();
    for(int n:factor.variables)model.incident[n].push_back(model.factors.size());
    model.factors.push_back(std::move(factor));return true;
  }
  void apply(const std::vector<int>& chosen) {
    labels=chosen;
    for(int n=0;n<int(s.nodes.size());n++)s.pos[n]=point(n);
    for(int e=0;e<int(s.edges.size());e++)s.routes[e]=route(e);
  }
  long violations(bool adjacent=true) const {
    long total=s.full().spacing;
    for(int e=0;e<int(s.edges.size());e++)for(int n:{s.edges[e].s,s.edges[e].t})total+=hits(s.routes[e],s.pos[n],s.nodes[n],-.02);
    if(adjacent)for(const auto& incident:s.incident)for(size_t i=0;i<incident.size();i++)for(size_t j=i+1;j<incident.size();j++)
      total+=crosses(s.routes[incident[i]],s.routes[incident[j]]);
    return total;
  }
public:
  SceneCrossover(State& state,const State& alternative,std::function<bool()> deadline,bool repair)
    :s(state),other(alternative),labels(s.nodes.size(),0),expired(std::move(deadline)),repairPorts(repair) {
    for(int n=0;n<int(s.nodes.size());n++)positions.push_back({s.pos[n],other.pos[n]});
    for(int e=0;e<int(s.edges.size());e++)routes.push_back({s.routes[e],segment(s.routes[e].a,other.routes[e].b),
      segment(other.routes[e].a,s.routes[e].b),other.routes[e]});
    if(repairPorts)for(int e=0;e<int(s.edges.size());e++)for(int choice:{1,2}) {
      const auto& edge=s.edges[e];auto a=positions[edge.s][choice/2],b=positions[edge.t][choice%2];
      if(hits(routes[e][choice],a,s.nodes[edge.s],-.02)||hits(routes[e][choice],b,s.nodes[edge.t],-.02))
        routes[e][choice]=segment(port(a,s.nodes[edge.s],b),port(b,s.nodes[edge.t],a));
    }
    model.domains.assign(s.nodes.size(),2);model.incident.resize(s.nodes.size());
  }
  bool build() {
    std::vector<Segment> bounds;
    for(const auto& choices:routes) {
      auto bound=choices[0];
      for(const auto& line:choices){bound.l=std::min(bound.l,line.l);bound.r=std::max(bound.r,line.r);bound.t=std::min(bound.t,line.t);bound.bottom=std::max(bound.bottom,line.bottom);}
      bounds.push_back(bound);
    }
    for(int e=0;e<int(s.edges.size());e++) {
      if(expired())return false;
      const auto edge=s.edges[e];
      if(!add({edge.s,edge.t},[&]{Value value;for(int n:{edge.s,edge.t})value.invalid+=hits(route(e),point(n),s.nodes[n],-.02);return value;}))return false;
      for(int f=e+1;f<int(s.edges.size());f++) {
        auto a=bounds[e],b=bounds[f];if(a.l>b.r||b.l>a.r||a.t>b.bottom||b.t>a.bottom)continue;
        const auto peer=s.edges[f];bool adjacent=edge.s==peer.s||edge.s==peer.t||edge.t==peer.s||edge.t==peer.t;
        if(!add({edge.s,edge.t,peer.s,peer.t},[&]{int count=crosses(route(e),route(f));return Value{count,adjacent&&!repairPorts?count:0};}))return false;
      }
      for(int n=0;n<int(s.nodes.size());n++)if(n!=edge.s&&n!=edge.t) {
        const auto a=bounds[e];auto p=positions[n][0],q=positions[n][1];const auto& node=s.nodes[n];
        if(a.r<=std::min(p.x,q.x)-node.w*.5-10||a.l>=std::max(p.x,q.x)+node.w*.5+10
          ||a.bottom<=std::min(p.y,q.y)-node.h*.5-10||a.t>=std::max(p.y,q.y)+node.h*.5+10)continue;
        if(!add({edge.s,edge.t,n},[&]{return Value{hits(route(e),point(n),s.nodes[n]),0};}))return false;
      }
    }
    for(int n=0;n<int(s.nodes.size());n++) {
      if(expired())return false;
      for(int m=n+1;m<int(s.nodes.size());m++)if(!add({n,m},[&] {
        auto p=point(n),q=point(m);double dx=std::abs(p.x-q.x)-(s.nodes[n].w+s.nodes[m].w)*.5;
        double dy=std::abs(p.y-q.y)-(s.nodes[n].h+s.nodes[m].h)*.5;
        return Value{dx<0&&dy<0,dx<55.99&&dy<41.99};
      }))return false;
    }
    std::cerr<<"crossover factors="<<model.factors.size()<<" cells="<<cells<<" constant="<<constant<<'\n';return true;
  }
  void audit(uint64_t seed) {
    const auto originalPos=s.pos;const auto originalRoutes=s.routes;
    std::mt19937_64 rng(seed);
    for(int trial=0;trial<8&&!expired();trial++) {
      std::vector<int> chosen(labels.size(),trial==1?1:0);
      if(trial>1)for(auto& label:chosen)label=int(rng()%2);
      apply(chosen);
      if(model.energy(chosen,0)+constant!=s.full().visual()||model.spacingCount(chosen)!=violations(!repairPorts))
        throw std::runtime_error("crossover full-scene factor drift");
    }
    s.pos=originalPos;s.routes=originalRoutes;
  }
  Score run(int steps,uint64_t seed,bool relax,long portSteps,const std::function<double()>& elapsed,const std::string& output) {
    auto best=s.full();const auto originalPos=s.pos;const auto originalRoutes=s.routes;
    if(!build()){std::cerr<<"crossover construction incomplete; retain original\n";return best;}
    audit(seed);
    auto solution=solveCrossoverFactors(model,s.groups,steps,seed,expired,relax);
    if(solution.energy+constant>best.visual())throw std::runtime_error("crossover worsened its feasible baseline");
    apply(solution.labels);auto candidate=s.full();
    if(candidate.visual()!=solution.energy+constant||candidate.spacing||violations(!repairPorts))throw std::runtime_error("invalid crossover result");
    int selected=std::count(solution.labels.begin(),solution.labels.end(),1);
    std::cerr<<"crossover selected="<<selected<<" variables="<<labels.size()<<" predicted="<<solution.energy+constant<<" blockWins="<<solution.blockWins<<'\n';
    if(repairPorts&&selected) {
      long adjacent=violations()-violations(false);int repairs=repairAdjacentBoundaryPorts(s,expired);
      bool reset=violations()>0;if(reset)s.allRoutes();
      candidate=s.full();std::mt19937_64 rng(seed);
      candidate=refineBoundaryPorts(s,candidate,portSteps,rng,expired,elapsed);
      std::cerr<<"crossover port-repair adjacentBefore="<<adjacent<<" repairs="<<repairs<<" centerReset="<<reset<<" actual="<<candidate.visual()<<'\n';
      if(violations())throw std::runtime_error("crossover port repair left invalid geometry");
    }
    if(better(candidate,best)){best=candidate;s.save(output);report("crossover-best",steps,best,elapsed());}
    else{s.pos=originalPos;s.routes=originalRoutes;}
    return best;
  }
};

Score runSceneCrossover(State& s,int argc,char** argv,const std::function<bool()>& expired,
                        const std::function<double()>& elapsed,const std::string& output) {
  const int steps=int(number(argc,argv,"--crossover-steps",100000));
  const long portSteps=long(number(argc,argv,"--crossover-port-steps",200000));
  const auto portPath=arg(argc,argv,"--crossover-ports","none");
  if(steps<0||portSteps<0||!s.sourceOffsets.empty()||portPath=="none")throw std::runtime_error("crossover requires positions and actual boundary ports");
  State other=s;std::ifstream input(arg(argc,argv,"--crossover-positions"));
  if(!input)throw std::runtime_error("cannot read crossover positions");
  std::unordered_map<std::string,int> ids;for(int n=0;n<int(s.nodes.size());n++)ids[s.nodes[n].id]=n;
  std::vector<char> present(s.nodes.size(),false);std::string row;
  while(std::getline(input,row)) {
    auto fields=split(row);
    if(fields.size()!=3||!ids.count(fields[0])||present[ids.at(fields[0])])throw std::runtime_error("invalid crossover position row");
    int n=ids.at(fields[0]);present[n]=true;other.pos[n]={std::stod(fields[1]),std::stod(fields[2])};
    if(!other.inside(n,other.pos[n]))throw std::runtime_error("crossover card is outside the fixed canvas");
  }
  if(std::find(present.begin(),present.end(),false)!=present.end())throw std::runtime_error("missing crossover positions");
  other.allRoutes();auto alternative=loadBoundaryPorts(other,portPath,other.full());
  if(alternative.overlap||alternative.spacing)throw std::runtime_error("crossover parent violates card spacing");
  SceneCrossover search(s,other,expired,number(argc,argv,"--crossover-repair-ports",1)!=0);
  return search.run(steps,uint64_t(number(argc,argv,"--seed",42)),number(argc,argv,"--crossover-relax",0)!=0,portSteps,elapsed,output);
}
}
