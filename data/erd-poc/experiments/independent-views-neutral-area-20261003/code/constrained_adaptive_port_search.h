// Joint search over actual card positions and their straight boundary routes.
// Ports are part of the saved state, so changing a fan does not discard earlier
// routing gains. All deltas include edges and cards outside the moving group.
namespace {
class AdaptivePortSearch {
  State& s;
  std::mt19937_64 rng;
  std::uniform_real_distribution<double> unit{0,1};
  std::function<bool()> expired;
  int samples;
  std::vector<long> pressure;
  int pick(int size) {return int(rng()%uint64_t(size));}
  double uniform() {return unit(rng);}
  struct EdgeCost {long visual=0,adjacent=0;double repair() const{return visual+adjacent*10000.;}};

  bool outward(int e,const Segment& route) const {
    for(int n:{s.edges[e].s,s.edges[e].t})if(hits(route,s.pos[n],s.nodes[n],-.02))return false;
    return true;
  }
  EdgeCost cost(int e) const {
    EdgeCost result;const auto& edge=s.edges[e];
    for(int f=0;f<int(s.edges.size());f++)if(f!=e&&crosses(s.routes[e],s.routes[f])) {
      result.visual++;
      const auto& other=s.edges[f];
      result.adjacent+=edge.s==other.s||edge.s==other.t||edge.t==other.s||edge.t==other.t;
    }
    for(int n=0;n<int(s.nodes.size());n++)if(n!=edge.s&&n!=edge.t)result.visual+=hits(s.routes[e],s.pos[n],s.nodes[n]);
    return result;
  }
  Point boundary(int n) {
    int side=pick(4);double ratio=uniform();if(uniform()<.4)ratio=uniform()<.5?0:1;
    const auto p=s.pos[n];const auto& node=s.nodes[n];
    return side<2?Point{rounded(p.x+(side?1:-1)*node.w*.5),rounded(p.y+(ratio-.5)*node.h)}
      :Point{rounded(p.x+(ratio-.5)*node.w),rounded(p.y+(side==3?1:-1)*node.h*.5)};
  }
  Segment centerRoute(int e) const {
    const auto& edge=s.edges[e];
    return segment(port(s.pos[edge.s],s.nodes[edge.s],s.pos[edge.t]),port(s.pos[edge.t],s.nodes[edge.t],s.pos[edge.s]));
  }
  Segment proposalRoute(int e,int mode) {
    auto line=s.routes[e];const auto& edge=s.edges[e];
    if(mode==0)return centerRoute(e);
    if(mode==1)return segment(port(s.pos[edge.s],s.nodes[edge.s],line.b),line.b);
    if(mode==2)return segment(line.a,port(s.pos[edge.t],s.nodes[edge.t],line.a));
    if(mode%3!=1)line.a=boundary(edge.s);
    if(mode%3!=2)line.b=boundary(edge.t);
    return segment(line.a,line.b);
  }
  void polish(int e) {
    auto best=s.routes[e];auto bestCost=cost(e);
    for(int i=0;i<samples;i++) {
      auto candidate=proposalRoute(e,i<3?i:3+pick(3));
      if(!outward(e,candidate))continue;
      s.routes[e]=candidate;auto next=cost(e);
      if(next.repair()<bestCost.repair()){best=candidate;bestCost=next;}
      s.routes[e]=best;
    }
  }
  Point position(int n,int mode) {
    Point p=s.pos[n];double angle=uniform()*6.283185307179586;
    double radius=std::pow(10.,.7+uniform()*3.4);
    if(mode<=2) {
      int a=s.adj[n][pick(s.adj[n].size())],b=s.adj[n][pick(s.adj[n].size())];
      double mix=uniform();p={s.pos[a].x*mix+s.pos[b].x*(1-mix),s.pos[a].y*mix+s.pos[b].y*(1-mix)};
    } else if(mode==3)p={s.width*uniform(),s.height*uniform()};
    else if(mode==4||mode==5) {
      int e=s.incident[n][pick(s.incident[n].size())];
      for(int attempt=0;attempt<80;attempt++) {
        int f=pick(s.edges.size());
        if(mode==4?!crosses(s.routes[e],s.routes[f]):!hits(s.routes[f],p,s.nodes[n]))continue;
        if(s.edges[f].s==n||s.edges[f].t==n)continue;
        auto line=s.routes[f];double dx=line.b.x-line.a.x,dy=line.b.y-line.a.y,len=std::hypot(dx,dy);
        if(len<1e-6)continue;
        Point normal{-dy/len,dx/len};double distance=(p.x-line.a.x)*normal.x+(p.y-line.a.y)*normal.y;
        double clear=(std::abs(normal.x)*s.nodes[n].w+std::abs(normal.y)*s.nodes[n].h)*.5+15+uniform()*100;
        if(mode==4) {
          int other=s.edges[e].s==n?s.edges[e].t:s.edges[e].s;
          double side=(s.pos[other].x-line.a.x)*normal.x+(s.pos[other].y-line.a.y)*normal.y;
          clear*=side>=0?1:-1;
        } else clear*=uniform()<.5?1:-1;
        return {p.x+normal.x*(clear-distance),p.y+normal.y*(clear-distance)};
      }
    }
    return {p.x+radius*std::cos(angle),p.y+radius*std::sin(angle)};
  }

  bool move(const std::vector<int>& moving,const std::vector<Point>& target,double temperature,Score& current,int& valid) {
    std::vector<Point> old;for(int n:moving){old.push_back(s.pos[n]);s.moved[n]=true;}
    bool free=true;
    for(size_t i=0;i<moving.size();i++) {
      s.pos[moving[i]]=target[i];free=free&&s.inside(moving[i],target[i]);
    }
    if(free)for(int n:moving) {
      for(int other=0;other<int(s.nodes.size());other++)if(n!=other&&(!s.moved[other]||n<other)&&s.pair(n,other).spacing){free=false;break;}
      if(!free)break;
    }
    for(size_t i=0;i<moving.size();i++)s.pos[moving[i]]=old[i];
    if(!free){for(int n:moving)s.moved[n]=false;return false;}
    std::vector<int> affected;
    for(int n:moving)for(int e:s.incident[n])if(!s.changedEdge[e]){s.changedEdge[e]=true;affected.push_back(e);}
    std::vector<Segment> previous;for(int e:affected)previous.push_back(s.routes[e]);
    const auto before=s.local(moving,affected);
    // Translate inherited endpoints, then permit their boundary offsets to
    // change. This preserves a good fan for small moves without freezing it.
    for(size_t i=0;i<moving.size();i++) {
      int n=moving[i];const auto& node=s.nodes[n];
      for(int e:s.incident[n]) {
        auto line=s.routes[e];auto& p=s.edges[e].s==n?line.a:line.b;
        double dx=p.x-old[i].x,dy=p.y-old[i].y;
        if(std::abs(std::abs(dx)-node.w*.5)<std::abs(std::abs(dy)-node.h*.5))dx=std::copysign(node.w*.5,dx);
        else dy=std::copysign(node.h*.5,dy);
        dx=std::clamp(dx,-node.w*.5,node.w*.5);dy=std::clamp(dy,-node.h*.5,node.h*.5);
        p={rounded(target[i].x+dx),rounded(target[i].y+dy)};s.routes[e]=segment(line.a,line.b);
      }
      s.pos[n]=target[i];
    }
    for(int e:affected)if(!outward(e,s.routes[e]))s.routes[e]=centerRoute(e);
    std::shuffle(affected.begin(),affected.end(),rng);
    for(int e:affected)polish(e);
    bool feasible=validAttachedRoutes(s,affected);
    if(!feasible) {
      for(int e:affected)if(cost(e).adjacent)polish(e);
      feasible=validAttachedRoutes(s,affected);
    }
    auto next=current+s.local(moving,affected)-before;
    valid+=feasible;
    double delta=next.visual()-current.visual();
    bool accept=feasible&&(delta<=0||(temperature>0&&uniform()<std::exp(-delta/temperature)));
    if(accept)current=next;
    else {
      for(size_t i=0;i<moving.size();i++)s.pos[moving[i]]=old[i];
      // affected was shuffled; restore through the unchanged incident order.
      size_t index=0;
      for(int n:moving)for(int e:s.incident[n])if(s.changedEdge[e]){s.routes[e]=previous[index++];s.changedEdge[e]=false;}
    }
    for(int n:moving)s.moved[n]=false;
    for(int e:affected)s.changedEdge[e]=false;
    return accept;
  }
  bool portMove(int e,double temperature,Score& current) {
    auto old=s.routes[e],candidate=proposalRoute(e,3+pick(3));
    if(!outward(e,candidate))return false;
    auto before=cost(e);s.routes[e]=candidate;auto after=cost(e);
    double delta=after.visual-before.visual;
    bool accept=after.adjacent==0&&(delta<=0||(temperature>0&&uniform()<std::exp(-delta/temperature)));
    if(accept) {
      // Keep the separate components exact, including unchanged cards.
      Score a,b;
      for(int f=0;f<int(s.edges.size());f++)if(e!=f){a.cross+=crosses(old,s.routes[f]);b.cross+=crosses(candidate,s.routes[f]);}
      for(int n=0;n<int(s.nodes.size());n++)if(n!=s.edges[e].s&&n!=s.edges[e].t){a.hit+=hits(old,s.pos[n],s.nodes[n]);b.hit+=hits(candidate,s.pos[n],s.nodes[n]);}
      current=current+b-a;
    } else s.routes[e]=old;
    return accept;
  }
public:
  AdaptivePortSearch(State& state,uint64_t seed,int count,std::function<bool()> deadline)
    :s(state),rng(seed),expired(std::move(deadline)),samples(count){}
  Score run(long steps,int cycles,double temperature,const std::function<double()>& elapsed,const std::string& output) {
    auto current=s.full(&pressure),best=current;auto bestPos=s.pos;auto bestRoutes=s.routes;
    std::vector<int> selectable;for(int n=0;n<int(s.nodes.size());n++)if(!s.adj[n].empty())selectable.push_back(n);
    const long cycleLength=std::max(1L,steps/cycles);long finished=0,portAccepted=0,positionAccepted=0,attempts=0;int valid=0;
    for(long step=0;step<steps;step++) {
      if(step%64==0&&expired())break;
      double progress=double(step%cycleLength)/cycleLength;
      double t=temperature*std::pow(.015/temperature,progress);
      int n=selectable[pick(selectable.size())];
      if(uniform()<.65)for(int i=0;i<3;i++){int other=selectable[pick(selectable.size())];if(pressure[other]>pressure[n])n=other;}
      if(uniform()<.4)portAccepted+=portMove(s.incident[n][pick(s.incident[n].size())],t*.25,current);
      else {
        std::vector<int> moving{n};std::vector<Point> target{position(n,pick(12))};
        if(!s.groups.empty()&&uniform()<.2) {
          moving=s.groups[pick(s.groups.size())];target.clear();Point center{};
          for(int member:moving){center.x+=s.pos[member].x;center.y+=s.pos[member].y;}
          center.x/=moving.size();center.y/=moving.size();
          double angle=uniform()*6.283185307179586,radius=std::pow(10.,1+uniform()*3);
          Point delta{radius*std::cos(angle),radius*std::sin(angle)};
          int mode=pick(4);double turn=(uniform()-.5)*6.283185307179586;
          for(int member:moving) {
            Point p=s.pos[member];double x=p.x-center.x,y=p.y-center.y;
            if(mode==0)p={center.x+x*std::cos(turn)-y*std::sin(turn),center.y+x*std::sin(turn)+y*std::cos(turn)};
            else if(mode==1)p={center.x-x,p.y};
            else p={p.x+delta.x,p.y+delta.y};
            target.push_back(p);
          }
        }
        attempts++;positionAccepted+=move(moving,target,t,current,valid);
      }
      finished=step+1;
      if(better(current,best)) {
        best=current;bestPos=s.pos;bestRoutes=s.routes;
        if(step%100<3)report("adaptive-best",step,best,elapsed());
      }
      if(finished%2000==0) {
        if(!equal(current,s.full(&pressure)))throw std::runtime_error("adaptive port score drift");
      }
      if((step+1)%cycleLength==0){s.pos=bestPos;s.routes=bestRoutes;current=best;s.full(&pressure);s.save(output);report("adaptive-cycle",step,best,elapsed());}
    }
    s.pos=bestPos;s.routes=bestRoutes;
    if(!equal(best,s.full())||!validAttachedRoutes(s,[&]{std::vector<int> es(s.edges.size());std::iota(es.begin(),es.end(),0);return es;}()))
      throw std::runtime_error("invalid adaptive port best");
    s.save(output);report("adaptive",finished,best,elapsed());
    std::cerr<<"adaptive attempts="<<attempts<<" feasible="<<valid<<" positionAccepted="<<positionAccepted<<" portAccepted="<<portAccepted<<'\n';
    return best;
  }
};
Score runAdaptivePortSearch(State& s,int argc,char** argv,const std::function<bool()>& expired,
                            const std::function<double()>& elapsed,const std::string& output) {
  const long steps=long(number(argc,argv,"--adaptive-steps",0));
  const int samples=int(number(argc,argv,"--adaptive-samples",8)),cycles=int(number(argc,argv,"--adaptive-cycles",4));
  const double temperature=number(argc,argv,"--adaptive-temperature",12);
  if(steps<0||samples<3||samples>64||cycles<1||temperature<=0||!std::isfinite(temperature)||!s.sourceOffsets.empty())
    throw std::runtime_error("invalid adaptive port search options");
  AdaptivePortSearch search(s,uint64_t(number(argc,argv,"--seed",42)),samples,expired);
  return search.run(steps,cycles,temperature,elapsed,output);
}
}
