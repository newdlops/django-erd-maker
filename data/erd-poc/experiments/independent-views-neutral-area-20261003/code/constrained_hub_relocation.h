// Relocate a hub before judging the move, then let its real neighbors adapt.
// Holding the hub temporarily avoids undoing a costly move before its followers
// can change order. Every accepted result is a complete, feasible scene.
namespace {
class HubRelocationSearch {
  State& s;
  std::mt19937_64 rng;
  std::uniform_real_distribution<double> unit{0,1};
  std::function<bool()> expired;
  std::vector<long> pressure;
  int pick(int size) {return int(rng()%uint64_t(size));}
  double uniform() {return unit(rng);}
  static double distance(Point a,Point b) {return std::hypot(a.x-b.x,a.y-b.y);}

  bool repair(const std::vector<int>& patch) {
    std::vector<char> unplaced(s.nodes.size(),false);
    for(int n:patch)unplaced[n]=true;
    for(int n:patch) {
      const auto wanted=s.pos[n];
      const auto free=[&](Point p) {
        if(!s.inside(n,p))return false;
        s.pos[n]=p;
        for(int other=0;other<int(s.nodes.size());other++)
          if(!unplaced[other]&&other!=n&&s.pair(n,other).spacing)return false;
        return true;
      };
      bool found=free(wanted);
      for(int ring=1;!found&&ring<=48;ring++) {
        if(expired())return false;
        for(int angle=0;!found&&angle<24;angle++) {
          double theta=angle*6.283185307179586/24;
          found=free({wanted.x+ring*75*std::cos(theta),wanted.y+ring*75*std::sin(theta)});
        }
      }
      if(!found)return false;
      unplaced[n]=false;
    }
    s.allRoutes();return true;
  }

  bool move(int n,Point p,double temperature,Score& current) {
    if(!s.inside(n,p))return false;
    const auto old=s.pos[n];s.pos[n]=p;
    for(int other=0;other<int(s.nodes.size());other++)if(other!=n&&s.pair(n,other).spacing) {
      s.pos[n]=old;return false;
    }
    s.pos[n]=old;s.moved[n]=true;
    const auto& es=s.incident[n];for(int e:es)s.changedEdge[e]=true;
    const auto before=s.local({n},es);
    s.pos[n]=p;s.update(es);
    const auto candidate=current+s.local({n},es)-before;
    const double delta=candidate.visual()-current.visual();
    bool accept=delta<=0||(temperature>0&&uniform()<std::exp(-delta/temperature));
    if(accept)current=candidate;
    else{s.pos[n]=old;s.update(es);}
    s.moved[n]=false;for(int e:es)s.changedEdge[e]=false;
    return accept;
  }

  // The root proposal deliberately excludes movable followers. Its complete
  // cost is evaluated after all followers have been restored and optimized.
  Point rootProposal(int root,const std::vector<char>& active,int samples,int round) {
    const auto old=s.pos[root];
    std::vector<Point> candidates;
    Point mean{};for(int n:s.adj[root]){mean.x+=s.pos[n].x;mean.y+=s.pos[n].y;}
    mean.x/=s.adj[root].size();mean.y/=s.adj[root].size();
    for(int i=0;i<samples;i++) {
      double theta=uniform()*6.283185307179586;
      double radius=std::pow(10.,2.6+uniform()*1.55);
      Point p=old;
      if(i%5==0)p=mean;
      if(i%5==1)p=s.pos[s.adj[root][pick(s.adj[root].size())]];
      if(i%5==2) {
        int a=s.adj[root][pick(s.adj[root].size())],b=s.adj[root][pick(s.adj[root].size())];
        double mix=uniform();p={s.pos[a].x*mix+s.pos[b].x*(1-mix),s.pos[a].y*mix+s.pos[b].y*(1-mix)};
      }
      if(i%5==3)p={s.width*uniform(),s.height*uniform()};
      p={p.x+radius*std::cos(theta),p.y+radius*std::sin(theta)};
      if(s.inside(root,p)&&distance(p,old)>350)candidates.push_back(p);
    }
    if(candidates.empty())return old;
    if(round%3==2)return candidates[pick(candidates.size())];
    double best=1e100;Point result=old;
    for(const auto p:candidates) {
      s.pos[root]=p;bool valid=true;double cost=0;
      for(int n=0;n<int(s.nodes.size());n++)if(!active[n]&&s.pair(root,n).spacing){valid=false;break;}
      if(!valid)continue;
      for(int e=0;e<int(s.edges.size());e++)if(!active[s.edges[e].s]&&!active[s.edges[e].t])
        cost+=hits(s.routes[e],p,s.nodes[root]);
      for(int e:s.incident[root]) {
        int other=s.edges[e].s==root?s.edges[e].t:s.edges[e].s;
        if(active[other])continue;
        const auto route=s.route(e);
        for(int f=0;f<int(s.edges.size());f++)if(!active[s.edges[f].s]&&!active[s.edges[f].t])cost+=crosses(route,s.routes[f]);
        for(int n=0;n<int(s.nodes.size());n++)if(!active[n]&&n!=other)cost+=hits(route,s.pos[n],s.nodes[n]);
      }
      if(cost<best){best=cost;result=p;}
      if(expired())break;
    }
    s.pos[root]=old;return result;
  }

  Point proposal(int n,int mode) {
    const auto old=s.pos[n];
    double theta=uniform()*6.283185307179586;
    double radius=std::pow(10.,1.1+uniform()*3.1);
    Point p=old;
    if(mode<=2&&!s.adj[n].empty()) {
      int a=s.adj[n][pick(s.adj[n].size())],b=s.adj[n][pick(s.adj[n].size())];
      double mix=uniform();p={s.pos[a].x*mix+s.pos[b].x*(1-mix),s.pos[a].y*mix+s.pos[b].y*(1-mix)};
    } else if(mode==3)p={s.width*uniform(),s.height*uniform()};
    else if(mode==4&&!s.incident[n].empty()) {
      int e=s.incident[n][pick(s.incident[n].size())];
      int neighbor=s.edges[e].s==n?s.edges[e].t:s.edges[e].s;
      for(int attempt=0;attempt<64;attempt++) {
        int f=pick(s.edges.size());if(!crosses(s.routes[e],s.routes[f]))continue;
        auto line=s.routes[f];double dx=line.b.x-line.a.x,dy=line.b.y-line.a.y,len=std::hypot(dx,dy);
        if(len<1e-6)continue;
        Point normal{-dy/len,dx/len};
        double side=(s.pos[neighbor].x-line.a.x)*normal.x+(s.pos[neighbor].y-line.a.y)*normal.y;
        double signedDistance=(old.x-line.a.x)*normal.x+(old.y-line.a.y)*normal.y;
        double clear=(side>=0?1:-1)*(20+uniform()*250);
        return {old.x+normal.x*(clear-signedDistance),old.y+normal.y*(clear-signedDistance)};
      }
    } else if(mode==5) {
      for(int attempt=0;attempt<64;attempt++) {
        int e=pick(s.edges.size());if(s.edges[e].s==n||s.edges[e].t==n||!hits(s.routes[e],old,s.nodes[n]))continue;
        auto line=s.routes[e];double dx=line.b.x-line.a.x,dy=line.b.y-line.a.y,len=std::hypot(dx,dy);
        if(len<1e-6)continue;
        Point normal{-dy/len,dx/len};double signedDistance=(old.x-line.a.x)*normal.x+(old.y-line.a.y)*normal.y;
        double clear=(uniform()<.5?1:-1)*((std::abs(normal.x)*s.nodes[n].w+std::abs(normal.y)*s.nodes[n].h)*.5+15+uniform()*60);
        return {old.x+normal.x*(clear-signedDistance),old.y+normal.y*(clear-signedDistance)};
      }
    }
    return {p.x+radius*std::cos(theta),p.y+radius*std::sin(theta)};
  }

public:
  HubRelocationSearch(State& state,uint64_t seed,std::function<bool()> deadline)
    :s(state),rng(seed),expired(std::move(deadline)){}
  Score run(int rounds,int steps,int samples,int maxDegree,double temperature,
            const std::function<double()>& elapsed,const std::string& output) {
    Score best=s.full(&pressure);auto bestPos=s.pos;int wins=0,finished=0;
    for(int round=0;round<rounds&&!expired();round++) {
      s.pos=bestPos;s.allRoutes();s.full(&pressure);
      std::vector<int> roots;
      for(int n=0;n<int(s.nodes.size());n++)if(s.adj[n].size()>=3&&pressure[n]>0)roots.push_back(n);
      if(roots.empty())break;
      std::stable_sort(roots.begin(),roots.end(),[&](int a,int b){return pressure[a]>pressure[b];});
      roots.resize(std::min(40,int(roots.size())));
      int root=roots[round%roots.size()];
      std::vector<int> patch{root};std::vector<char> active(s.nodes.size(),false);active[root]=true;
      for(int n:s.adj[root])if(int(s.adj[n].size())<=maxDegree&&!active[n]){active[n]=true;patch.push_back(n);}
      const int neighbors=patch.size();
      for(int i=1;i<neighbors;i++)for(int n:s.adj[patch[i]])if(s.adj[n].size()<=3&&!active[n]&&patch.size()<240){active[n]=true;patch.push_back(n);}
      if(patch.size()<3)continue;
      Point target=rootProposal(root,active,samples,round);
      if(distance(target,bestPos[root])<1)continue;
      if(round%2==0) {
        std::vector<double> weights(s.nodes.size(),0);weights[root]=1;
        for(int iter=0;iter<40;iter++)for(int n:patch)if(n!=root&&!s.adj[n].empty()) {
          double sum=0;for(int other:s.adj[n])sum+=weights[other];weights[n]=sum/s.adj[n].size();
        }
        Point delta{target.x-bestPos[root].x,target.y-bestPos[root].y};
        for(int n:patch)s.pos[n]={bestPos[n].x+delta.x*weights[n],bestPos[n].y+delta.y*weights[n]};
      }
      s.pos[root]=target;
      if(!repair(patch))continue;
      auto current=s.full();const auto before=current;auto trialBest=current;auto trialPos=s.pos;
      int completed=0;
      for(int step=0;step<steps;step++) {
        if(step%128==0&&expired())break;
        int index=1+pick(patch.size()-1);
        if(step>steps*3/4&&uniform()<.05)index=0;
        int n=patch[index];
        double t=temperature*std::pow(.015/temperature,double(step)/std::max(1,steps));
        move(n,proposal(n,pick(10)),t,current);completed++;
        if(better(current,trialBest)){trialBest=current;trialPos=s.pos;}
      }
      s.pos=trialPos;s.allRoutes();
      if(!equal(s.full(),trialBest))throw std::runtime_error("hub relocation score drift");
      finished+=completed==steps;
      bool improve=better(trialBest,best);
      std::cerr<<"hub round="<<round<<" root="<<s.nodes[root].id<<" patch="<<patch.size()<<" forced="<<before.visual()
        <<" relaxed="<<trialBest.visual()<<" steps="<<completed<<" accepted="<<improve<<'\n';
      if(improve){best=trialBest;bestPos=s.pos;s.save(output);wins++;report("hub-best",round,best,elapsed());}
    }
    s.pos=bestPos;s.allRoutes();
    std::cerr<<"hub completed="<<finished<<" wins="<<wins<<'\n';
    return best;
  }
};
Score runHubRelocation(State& s,int argc,char** argv,const std::function<bool()>& expired,
                       const std::function<double()>& elapsed,const std::string& output) {
  const int rounds=int(number(argc,argv,"--hub-rounds",0));
  const int steps=int(number(argc,argv,"--hub-steps",50000));
  const int samples=int(number(argc,argv,"--hub-candidates",48));
  const int degree=int(number(argc,argv,"--hub-follower-degree",12));
  const double temperature=number(argc,argv,"--hub-temperature",12);
  if(rounds<0||steps<0||samples<1||degree<1||degree>32||!std::isfinite(temperature)||temperature<=0)
    throw std::runtime_error("invalid hub relocation limits");
  HubRelocationSearch search(s,uint64_t(number(argc,argv,"--seed",42)),expired);
  return search.run(rounds,steps,samples,degree,temperature,elapsed,output);
}
}
