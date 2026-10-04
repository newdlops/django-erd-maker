// Whole-scene geometric proposals. Actual card sizes remain fixed; every
// proposal is fitted and repaired before all original relations are scored.
namespace {
Score searchGlobalProjections(State& s,int trials,uint64_t seed,
                             const std::function<bool()>& expired,
                             const std::function<double()>& elapsed,
                             const std::string& output) {
  std::mt19937_64 rng(seed);
  std::uniform_real_distribution<double> uniform(0,1);
  auto current=s.full();
  const auto origin=s.pos;
  auto best=s.pos;
  long lowestProposal=std::numeric_limits<long>::max();
  int attempted=0,feasible=0;
  for(int trial=0;trial<trials&&!expired();trial++) {
    attempted++;
    s.pos=origin;
    Point center{s.width*uniform(rng),s.height*uniform(rng)};
    if(trial%4==0) {
      int n=int(rng()%s.nodes.size());
      for(int i=0;i<30;i++) {int other=int(rng()%s.nodes.size());if(s.adj[other].size()>s.adj[n].size())n=other;}
      center=s.pos[n];
      double theta=uniform(rng)*6.283185307179586,radius=200+uniform(rng)*2000;
      center.x+=radius*std::cos(theta);center.y+=radius*std::sin(theta);
    }
    double theta=uniform(rng)*6.283185307179586;
    Point axis{std::cos(theta),std::sin(theta)};
    double tilt=.5+uniform(rng)*3,power=.3+uniform(rng)*2.7;
    bool valid=true;
    for(auto& p:s.pos) {
      double x=(p.x-center.x)/s.width,y=(p.y-center.y)/s.height;
      double scale;
      if(trial%2) {
        double denominator=1+tilt*(x*axis.x+y*axis.y);
        if(std::abs(denominator)<.015){valid=false;break;}
        scale=1/denominator;
      } else scale=1/std::pow(std::max(1e-4,std::hypot(x,y)),power);
      p={x*scale*s.width,y*scale*s.height};
    }
    if(!valid)continue;
    try {initialize(s,1,expired);}catch(const std::runtime_error&){continue;}
    feasible++;
    const auto candidate=s.full();lowestProposal=std::min(lowestProposal,candidate.visual());
    if(better(candidate,current)){current=candidate;best=s.pos;s.save(output);report("projection",trial,current,elapsed());}
  }
  s.pos=best;s.allRoutes();
  std::cerr<<"projection attempted="<<attempted<<" feasible="<<feasible<<" lowestProposal="<<lowestProposal<<'\n';
  return current;
}
}
