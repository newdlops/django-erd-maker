// Reuse the exact two-view geometry and grouping validator. The only action is
// two neural boundary parameters; no coordinate search or endpoint repair.
#define ERD_COMPONENT_HELPERS_ONLY
#include "ml_component_environment.cpp"
#include <tuple>

namespace {
int endpointSide(Point p,Point center,const Node& n) {
  const std::array<double,4> d{{std::abs(p.x-center.x+n.w/2),std::abs(p.x-center.x-n.w/2),
    std::abs(p.y-center.y+n.h/2),std::abs(p.y-center.y-n.h/2)}};
  const int side=std::min_element(d.begin(),d.end())-d.begin();
  if(d[side]>.02)throw std::runtime_error("input endpoint is not on its card");return side;
}
Point boundaryPoint(Point center,const Node& n,int side,double parameter) {
  const double q=std::clamp(parameter,-1.,1.);
  return side<2?Point{rounded(center.x+(side?1:-1)*n.w/2),rounded(center.y+q*n.h/2)}:
    Point{rounded(center.x+q*n.w/2),rounded(center.y+(side==3?1:-1)*n.h/2)};
}
double boundaryParameter(Point p,Point center,const Node& n,int side) {
  return side<2?2*(p.y-center.y)/n.h:2*(p.x-center.x)/n.w;
}
struct PortEnvironment:ComponentEnvironment {
  std::vector<std::array<int,2>> sides;
  explicit PortEnvironment(ComponentEnvironment base):ComponentEnvironment(std::move(base)) {
    portActions=true;
    for(int e=0;e<int(b.edges.size());e++) {const auto v=b.edges[e];
      sides.push_back({endpointSide(b.routes[e].a,b.pos[v.s],b.nodes[v.s]),endpointSide(b.routes[e].b,b.pos[v.t],b.nodes[v.t])});}
  }
  Result propose(int e,Point parameters,bool commit) {
    if(e<0||e>=int(b.edges.size())||!std::isfinite(parameters.x)||!std::isfinite(parameters.y))throw std::runtime_error("invalid neural port action");
    attempts++;const int g=edgeGroup[e];const std::vector<int> ns,ae{g},be{e};
    mark(a,ns,ae,true);mark(b,ns,be,true);const auto ca=cost(a,ns,ae),cb=cost(b,ns,be);
    const auto oldA=a.routes[g],oldB=b.routes[e];const auto oldEdge=a.edges[g],v=b.edges[e];
    if(residualPorts) {
      parameters.x+=boundaryParameter(oldB.a,b.pos[v.s],b.nodes[v.s],sides[e][0]);
      parameters.y+=boundaryParameter(oldB.b,b.pos[v.t],b.nodes[v.t],sides[e][1]);
    }
    b.routes[e]=segment(boundaryPoint(b.pos[v.s],b.nodes[v.s],sides[e][0],parameters.x),
      boundaryPoint(b.pos[v.t],b.nodes[v.t],sides[e][1],parameters.y));
    Result r;r.reason="projection";
    if(project(g)) {
      const auto da=cost(a,ns,ae),db=cost(b,ns,be);
      r.gain=ca.score.visual()-da.score.visual();r.individualGain=cb.score.visual()-db.score.visual();
      r.legal=da.hard<=ca.hard&&db.hard<=cb.hard;
      const bool moved=distance(oldB.a,b.routes[e].a)>1e-8||distance(oldB.b,b.routes[e].b)>1e-8;
      r.accepted=r.legal&&r.gain>=0&&(overviewOnly?r.gain>0||(allowNeutral&&moved):r.individualGain>=0&&(r.gain||r.individualGain||(allowNeutral&&moved)));
      const bool neutral=r.accepted&&r.gain==0&&(overviewOnly||r.individualGain==0);
      r.reason=da.hard>ca.hard?"overview-hard":db.hard>cb.hard?"individual-hard":r.gain<0?"overview-regression":!overviewOnly&&r.individualGain<0?"individual-regression":neutral?"accepted-neutral":r.accepted?"accepted":"equal";
    }
    if(commit&&r.accepted){visual-=r.gain;individualVisual-=r.individualGain;accepted++;acceptedNeutral+=r.gain==0&&(overviewOnly||r.individualGain==0);}
    else {a.routes[g]=oldA;a.edges[g]=oldEdge;b.routes[e]=oldB;}
    mark(a,ns,ae,false);mark(b,ns,be,false);return r;
  }
  std::vector<double> observe(int e) {
    const auto v=b.edges[e];const Point p=b.pos[v.s],q=b.pos[v.t];const auto s=b.nodes[v.s],t=b.nodes[v.t];
    const double len=std::max(1.,distance(p,q)),ux=(q.x-p.x)/len,uy=(q.y-p.y)/len;
    const int g=edgeGroup[e];const std::vector<int> ns,ae{g},be{e};
    mark(a,ns,ae,true);mark(b,ns,be,true);const auto ca=cost(a,ns,ae),cb=cost(b,ns,be);
    mark(a,ns,ae,false);mark(b,ns,be,false);
    std::vector<double> f;const auto add=[&](double x){f.push_back(std::clamp(x,-8.,8.));};
    for(int k=0;k<4;k++)add(sides[e][0]==k);for(int k=0;k<4;k++)add(sides[e][1]==k);
    add(ux);add(uy);add(std::log1p(len/512));add(s.w/len);add(s.h/len);add(t.w/len);add(t.h/len);
    add(boundaryParameter(b.routes[e].a,p,s,sides[e][0]));add(boundaryParameter(b.routes[e].b,q,t,sides[e][1]));
    for(const auto c:{cb,ca}){add(std::log1p(c.score.cross));add(std::log1p(c.score.hit));add(std::log1p(c.hard));}
    add(std::log1p(b.incident[v.s].size()));add(std::log1p(b.incident[v.t].size()));
    std::vector<std::tuple<int,double,int>> nearby;
    for(int n=0;n<int(b.nodes.size());n++)if(n!=v.s&&n!=v.t) {
      const double along=std::clamp(((b.pos[n].x-p.x)*ux+(b.pos[n].y-p.y)*uy)/len,0.,1.);
      const double d=std::round(distance(b.pos[n],{p.x+along*(q.x-p.x),p.y+along*(q.y-p.y)})*1e6)/1e6;
      nearby.push_back({hits(b.routes[e],b.pos[n],b.nodes[n],0)?0:1,d,n});
    }
    std::sort(nearby.begin(),nearby.end());
    for(int k=0;k<5;k++) {
      if(k>=int(nearby.size())){for(int j=0;j<5;j++)add(0);continue;}
      const int n=std::get<2>(nearby[k]);const double dx=b.pos[n].x-p.x,dy=b.pos[n].y-p.y;
      add((dx*ux+dy*uy)/len);add((-dx*uy+dy*ux)/len);add(b.nodes[n].w/len);add(b.nodes[n].h/len);add(std::get<0>(nearby[k])==0);
    }
    std::vector<std::pair<double,int>> lines;
    for(int other=0;other<int(b.edges.size());other++)if(other!=e&&crosses(b.routes[e],b.routes[other])) {
      const auto r=b.routes[other];lines.push_back({std::round(distance(p,{(r.a.x+r.b.x)/2,(r.a.y+r.b.y)/2})*1e6)/1e6,other});
    }
    std::sort(lines.begin(),lines.end());
    for(int k=0;k<3;k++) {
      if(k>=int(lines.size())){for(int j=0;j<4;j++)add(0);continue;}
      const auto r=b.routes[lines[k].second];for(auto endpoint:{r.a,r.b}){add((endpoint.x-p.x)/len);add((endpoint.y-p.y)/len);}
    }
    add(std::log1p(groups[g].size()));add(std::log((s.w+s.h)/(t.w+t.h)));
    if(f.size()!=64)throw std::runtime_error("port feature schema");return f;
  }
  std::vector<std::pair<long,int>> priority() {
    std::vector<std::pair<long,int>> result;const std::vector<int> ns;
    for(int e=0;e<int(b.edges.size());e++) {
      const int g=edgeGroup[e];const std::vector<int> ae{g},be{e};mark(a,ns,ae,true);mark(b,ns,be,true);
      const auto ca=cost(a,ns,ae),cb=cost(b,ns,be);mark(a,ns,ae,false);mark(b,ns,be,false);
      const long pressure=ca.score.visual()+(overviewOnly?0:cb.score.visual());if(pressure)result.push_back({-pressure,e});
    }
    std::sort(result.begin(),result.end());return result;
  }
};
void portDataset(int argc,char**argv) {
  std::ofstream out(arg(argc,argv,"--dataset"));out<<std::setprecision(10);
  const int graphs=number(argc,argv,"--graphs",48),samples=number(argc,argv,"--samples",48);const uint64_t seed=number(argc,argv,"--seed",73101);
  const double seconds=number(argc,argv,"--seconds",20);if(graphs<8||graphs>96||samples<8||samples>64||seconds>20)throw std::runtime_error("port dataset budget");
  const bool residual=arg(argc,argv,"--decoder","ports")=="ports-residual";
  std::mt19937_64 rng(seed);std::uniform_real_distribution<double> unit(-1,1);std::normal_distribution<double> normal;const auto start=std::chrono::steady_clock::now();long states=0,actions=0,positive=0;
  for(int g=0;g<graphs;g++) {
    if(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>seconds)break;
    PortEnvironment env(synthetic(seed+g));env.residualPorts=residual;
    for(auto [pressure,e]:env.priority()) {
      if(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>seconds)break;
      out<<"{\"graph\":"<<g<<",\"features\":";array(out,env.observe(e));out<<",\"actions\":[";
      for(int k=0;k<samples;k++) {
        const double radius=residual?std::exp(std::log(.0005)+(unit(rng)+1)*.5*std::log(3000.)):1.;
        const Point action=residual?Point{normal(rng)*radius,normal(rng)*radius}:Point{unit(rng),unit(rng)};const auto r=env.propose(e,action,false);
        const long gain=r.accepted?r.gain+r.individualGain:0;if(k)out<<',';out<<'['<<action.x<<','<<action.y<<','<<gain<<']';actions++;positive+=gain>0;}
      out<<"]}\n";states++;
    }
    if(env.accepted||env.a.full().visual()!=env.initialVisual||env.b.full().visual()!=env.initialIndividualVisual)throw std::runtime_error("port sampling changed source");
  }
  std::cout<<"{\"states\":"<<states<<",\"randomPortActions\":"<<actions<<",\"positiveActions\":"<<positive<<",\"seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<"}\n";
}
void portAdaptationDataset(int argc,char**argv) {
  PortEnvironment env(load(arg(argc,argv,"--directory")));
  env.residualPorts=arg(argc,argv,"--decoder","ports-residual")=="ports-residual";
  env.overviewOnly=arg(argc,argv,"--overview-only","0")=="1";
  const int samples=number(argc,argv,"--samples",64),budget=number(argc,argv,"--budget",30000);
  const double seconds=number(argc,argv,"--seconds",20);
  if(samples<8||samples>64||budget<samples||budget>50000||seconds<=0||seconds>20)throw std::runtime_error("port adaptation budget");
  std::ofstream out(arg(argc,argv,"--adapt-dataset"));out<<std::setprecision(10);
  std::mt19937_64 rng(number(argc,argv,"--seed",97101));std::uniform_real_distribution<double> unit(0,1);std::normal_distribution<double> normal;
  const auto start=std::chrono::steady_clock::now();const auto positions=env.b.pos;const auto routes=env.b.routes;
  long states=0,actions=0,positive=0;std::map<std::string,long> reasons;
  for(auto [pressure,e]:env.priority()) {
    if(actions+samples>budget||std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=seconds)break;
    out<<"{\"graph\":100000,\"features\":";array(out,env.observe(e));out<<",\"actions\":[";
    for(int k=0;k<samples;k++) {
      const double radius=std::exp(std::log(.00001)+unit(rng)*std::log(30000.));
      const Point action=env.residualPorts?Point{normal(rng)*radius,normal(rng)*radius}:Point{2*unit(rng)-1,2*unit(rng)-1};
      const auto r=env.propose(e,action,false);const long gain=r.accepted?r.gain+(env.overviewOnly?0:r.individualGain):0;
      if(k)out<<',';out<<'['<<action.x<<','<<action.y<<','<<gain<<']';actions++;positive+=gain>0;reasons[r.reason]++;
    }
    out<<"]}\n";states++;
  }
  if(env.accepted||env.a.full().visual()!=env.initialVisual||env.b.full().visual()!=env.initialIndividualVisual)throw std::runtime_error("port adaptation changed source metrics");
  for(int n=0;n<int(positions.size());n++)if(distance(positions[n],env.b.pos[n])>1e-10)throw std::runtime_error("port adaptation moved a card");
  for(int e=0;e<int(routes.size());e++)if(distance(routes[e].a,env.b.routes[e].a)>1e-10||distance(routes[e].b,env.b.routes[e].b)>1e-10)throw std::runtime_error("port adaptation changed a route");
  std::cout<<"{\"domain\":\"Captain non-committing boundary-port rewards\",\"graphId\":100000,\"states\":"<<states<<",\"randomPortActions\":"<<actions
    <<",\"positiveActions\":"<<positive<<",\"sourceGeometryUnchanged\":true,\"seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<",\"reasons\":{";
  bool first=true;for(auto [key,value]:reasons){if(!first)std::cout<<',';first=false;std::cout<<'"'<<key<<"\":"<<value;}std::cout<<"}}\n";
}
void portSelfTest() {
  std::mt19937_64 rng(75502);std::uniform_real_distribution<double> unit(-1.5,1.5);long checked=0,accepted=0,neutral=0;
  for(int g=0;g<16;g++) {
    PortEnvironment env(synthetic(42000+g%8));const auto aPos=env.a.pos,bPos=env.b.pos;env.overviewOnly=g%2;env.residualPorts=g%8>=4;env.allowNeutral=g>=8;
    for(int e=0;e<std::min(32,int(env.b.edges.size()));e++)for(int k=0;k<4;k++) {
      env.observe(e);const long before=env.visual,beforeIndividual=env.individualVisual;const auto r=env.propose(e,{unit(rng),unit(rng)},true);
      if(env.a.full().visual()!=env.visual||env.b.full().visual()!=env.individualVisual||env.visual>before||(!env.overviewOnly&&env.individualVisual>beforeIndividual))throw std::runtime_error("port delta/objective mismatch");
      accepted+=r.accepted;neutral+=r.accepted&&r.gain==0&&(env.overviewOnly||r.individualGain==0);checked++;
    }
    for(int n=0;n<int(aPos.size());n++)if(distance(aPos[n],env.a.pos[n])>1e-10)throw std::runtime_error("port action moved a card");
    for(int n=0;n<int(bPos.size());n++)if(distance(bPos[n],env.b.pos[n])>1e-10)throw std::runtime_error("port action moved a model");
    auto shifted=env;for(State* s:{&shifted.a,&shifted.b}){for(auto& p:s->pos){p.x+=10000;p.y+=20000;}for(auto& r:s->routes)r=segment({r.a.x+10000,r.a.y+20000},{r.b.x+10000,r.b.y+20000});}
    for(int e=0;e<8;e++){auto f=env.observe(e),h=shifted.observe(e);for(size_t i=0;i<f.size();i++)if(std::abs(f[i]-h[i])>1e-8)throw std::runtime_error("port features are not translation invariant");}
  }
  if(!accepted||!neutral)throw std::runtime_error("port fixture accepted no improving or neutral actions");
  std::cout<<"{\"selfTest\":\"pass\",\"globalDeltaComparisons\":"<<checked<<",\"acceptedPortActions\":"<<accepted<<",\"acceptedNeutralActions\":"<<neutral<<",\"positionsPreserved\":true,\"translationInvariance\":true}\n";
}
}
int main(int argc,char**argv) {
  try {
    if(argc==2&&std::string(argv[1])=="--self-test"){portSelfTest();return 0;}
    if(arg(argc,argv,"--adapt-dataset","none")!="none"){portAdaptationDataset(argc,argv);return 0;}
    if(arg(argc,argv,"--dataset","none")!="none"){portDataset(argc,argv);return 0;}
    PortEnvironment env(load(arg(argc,argv,"--directory")));env.overviewOnly=arg(argc,argv,"--overview-only","0")=="1";
    env.allowNeutral=arg(argc,argv,"--allow-neutral","0")=="1";
    env.residualPorts=arg(argc,argv,"--decoder","ports")=="ports-residual";
    const auto output=arg(argc,argv,"--out");
    std::cout<<std::setprecision(10)<<"{\"ready\":true,\"eligible\":"<<env.b.edges.size()<<",\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<"}"<<std::endl;
    std::string line;while(std::getline(std::cin,line)) {
      std::istringstream command(line);std::string op;command>>op;
      if(op=="OBS") {
        int limit=192;command>>limit;limit=std::clamp(limit,1,1800);const auto order=env.priority();bool first=true;int emitted=0;
        std::cout<<"{\"nodes\":[";for(auto [pressure,e]:order){if(emitted++>=limit)break;if(!first)std::cout<<',';first=false;
          std::cout<<"{\"id\":"<<e<<",\"actionScale\":1,\"features\":";array(std::cout,env.observe(e));std::cout<<'}';}
        std::cout<<"],\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<"}"<<std::endl;
      }else if(op=="TRY") {
        int e;Point p;if(!(command>>e>>p.x>>p.y))throw std::runtime_error("invalid neural port parameters");const auto r=env.propose(e,p,true);
        std::cout<<"{\"accepted\":"<<(r.accepted?"true":"false")<<",\"legal\":"<<(r.legal?"true":"false")<<",\"gain\":"<<r.gain<<",\"individualGain\":"<<r.individualGain
          <<",\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<",\"reason\":\""<<r.reason<<"\"}"<<std::endl;
      }else if(op=="SAVE"){env.save(output);std::cout<<"{\"saved\":true}"<<std::endl;}
      else if(op=="QUIT")return 0;else throw std::runtime_error("unknown port command");
    }
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
