// Read-only replay and classification of already frozen neural proposals.
// No new coordinates, fallback actions, or candidate files are produced.
#define ERD_COMPONENT_HELPERS_ONLY
#include "ml_component_environment.cpp"

std::array<long,5> hardKinds(const State& s,const std::vector<int>& edges) {
  std::array<long,5> result{};
  for(int e:edges) {
    const auto v=s.edges[e];const auto line=s.routes[e];
    for(auto endpoint:{std::make_pair(v.s,std::make_pair(line.a,line.b)),std::make_pair(v.t,std::make_pair(line.b,line.a))})
      result[s.moved[endpoint.first]?0:1]+=!outwardBoundary(s,endpoint.first,endpoint.second.first,endpoint.second.second);
    result[2]+=hits(line,s.pos[v.s],s.nodes[v.s],-.02)+hits(line,s.pos[v.t],s.nodes[v.t],-.02);
    for(int f=0;f<int(s.edges.size());f++)if(f!=e&&(!s.changedEdge[f]||e<f)) {
      const auto w=s.edges[f];const bool adjacent=v.s==w.s||v.s==w.t||v.t==w.s||v.t==w.t;
      result[3]+=2*long(adjacent&&crosses(line,s.routes[f]));
      result[4]+=2*long(boundaryContact(s,e,line,f,s.routes[f]));
    }
  }
  return result;
}

void inspectAction(const ComponentEnvironment& env,int n,const Result& measured,long index,bool checkGlobal) {
  auto before=env;const auto c=env.context(n);const auto delta=measured.decodedDelta;
  ComponentEnvironment::mark(before.a,c.physical,c.groups,true);ComponentEnvironment::mark(before.b,c.nodes,c.edges,true);
  auto candidate=before;
  for(int m:c.physical){candidate.a.pos[m].x+=delta.x;candidate.a.pos[m].y+=delta.y;}
  for(int m:c.nodes){candidate.b.pos[m].x+=delta.x;candidate.b.pos[m].y+=delta.y;}
  for(int e:c.edges) {
    const auto v=candidate.b.edges[e];const auto old=before.b.routes[e];
    if(candidate.b.moved[v.s]&&candidate.b.moved[v.t])
      candidate.b.routes[e]=segment({rounded(old.a.x+delta.x),rounded(old.a.y+delta.y)},
                                    {rounded(old.b.x+delta.x),rounded(old.b.y+delta.y)});
    else if(candidate.b.moved[v.s])candidate.b.routes[e]=segment(port(candidate.b.pos[v.s],candidate.b.nodes[v.s],old.b),old.b);
    else if(candidate.b.moved[v.t])candidate.b.routes[e]=segment(old.a,port(candidate.b.pos[v.t],candidate.b.nodes[v.t],old.a));
  }
  for(int e:c.groups)if(!candidate.project(e))throw std::runtime_error("selected hard action changed projection type");
  const auto a=hardKinds(candidate.a,c.groups),b=hardKinds(candidate.b,c.edges);
  const auto ca=ComponentEnvironment::cost(before.a,c.physical,c.groups),cb=ComponentEnvironment::cost(before.b,c.nodes,c.edges);
  const auto da=ComponentEnvironment::cost(candidate.a,c.physical,c.groups),db=ComponentEnvironment::cost(candidate.b,c.nodes,c.edges);
  const auto sum=[](auto values){return std::accumulate(values.begin(),values.end(),0L);};
  if(ca.hard||cb.hard||sum(a)!=da.hard||sum(b)!=db.hard||ca.score.visual()-da.score.visual()!=measured.gain
     ||cb.score.visual()-db.score.visual()!=measured.individualGain)throw std::runtime_error("diagnostic differs from native action");
  if(checkGlobal&&(sum(a)!=policyHardScore(candidate.a)||sum(b)!=policyHardScore(candidate.b)))throw std::runtime_error("diagnostic differs from full hard geometry");
  const auto print=[](auto values){std::cout<<'[';for(size_t k=0;k<values.size();k++){if(k)std::cout<<',';std::cout<<values[k];}std::cout<<']';};
  std::cout<<"{\"actionIndex\":"<<index<<",\"node\":"<<n<<",\"cards\":"<<c.physical.size()<<",\"gain\":"<<measured.gain
           <<",\"individualGain\":"<<measured.individualGain<<",\"overviewHard\":";print(a);std::cout<<",\"individualHard\":";print(b);
  std::cout<<",\"fullHardCompared\":"<<(checkGlobal?"true":"false")<<"}\n";
}

int main(int argc,char**argv) {
  try {
    auto env=load(arg(argc,argv,"--directory"));env.movingRayPorts=true;env.boundedMoves=true;env.allowNeutral=true;
    env.overviewOnly=arg(argc,argv,"--overview-only","0")=="1";loadBranches(env,arg(argc,argv,"--directory"),"cut");
    if(env.initialHard||env.initialIndividualHard)throw std::runtime_error("diagnostic requires a valid source");
    long index=0,inspected=0;int n,accepted,inspect;Point latent;long gain,individualGain;std::string reason;
    while(std::cin>>n>>latent.x>>latent.y>>accepted>>gain>>individualGain>>reason>>inspect) {
      index++;const auto r=env.trial(n,latent,true);
      if(r.accepted!=bool(accepted)||r.gain!=gain||r.individualGain!=individualGain||r.reason!=reason)
        throw std::runtime_error("frozen action result mismatch at "+std::to_string(index));
      if(inspect){if(r.accepted||r.legal)throw std::runtime_error("diagnostic selection is not a rejected hard action");inspectAction(env,n,r,index,inspected<4);inspected++;}
    }
    if(env.visual!=number(argc,argv,"--expected-visual",-1)||env.a.full().visual()!=env.visual||env.b.full().visual()!=env.individualVisual)
      throw std::runtime_error("final replay metric mismatch");
    std::cout<<"{\"completeReplay\":true,\"actions\":"<<index<<",\"inspected\":"<<inspected<<",\"visual\":"<<env.visual<<"}\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
