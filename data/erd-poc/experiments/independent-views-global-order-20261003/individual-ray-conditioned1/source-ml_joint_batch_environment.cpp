// Exact, non-searching validator for a complete batch of neural translations.
// Every batch is decoded from the same source; only model outputs move cards.
#define ERD_COMPONENT_HELPERS_ONLY
#include "ml_component_environment.cpp"

namespace {
struct BatchResult {bool legal=false;const char* reason="frame";long visual=0,individual=0,hard=0,individualHard=0,spacing=0;};
Point perimeterPoint(double phase,double w,double h) {
  double t=phase-std::floor(phase);t*=2*(w+h);
  if(t<w)return {t-w/2,-h/2};
  if(t<w+h)return {w/2,t-w-h/2};
  if(t<2*w+h)return {w/2-(t-w-h),h/2};
  return {-w/2,h/2-(t-2*w-h)};
}
Point perimeterShift(Point original,Point center,double w,double h,double action) {
  if(!std::isfinite(action)||std::abs(action)>.500000001)throw std::runtime_error("invalid neural perimeter offset");
  const double x=original.x-center.x,y=original.y-center.y;
  const double distances[]={std::abs(y+h/2),std::abs(x-w/2),std::abs(y-h/2),std::abs(x+w/2)};
  const double phases[]={x+w/2,w+y+h/2,w+h+w/2-x,2*w+h+h/2-y};
  int side=0;for(int k=1;k<4;k++)if(distances[k]<distances[side])side=k;
  const double phase=phases[side]/(2*(w+h));
  const auto before=perimeterPoint(phase,w,h),after=perimeterPoint(phase+action,w,h);
  return {original.x+(after.x-before.x),original.y+(after.y-before.y)};
}
BatchResult evaluateBatch(const ComponentEnvironment& source,const std::vector<Point>& delta,ComponentEnvironment& candidate,
                          const std::vector<Point>& perimeter={}) {
  if(delta.size()!=source.components.size())throw std::runtime_error("batch size mismatch");
  if(source.neuralPerimeterPorts&&perimeter.size()!=source.b.edges.size())throw std::runtime_error("perimeter batch size mismatch");
  candidate=source;BatchResult result;
  for(int n=0;n<int(delta.size());n++) {
    if(!std::isfinite(delta[n].x)||!std::isfinite(delta[n].y))throw std::runtime_error("nonfinite neural translation");
    const Point d{rounded(delta[n].x),rounded(delta[n].y)};
    auto& p=candidate.a.pos[n];p.x+=d.x;p.y+=d.y;
    const auto size=candidate.a.nodes[n];
    if(p.x-size.w/2<source.left-1e-7||p.x+size.w/2>source.right+1e-7
       ||p.y-size.h/2<source.top-1e-7||p.y+size.h/2>source.bottom+1e-7)return result;
    for(int member:candidate.components[n]){candidate.b.pos[member].x+=d.x;candidate.b.pos[member].y+=d.y;}
  }
  // Fixed decoder, with no repair or alternative endpoint search.
  if(source.attachAllPorts||source.neuralPerimeterPorts) {
    for(int e=0;e<int(source.b.edges.size());e++) {
      const auto endpoints=source.b.edges[e];auto original=source.b.routes[e];
      if(source.neuralPerimeterPorts) {
        const auto s=source.b.nodes[endpoints.s],t=source.b.nodes[endpoints.t];
        original.a=perimeterShift(original.a,source.b.pos[endpoints.s],s.w,s.h,perimeter[e].x);
        original.b=perimeterShift(original.b,source.b.pos[endpoints.t],t.w,t.h,perimeter[e].y);
      }
      const auto ds=delta[source.owner[endpoints.s]],dt=delta[source.owner[endpoints.t]];
      candidate.b.routes[e]=segment({rounded(original.a.x+rounded(ds.x)),rounded(original.a.y+rounded(ds.y))},
                                    {rounded(original.b.x+rounded(dt.x)),rounded(original.b.y+rounded(dt.y))});
    }
  }else candidate.b.allRoutes();
  for(int g=0;g<int(candidate.groups.size());g++)if(!candidate.project(g)){result.reason="projection";return result;}
  const auto a=candidate.a.full(),b=candidate.b.full();
  result.visual=candidate.visual=a.visual();result.individual=candidate.individualVisual=b.visual();
  result.spacing=a.spacing+b.spacing;
  if(a.spacing||b.spacing||a.overlap||b.overlap){result.reason="spacing";return result;}
  result.hard=policyHardScore(candidate.a);result.individualHard=policyHardScore(candidate.b);
  if(result.hard>source.initialHard||result.individualHard>source.initialIndividualHard){result.reason="hard";return result;}
  result.legal=true;result.reason="legal";return result;
}
void batchSelfTest() {
  auto contract=synthetic(98999).b;
  const auto c=contract.pos[0];const auto n=contract.nodes[0];
  const Point corner{c.x+n.w/2,c.y+n.h/2},peer{c.x-n.w/2-500,c.y+n.h/2+400};
  if(outwardBoundary(contract,0,{corner.x,corner.y-.03},peer)
     ||!outwardBoundary(contract,0,corner,peer))throw std::runtime_error("outward corner endpoint contract");
  std::mt19937_64 rng(7819);long checked=0,legal=0;
  for(int mode=0;mode<3;mode++)for(int g=0;g<12;g++) {
    auto source=synthetic(99000+g);source.attachAllPorts=mode==1;source.neuralPerimeterPorts=mode==2;auto candidate=source;
    for(int k=0;k<16;k++) {
      std::vector<Point> delta(source.components.size());
      for(int n=0;n<int(delta.size());n++) {
        const auto p=source.a.pos[n];const auto size=source.a.nodes[n];
        const double radius=k%2?40.:.1;
        delta[n]={rounded(std::clamp((double(int(rng()%2001)-1000)/1000)*radius,source.left+size.w/2-p.x,source.right-size.w/2-p.x)),
                  rounded(std::clamp((double(int(rng()%2001)-1000)/1000)*radius,source.top+size.h/2-p.y,source.bottom-size.h/2-p.y))};
      }
      std::vector<Point> perimeter(source.b.edges.size());
      if(mode==2&&k)for(auto& p:perimeter)p={double(int(rng()%2001)-1000)/10000,double(int(rng()%2001)-1000)/10000};
      const auto result=evaluateBatch(source,delta,candidate,perimeter);checked++;
      if(mode==1||(mode==2&&!k))for(int e=0;e<int(source.b.edges.size());e++) {
        const auto endpoints=source.b.edges[e];const auto before=source.b.routes[e],after=candidate.b.routes[e];
        const auto ds=delta[source.owner[endpoints.s]],dt=delta[source.owner[endpoints.t]];
        if(distance(after.a,{rounded(before.a.x+rounded(ds.x)),rounded(before.a.y+rounded(ds.y))})>1e-8
           ||distance(after.b,{rounded(before.b.x+rounded(dt.x)),rounded(before.b.y+rounded(dt.y))})>1e-8)
          throw std::runtime_error("attached batch changed a source port offset");
      }
      if(result.legal) {
        legal++;
        if(candidate.a.full().visual()!=result.visual||candidate.b.full().visual()!=result.individual
           ||candidate.a.full().spacing||candidate.b.full().spacing)throw std::runtime_error("batch metric mismatch");
        for(int n=0;n<int(delta.size());n++)for(int member:source.components[n])
          if(distance(candidate.b.pos[member],{source.b.pos[member].x+rounded(delta[n].x),source.b.pos[member].y+rounded(delta[n].y)})>1e-8)
            throw std::runtime_error("batch is not a rigid neural translation");
      }
    }
    if(source.a.full().visual()!=source.visual||source.b.full().visual()!=source.individualVisual)
      throw std::runtime_error("batch evaluation changed its source");
  }
  if(!legal)throw std::runtime_error("batch fixtures had no legal proposals");
  std::cout<<"{\"jointBatchSelfTest\":\"pass\",\"fullGeometryComparisons\":"<<checked<<",\"legalBatches\":"<<legal<<"}\n";
}
}
int main(int argc,char** argv) {
  try {
    if(argc==2&&std::string(argv[1])=="--self-test"){batchSelfTest();return 0;}
    auto source=load(arg(argc,argv,"--directory"));source.overviewOnly=arg(argc,argv,"--overview-only","0")=="1";
    source.attachAllPorts=arg(argc,argv,"--attached-ports","0")=="1";
    source.neuralPerimeterPorts=arg(argc,argv,"--neural-perimeter-ports","0")=="1";
    source.temporaryRegressionBudget=std::stol(arg(argc,argv,"--regression-limit","0"));
    if(source.temporaryRegressionBudget<0||source.temporaryRegressionBudget>200)throw std::runtime_error("invalid temporary batch regression limit");
    auto best=source;long attempts=0,accepted=0;
    std::cout<<std::setprecision(12)<<"{\"ready\":true,\"visual\":"<<source.visual<<",\"individualVisual\":"<<source.individualVisual<<",\"nodes\":[";
    for(int n=0;n<int(source.components.size());n++) {
      double scale;const auto f=source.features(n,scale);if(n)std::cout<<',';
      std::cout<<"{\"id\":"<<n<<",\"features\":";array(std::cout,f);std::cout<<'}';
    }
    std::cout<<"]}"<<std::endl;
    std::string line;
    while(std::getline(std::cin,line)) {
      std::istringstream command(line);std::string op;command>>op;
      if(op=="TRY") {
        std::vector<Point> delta(source.components.size());
        for(auto& d:delta)if(!(command>>d.x>>d.y))throw std::runtime_error("incomplete neural batch");
        std::vector<Point> perimeter;
        if(source.neuralPerimeterPorts) {
          perimeter.resize(source.b.edges.size());
          for(auto& p:perimeter)if(!(command>>p.x>>p.y))throw std::runtime_error("incomplete neural perimeter batch");
        }
        std::string extra;if(command>>extra)throw std::runtime_error("extra neural batch coordinates");
        auto candidate=source;const auto r=evaluateBatch(source,delta,candidate,perimeter);attempts++;
        const long ceiling=accepted?best.visual:source.visual+source.temporaryRegressionBudget;
        const long individualCeiling=accepted?best.individualVisual:source.individualVisual+source.temporaryRegressionBudget;
        const bool improve=r.legal&&r.visual<ceiling&&(source.overviewOnly||r.individual<=individualCeiling);
        if(improve){best=std::move(candidate);accepted++;}
        std::cout<<"{\"accepted\":"<<(improve?"true":"false")<<",\"legal\":"<<(r.legal?"true":"false")<<",\"visual\":"<<r.visual
          <<",\"individualVisual\":"<<r.individual<<",\"spacing\":"<<r.spacing<<",\"hard\":"<<r.hard<<",\"individualHard\":"<<r.individualHard
          <<",\"reason\":\""<<r.reason<<"\"}"<<std::endl;
      }else if(op=="SAVE") {
        best.attempts=attempts;best.accepted=accepted;best.save(arg(argc,argv,"--out"));std::cout<<"{\"saved\":true}"<<std::endl;
      }else if(op=="QUIT")return 0;
      else throw std::runtime_error("unknown neural batch command");
    }
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
