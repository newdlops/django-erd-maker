#include <sstream>

namespace {
// Exchanging two boundary points on their shared real card can restore a fan
// order after coordinated node movement. Count every affected adjacent pair;
// a swap is accepted only if that count decreases and both edges exit outward.
[[maybe_unused]] int repairAdjacentBoundaryPorts(State& s,const std::function<bool()>& expired={}) {
  const auto count=[&]() {
    long result=0;
    for(const auto& incident:s.incident)for(size_t i=0;i<incident.size();i++)for(size_t j=i+1;j<incident.size();j++)
      result+=crosses(s.routes[incident[i]],s.routes[incident[j]]);
    return result;
  };
  long remaining=count();int repaired=0;
  for(int pass=0;remaining&&pass<32;pass++) {
    bool improved=false;
    for(int n=0;n<int(s.nodes.size())&&!improved;n++) {
      const auto& incident=s.incident[n];
      for(size_t i=0;i<incident.size()&&!improved;i++)for(size_t j=i+1;j<incident.size()&&!improved;j++) {
        int e=incident[i],f=incident[j];if(!crosses(s.routes[e],s.routes[f]))continue;
        const auto original=s.routes;
        for(int mode=0;mode<5&&!improved;mode++) {
          if(expired&&expired()){s.routes=original;return repaired;}
          s.routes=original;
          if(mode==0) {
            Point a=original[e].a,b=original[e].b,c=original[f].a,d=original[f].b;
            Point& p=s.edges[e].s==n?a:b;Point& q=s.edges[f].s==n?c:d;std::swap(p,q);
            s.routes[e]=segment(a,b);s.routes[f]=segment(c,d);
          } else {
            if(mode==1||mode==3)s.routes[e]=s.route(e);
            if(mode==2||mode==3)s.routes[f]=s.route(f);
            if(mode==4)for(int edge:incident)s.routes[edge]=s.route(edge);
          }
          bool valid=true;
          for(int edge:incident)for(int endpoint:{s.edges[edge].s,s.edges[edge].t})
            valid=valid&&!hits(s.routes[edge],s.pos[endpoint],s.nodes[endpoint],-.02);
          long next=valid?count():remaining;
          if(next<remaining){remaining=next;repaired++;improved=true;}
        }
        if(!improved)s.routes=original;
      }
    }
    if(!improved)break;
  }
  return repaired;
}

Score loadBoundaryPorts(State& s,const std::string& path,Score previous) {
  if(path=="none")return previous;
  std::ifstream in(path);if(!in)throw std::runtime_error("cannot read initial ports");
  std::unordered_map<std::string,int> ids;
  for(int e=0;e<int(s.edges.size());e++)ids[s.edges[e].id]=e;
  std::vector<char> present(s.edges.size(),false);
  auto routes=s.routes;std::string line;
  const auto point=[](const std::string& value) {
    auto comma=value.find(',');if(comma==std::string::npos)throw std::runtime_error("invalid port coordinate");
    size_t xEnd=0,yEnd=0;double x=std::stod(value.substr(0,comma),&xEnd),y=std::stod(value.substr(comma+1),&yEnd);
    if(xEnd!=comma||yEnd!=value.size()-comma-1||!std::isfinite(x)||!std::isfinite(y))throw std::runtime_error("invalid port coordinate");
    if(std::abs(x-rounded(x))>1e-7||std::abs(y-rounded(y))>1e-7)throw std::runtime_error("ports require two-decimal coordinates");
    return Point{x,y};
  };
  const auto onBoundary=[&](Point p,int n) {
    double dx=std::abs(p.x-s.pos[n].x),dy=std::abs(p.y-s.pos[n].y);
    return dx<=s.nodes[n].w*.5+.011&&dy<=s.nodes[n].h*.5+.011
      &&(std::abs(dx-s.nodes[n].w*.5)<=.011||std::abs(dy-s.nodes[n].h*.5)<=.011);
  };
  while(std::getline(in,line)) {
    auto fields=split(line);
    if(fields.size()!=2||!ids.count(fields[0])||present[ids[fields[0]]])throw std::runtime_error("invalid initial port edge");
    int e=ids.at(fields[0]);present[e]=true;
    std::istringstream coordinates(fields[1]);std::string first,second,extra;
    if(!(coordinates>>first>>second)||(coordinates>>extra))throw std::runtime_error("initial ports must be straight");
    auto a=point(first),b=point(second);routes[e]=segment(a,b);
    const auto& edge=s.edges[e];
    if(!onBoundary(a,edge.s)||!onBoundary(b,edge.t))throw std::runtime_error("initial port is outside its card boundary");
    if(hits(routes[e],s.pos[edge.s],s.nodes[edge.s],-.02)||hits(routes[e],s.pos[edge.t],s.nodes[edge.t],-.02))throw std::runtime_error("initial route enters its endpoint card");
  }
  if(std::find(present.begin(),present.end(),false)!=present.end())throw std::runtime_error("missing initial ports");
  for(const auto& incident:s.incident)for(size_t i=0;i<incident.size();i++)for(size_t j=i+1;j<incident.size();j++)
    if(crosses(routes[incident[i]],routes[incident[j]]))throw std::runtime_error("initial routes cross adjacent edges");
  auto old=s.routes;s.routes=std::move(routes);auto candidate=s.full();
  if(better(previous,candidate)){s.routes=std::move(old);return previous;}
  return candidate;
}

Score attachBoundaryPorts(State& s,const std::string& path,Score previous) {
  auto current=loadBoundaryPorts(s,path,previous);
  for(int e=0;e<int(s.edges.size());e++) {
    auto source=s.pos[s.edges[e].s],target=s.pos[s.edges[e].t];
    s.sourceOffsets.push_back({s.routes[e].a.x-source.x,s.routes[e].a.y-source.y});
    s.targetOffsets.push_back({s.routes[e].b.x-target.x,s.routes[e].b.y-target.y});
  }
  return current;
}

bool validAttachedRoutes(const State& s,const std::vector<int>& affected) {
  for(int e:affected)for(int n:{s.edges[e].s,s.edges[e].t}) {
    if(hits(s.routes[e],s.pos[n],s.nodes[n],-.02))return false;
    for(int other:s.incident[n])if(e!=other&&crosses(s.routes[e],s.routes[other]))return false;
  }
  return true;
}

Score refineBoundaryPorts(State& s,Score exact,long steps,std::mt19937_64& rng,
                          const std::function<bool()>& expired,const std::function<double()>& elapsed) {
  std::uniform_real_distribution<double> unit(0,1);
  const auto randomIndex=[&](int count){return int(rng()%uint64_t(count));};
  const auto edgeCost=[&](int e){
    Score result;for(int other=0;other<int(s.edges.size());other++)if(e!=other)result.cross+=crosses(s.routes[e],s.routes[other]);
    for(int n=0;n<int(s.nodes.size());n++)if(n!=s.edges[e].s&&n!=s.edges[e].t)result.hit+=hits(s.routes[e],s.pos[n],s.nodes[n]);return result;
  };
  const auto boundaryPoint=[&](int n){
    const auto& node=s.nodes[n];const auto& p=s.pos[n];int side=randomIndex(4);double ratio=unit(rng);if(unit(rng)<.3)ratio=unit(rng)<.5?0:1;
    if(side<2)return Point{rounded(p.x+(side?1:-1)*node.w*.5),rounded(p.y+(ratio-.5)*node.h)};
    return Point{rounded(p.x+(ratio-.5)*node.w),rounded(p.y+(side==3?1:-1)*node.h*.5)};
  };
  long completed=0;
  for(long step=0;step<steps;step++) {
    if(step%256==0&&expired())break;
    completed=step+1;
    int e=randomIndex(s.edges.size());const auto& edge=s.edges[e];Segment old=s.routes[e];Point a=old.a,b=old.b;
    int choice=randomIndex(3);if(choice!=1)a=boundaryPoint(edge.s);if(choice!=0)b=boundaryPoint(edge.t);
    Segment candidate=segment(a,b);
    if(hits(candidate,s.pos[edge.s],s.nodes[edge.s],-.02)||hits(candidate,s.pos[edge.t],s.nodes[edge.t],-.02))continue;
    bool adjacentCrossing=false;
    for(int n:{edge.s,edge.t})for(int incident:s.incident[n])if(incident!=e&&crosses(candidate,s.routes[incident]))adjacentCrossing=true;
    if(adjacentCrossing)continue;
    Score before=edgeCost(e);s.routes[e]=candidate;Score after=edgeCost(e);
    if(after.visual()<before.visual()||(after.visual()==before.visual()&&after.hit<before.hit))exact=exact+after-before;
    else s.routes[e]=old;
  }
  if(!equal(exact,s.full()))throw std::runtime_error("port score drift");
  if(steps)report("ports",completed,exact,elapsed());
  return exact;
}
}
