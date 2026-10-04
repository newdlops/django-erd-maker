// Read-only hard-condition attribution for externally supplied model batches.
// Reuse the exact decoder and gates; never propose, repair, commit or save.
#define main unchanged_batch_protocol_main
#include "ml_joint_batch_environment.cpp"
#undef main

namespace {
struct HardComponents {
  long ownInterior=0,adjacentCross=0,endpointContact=0,outward=0;
  long total()const{return ownInterior+adjacentCross+endpointContact+outward;}
};
HardComponents hardComponents(const State& s) {
  HardComponents result;
  for(int e=0;e<int(s.edges.size());e++) {
    const auto edge=s.edges[e];const auto line=s.routes[e];
    result.ownInterior+=hits(line,s.pos[edge.s],s.nodes[edge.s],-.02);
    result.ownInterior+=hits(line,s.pos[edge.t],s.nodes[edge.t],-.02);
    result.outward+=!outwardBoundary(s,edge.s,line.a,line.b);
    result.outward+=!outwardBoundary(s,edge.t,line.b,line.a);
    for(int f=e+1;f<int(s.edges.size());f++) {
      const auto other=s.edges[f];
      const bool adjacent=edge.s==other.s||edge.s==other.t||edge.t==other.s||edge.t==other.t;
      result.adjacentCross+=2*long(adjacent&&crosses(line,s.routes[f]));
      result.endpointContact+=2*long(boundaryContact(s,e,line,f,s.routes[f]));
    }
  }
  return result;
}
void writeHardComponents(const HardComponents& c) {
  std::cout<<"{\"ownInterior\":"<<c.ownInterior<<",\"adjacentCross\":"<<c.adjacentCross
    <<",\"endpointContact\":"<<c.endpointContact<<",\"outward\":"<<c.outward<<",\"total\":"<<c.total()<<'}';
}
bool sameGeometry(const State& a,const State& b) {
  if(a.nodes.size()!=b.nodes.size()||a.edges.size()!=b.edges.size())return false;
  for(int n=0;n<int(a.nodes.size());n++) {
    if(a.nodes[n].id!=b.nodes[n].id||a.nodes[n].w!=b.nodes[n].w||a.nodes[n].h!=b.nodes[n].h
       ||a.pos[n].x!=b.pos[n].x||a.pos[n].y!=b.pos[n].y)return false;
  }
  for(int e=0;e<int(a.edges.size());e++) {
    const auto x=a.routes[e],y=b.routes[e];
    if(a.edges[e].id!=b.edges[e].id||a.edges[e].s!=b.edges[e].s||a.edges[e].t!=b.edges[e].t
       ||x.a.x!=y.a.x||x.a.y!=y.a.y||x.b.x!=y.b.x||x.b.y!=y.b.y)return false;
  }
  return true;
}
void hardAttributionSelfTest() {
  long checked=0;std::mt19937_64 rng(84511);
  for(int k=0;k<12;k++) {
    auto source=synthetic(84600+k);
    for(State* state:{&source.a,&source.b}) {
      for(int trial=0;trial<4;trial++) {
        if(trial)for(auto& line:state->routes) {
          line=segment({line.a.x+double(int(rng()%2001)-1000)/100.,line.a.y+double(int(rng()%2001)-1000)/100.},
                       {line.b.x+double(int(rng()%2001)-1000)/100.,line.b.y+double(int(rng()%2001)-1000)/100.});
        }
        const auto c=hardComponents(*state);
        if(c.total()!=policyHardScore(*state))throw std::runtime_error("hard component sum mismatch");
        checked++;
      }
    }
  }
  std::cout<<"{\"hardComponentsSelfTest\":\"pass\",\"fullHardScoreComparisons\":"<<checked<<"}\n";
}
}
int main(int argc,char** argv) {
  try {
    if(argc==2&&std::string(argv[1])=="--self-test"){hardAttributionSelfTest();return 0;}
    if(argc==2&&std::string(argv[1])=="--batch-self-test"){batchSelfTest();return 0;}
    auto source=load(arg(argc,argv,"--directory"));
    source.overviewOnly=arg(argc,argv,"--overview-only","0")=="1";
    source.neuralPerimeterPorts=true;
    const auto original=source;
    ExactSparseBatchScore sparse(source);
    const auto baselineA=hardComponents(source.a),baselineB=hardComponents(source.b);
    if(baselineA.total()!=source.initialHard||baselineB.total()!=source.initialIndividualHard)
      throw std::runtime_error("source hard components mismatch");
    std::cout<<std::setprecision(12)<<"{\"ready\":true,\"readOnly\":true,\"visual\":"<<source.visual
      <<",\"individualVisual\":"<<source.individualVisual<<",\"physical\":";
    writeHardComponents(baselineA);std::cout<<",\"individual\":";writeHardComponents(baselineB);std::cout<<'}'<<std::endl;
    std::string line;
    while(std::getline(std::cin,line)) {
      std::istringstream command(line);std::string op;command>>op;
      if(op=="QUIT")return 0;
      if(op!="DIAG")throw std::runtime_error("read-only diagnostic command required");
      std::vector<Point> delta(source.components.size()),perimeter(source.b.edges.size());
      for(auto& d:delta)if(!(command>>d.x>>d.y))throw std::runtime_error("incomplete diagnostic translation batch");
      for(auto& p:perimeter)if(!(command>>p.x>>p.y))throw std::runtime_error("incomplete diagnostic perimeter batch");
      std::string extra;if(command>>extra)throw std::runtime_error("extra diagnostic coordinates");
      auto candidate=source;const auto r=evaluateBatch(source,delta,candidate,perimeter,&sparse);
      const bool complete=std::string(r.reason)!="frame"&&std::string(r.reason)!="projection";
      std::cout<<"{\"legal\":"<<(r.legal?"true":"false")<<",\"visual\":"<<r.visual
        <<",\"individualVisual\":"<<r.individual<<",\"spacing\":"<<r.spacing<<",\"hard\":"<<r.hard
        <<",\"individualHard\":"<<r.individualHard<<",\"reason\":\""<<r.reason<<"\",\"componentsAvailable\":"<<(complete?"true":"false");
      if(complete) {
        const auto a=hardComponents(candidate.a),b=hardComponents(candidate.b);
        if(std::string(r.reason)!="spacing"&&(a.total()!=r.hard||b.total()!=r.individualHard))
          throw std::runtime_error("evaluated hard components mismatch");
        std::cout<<",\"physical\":";writeHardComponents(a);std::cout<<",\"individual\":";writeHardComponents(b);
      }
      if(!sameGeometry(source.a,original.a)||!sameGeometry(source.b,original.b)
         ||source.attempts||source.accepted||source.visual!=original.visual||source.individualVisual!=original.individualVisual)
        throw std::runtime_error("read-only diagnostic changed source or counters");
      std::cout<<",\"sourceGeometryAndCountersUnchanged\":true}"<<std::endl;
    }
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
