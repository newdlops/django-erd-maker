// A neural two-parameter spacing controller. Every scale comes from the model;
// bounds reject proposals without clipping their area or searching alternatives.
#define ERD_COMPONENT_HELPERS_ONLY
#include "ml_component_environment.cpp"

namespace {
struct Bounds {double l=1e100,r=-1e100,t=1e100,b=-1e100;double area()const{return (r-l)*(b-t);}};
Bounds bounds(const State& s) {
  Bounds b;for(int n=0;n<int(s.nodes.size());n++){b.l=std::min(b.l,s.pos[n].x-s.nodes[n].w/2);b.r=std::max(b.r,s.pos[n].x+s.nodes[n].w/2);
    b.t=std::min(b.t,s.pos[n].y-s.nodes[n].h/2);b.b=std::max(b.b,s.pos[n].y+s.nodes[n].h/2);}return b;
}
struct GlobalEnvironment:ComponentEnvironment {
  explicit GlobalEnvironment(ComponentEnvironment base,double limit,bool attached=false):ComponentEnvironment(std::move(base)){globalScale=true;bboxLimit=limit;attachAllPorts=attached;}
  Result propose(Point parameters,bool commit) {
    if(!std::isfinite(parameters.x)||!std::isfinite(parameters.y))throw std::runtime_error("invalid global neural action");
    attempts++;const auto frame=bounds(a);const Point center{(frame.l+frame.r)/2,(frame.t+frame.b)/2};
    const double sx=std::exp(std::clamp(parameters.x,-.5,.5)),sy=std::exp(std::clamp(parameters.y,-.5,.5));
    const auto oldA=a,oldB=b;Result r;r.reason="area";
    for(int n=0;n<int(components.size());n++) {
      const Point next{rounded(center.x+(a.pos[n].x-center.x)*sx),rounded(center.y+(a.pos[n].y-center.y)*sy)};
      const Point delta{next.x-a.pos[n].x,next.y-a.pos[n].y};a.pos[n]=next;
      for(int m:components[n]){b.pos[m].x+=delta.x;b.pos[m].y+=delta.y;}
    }
    if(bounds(a).area()<=bboxLimit&&bounds(b).area()<=bboxLimit) {
      if(attachAllPorts)for(int e=0;e<int(b.edges.size());e++) {
        const auto edge=b.edges[e];const auto line=oldB.routes[e];
        b.routes[e]=segment({rounded(line.a.x+b.pos[edge.s].x-oldB.pos[edge.s].x),rounded(line.a.y+b.pos[edge.s].y-oldB.pos[edge.s].y)},
          {rounded(line.b.x+b.pos[edge.t].x-oldB.pos[edge.t].x),rounded(line.b.y+b.pos[edge.t].y-oldB.pos[edge.t].y)});
      } else b.allRoutes();
      bool projected=true;for(int g=0;g<int(groups.size());g++)projected=project(g)&&projected;
      r.reason=projected?"spacing":"projection";
      if(projected) {
        const auto ca=a.full(),cb=b.full();
        if(!ca.overlap&&!ca.spacing&&!cb.overlap&&!cb.spacing) {
          const long ha=hardScore(a),hb=hardScore(b);
          r.gain=visual-ca.visual();r.individualGain=individualVisual-cb.visual();
          r.legal=ha<=hardScore(oldA)&&hb<=hardScore(oldB);
          if(areaExploration) {
            r.accepted=r.legal&&bounds(a).area()>frame.area()+.01&&ca.visual()<=initialVisual+temporaryRegressionBudget
              &&(overviewOnly||cb.visual()<=initialIndividualVisual+temporaryRegressionBudget);
            r.reason=!r.legal?"hard":bounds(a).area()<=frame.area()+.01?"area-not-increased":r.accepted?"accepted-area-exploration":"regression-ceiling";
          }else {
            r.accepted=r.legal&&r.gain>=0&&(overviewOnly?r.gain>0:r.individualGain>=0&&(r.gain||r.individualGain));
            r.reason=!r.legal?"hard":r.gain<0?"overview-regression":!overviewOnly&&r.individualGain<0?"individual-regression":r.accepted?"accepted":"equal";
          }
        }
      }
    }
    if(commit&&r.accepted){visual-=r.gain;individualVisual-=r.individualGain;accepted++;}
    else {a=oldA;b=oldB;}
    return r;
  }
  std::vector<double> observe() {
    std::vector<long> ap,bp;a.full(&ap);b.full(&bp);for(int n=0;n<int(ap.size());n++)for(int m:components[n])ap[n]+=bp[m];
    std::vector<int> order(ap.size());std::iota(order.begin(),order.end(),0);std::stable_sort(order.begin(),order.end(),[&](int n,int m){return ap[n]>ap[m];});
    const int count=std::min(128,int(order.size()));std::vector<double> mean(32),square(24);
    for(int i=0;i<count;i++){double scale;const auto f=features(order[i],scale);for(int k=0;k<32;k++)mean[k]+=f[k]/count;for(int k=0;k<24;k++)square[k]+=f[k]*f[k]/count;}
    std::vector<double> out=mean;for(int k=0;k<24;k++)out.push_back(std::sqrt(std::max(0.,square[k]-mean[k]*mean[k])));
    const auto box=bounds(a);double occupied=0;for(const auto n:a.nodes)occupied+=n.w*n.h;
    out.push_back(std::log((box.r-box.l)/(box.b-box.t)));out.push_back(box.area()/bboxLimit);out.push_back(occupied/box.area());
    out.push_back(std::log1p(a.nodes.size())/8);out.push_back(std::log1p(a.edges.size())/8);
    out.push_back(std::log1p(visual)/std::max(1.,2*std::log1p(a.edges.size())));
    out.push_back(std::log1p(individualVisual)/std::max(1.,2*std::log1p(b.edges.size())));out.push_back(overviewOnly);
    if(out.size()!=64)throw std::runtime_error("global feature schema");return out;
  }
  void save(const std::string& file) {
    if(bounds(a).area()>bboxLimit||bounds(b).area()>bboxLimit)throw std::runtime_error("area budget exceeded");ComponentEnvironment::save(file);
  }
};
void globalDataset(int argc,char**argv) {
  std::ofstream out(arg(argc,argv,"--dataset"));out<<std::setprecision(10);
  const int graphs=number(argc,argv,"--graphs",64),samples=number(argc,argv,"--samples",96);const uint64_t seed=number(argc,argv,"--seed",75101);
  if(graphs<8||graphs>128||samples<8||samples>128)throw std::runtime_error("global dataset budget");
  std::mt19937_64 rng(seed);std::uniform_real_distribution<double> unit(0,1);long positive=0,total=0;auto started=std::chrono::steady_clock::now();
  for(int g=0;g<graphs;g++) {
    if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>20)break;
    auto base=synthetic(seed+g);const double limit=bounds(base.a).area()*(1.05+.45*unit(rng));GlobalEnvironment env(std::move(base),limit,arg(argc,argv,"--decoder","")=="global-scale-attached");env.overviewOnly=g%3==0;
    out<<"{\"graph\":"<<g<<",\"features\":";array(out,env.observe());out<<",\"actions\":[";
    for(int k=0;k<samples;k++){const Point p{-.15+.5*unit(rng),-.15+.5*unit(rng)};const auto r=env.propose(p,false);const long gain=r.accepted?r.gain+(env.overviewOnly?0:r.individualGain):0;
      if(k)out<<',';out<<'['<<p.x<<','<<p.y<<','<<gain<<']';positive+=gain>0;total++;}
    out<<"]}\n";if(env.accepted||env.visual!=env.a.full().visual()||env.individualVisual!=env.b.full().visual())throw std::runtime_error("global sampling changed source");
  }
  std::cout<<"{\"randomGlobalActions\":"<<total<<",\"positiveActions\":"<<positive<<",\"seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<"}\n";
}
void globalSelfTest() {
  long checked=0,accepted=0,regressions=0;std::mt19937_64 rng(75503);std::uniform_real_distribution<double> unit(-.2,.25);
  for(int g=0;g<24;g++) {
    auto base=synthetic(43000+g%8);const double limit=bounds(base.a).area()*1.4;GlobalEnvironment env(std::move(base),limit,g>=8&&g<16);env.overviewOnly=g%2;
    if(g>=16){env.areaExploration=true;env.temporaryRegressionBudget=200;}
    for(int k=0;k<32;k++) {
      const auto oldA=env.a,oldB=env.b;const long va=env.visual,vb=env.individualVisual;
      const auto r=env.propose({unit(rng),unit(rng)},true);checked++;accepted+=r.accepted;
      regressions+=r.accepted&&(env.visual>va||(!env.overviewOnly&&env.individualVisual>vb));
      const long cap=env.areaExploration?env.initialVisual+env.temporaryRegressionBudget:va;
      const long individualCap=env.areaExploration?env.initialIndividualVisual+env.temporaryRegressionBudget:vb;
      if(env.a.full().visual()!=env.visual||env.b.full().visual()!=env.individualVisual||env.visual>cap||(!env.overviewOnly&&env.individualVisual>individualCap)||bounds(env.a).area()>limit)throw std::runtime_error("global delta/budget mismatch");
      if(env.areaExploration&&r.accepted&&bounds(env.a).area()<=bounds(oldA).area()+.01)throw std::runtime_error("area exploration did not increase space");
      if(!r.accepted){for(int n=0;n<int(oldA.pos.size());n++)if(distance(oldA.pos[n],env.a.pos[n])>1e-10)throw std::runtime_error("rejected global action moved cards");}
      for(int n=0;n<int(env.components.size());n++)for(int m:env.components[n])
        if(std::abs((env.b.pos[m].x-env.a.pos[n].x)-(oldB.pos[m].x-oldA.pos[n].x))>1e-8||std::abs((env.b.pos[m].y-env.a.pos[n].y)-(oldB.pos[m].y-oldA.pos[n].y))>1e-8)throw std::runtime_error("global action changed internal Leaf geometry");
      if(r.accepted&&env.attachAllPorts)for(int e=0;e<int(env.b.edges.size());e++) {
        const auto edge=env.b.edges[e];
        if(std::abs((env.b.routes[e].a.x-env.b.pos[edge.s].x)-(oldB.routes[e].a.x-oldB.pos[edge.s].x))>.011||
          std::abs((env.b.routes[e].a.y-env.b.pos[edge.s].y)-(oldB.routes[e].a.y-oldB.pos[edge.s].y))>.011||
          std::abs((env.b.routes[e].b.x-env.b.pos[edge.t].x)-(oldB.routes[e].b.x-oldB.pos[edge.t].x))>.011||
          std::abs((env.b.routes[e].b.y-env.b.pos[edge.t].y)-(oldB.routes[e].b.y-oldB.pos[edge.t].y))>.011)throw std::runtime_error("global action detached a boundary port");
      }
    }
    auto shifted=env;for(State* s:{&shifted.a,&shifted.b}){for(auto& p:s->pos){p.x+=10000;p.y+=20000;}for(auto& r:s->routes)r=segment({r.a.x+10000,r.a.y+20000},{r.b.x+10000,r.b.y+20000});}
    shifted.left+=10000;shifted.right+=10000;shifted.top+=20000;shifted.bottom+=20000;
    const auto f=env.observe(),h=shifted.observe();for(size_t k=0;k<f.size();k++)if(std::abs(f[k]-h[k])>1e-8)throw std::runtime_error("global features are not translation invariant");
  }
  if(!accepted)throw std::runtime_error("global fixture accepted no actions");if(!regressions)throw std::runtime_error("no temporary regression fixture");
  std::cout<<"{\"selfTest\":\"pass\",\"globalComparisons\":"<<checked<<",\"acceptedGlobalActions\":"<<accepted<<",\"boundedTemporaryRegressions\":"<<regressions<<",\"rigidLeafGeometry\":true,\"areaBudget\":true}\n";
}
}
int main(int argc,char**argv) {
  try {
    if(argc==2&&std::string(argv[1])=="--self-test"){globalSelfTest();return 0;}
    if(arg(argc,argv,"--dataset","none")!="none"){globalDataset(argc,argv);return 0;}
    const double limit=number(argc,argv,"--bbox-limit",1.5e9);if(limit<=0||limit>1.5e9)throw std::runtime_error("global area exceeds product budget");
    GlobalEnvironment env(load(arg(argc,argv,"--directory")),limit,arg(argc,argv,"--decoder","")=="global-scale-attached");env.overviewOnly=arg(argc,argv,"--overview-only","0")=="1";const auto output=arg(argc,argv,"--out");
    env.areaExploration=arg(argc,argv,"--area-exploration","0")=="1";
    if(env.areaExploration){if(env.attachAllPorts)throw std::runtime_error("area exploration uses the ray checkpoint");env.temporaryRegressionBudget=200;}
    std::cout<<std::setprecision(10)<<"{\"ready\":true,\"eligible\":1,\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<"}"<<std::endl;
    std::string line;while(std::getline(std::cin,line)) {
      std::istringstream command(line);std::string op;command>>op;
      if(op=="OBS") {std::cout<<"{\"nodes\":[{\"id\":0,\"actionScale\":1,\"features\":";array(std::cout,env.observe());std::cout<<"}],\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<"}"<<std::endl;}
      else if(op=="TRY") {int node;Point p;if(!(command>>node>>p.x>>p.y)||node!=0)throw std::runtime_error("invalid global action");const auto r=env.propose(p,true);
        std::cout<<"{\"accepted\":"<<(r.accepted?"true":"false")<<",\"legal\":"<<(r.legal?"true":"false")<<",\"gain\":"<<r.gain<<",\"individualGain\":"<<r.individualGain
          <<",\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<",\"reason\":\""<<r.reason<<"\"}"<<std::endl;}
      else if(op=="SAVE"){env.save(output);std::cout<<"{\"saved\":true}"<<std::endl;}
      else if(op=="QUIT")return 0;else throw std::runtime_error("unknown global command");
    }return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
