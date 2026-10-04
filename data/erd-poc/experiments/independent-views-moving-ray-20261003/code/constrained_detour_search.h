// Last-resort research routing: real cards stay fixed; each relationship gets
// at most two bends, a bounded length, and points inside the actual card bbox.
// New collinear overlaps, endpoint-card entries and adjacent-edge crossings
// are forbidden. Straight routes win ties over bent routes.
namespace {
struct BoundedPath {
  std::vector<Point> points;
  std::vector<Segment> segments;
  double length=0;
};
BoundedPath boundedPath(std::vector<Point> points) {
  BoundedPath result;
  for(auto p:points) {
    p={rounded(p.x),rounded(p.y)};
    if(!result.points.empty()&&std::hypot(p.x-result.points.back().x,p.y-result.points.back().y)<.01)continue;
    result.points.push_back(p);
  }
  for(size_t i=1;i<result.points.size();i++) {
    auto a=result.points[i-1],b=result.points[i];result.segments.push_back(segment(a,b));result.length+=std::hypot(a.x-b.x,a.y-b.y);
  }
  return result;
}
bool collinearOverlap(const Segment& a,const Segment& b) {
  if(a.l>b.r||b.l>a.r||a.t>b.bottom||b.t>a.bottom)return false;
  if(std::abs(orient(a.a,a.b,b.a))>1e-5||std::abs(orient(a.a,a.b,b.b))>1e-5)return false;
  return std::max(std::min(a.r,b.r)-std::max(a.l,b.l),std::min(a.bottom,b.bottom)-std::max(a.t,b.t))>.5;
}
bool routePointOnSegment(Point p,const Segment& line) {
  return p.x>=line.l-1e-6&&p.x<=line.r+1e-6&&p.y>=line.t-1e-6&&p.y<=line.bottom+1e-6&&std::abs(orient(line.a,line.b,p))<1e-5;
}
bool sameRoutePoint(Point a,Point b){return std::abs(a.x-b.x)<.001&&std::abs(a.y-b.y)<.001;}
class BoundedDetourSearch {
  State& s;
  std::vector<BoundedPath> paths;
  std::vector<double> baseLength;
  std::mt19937_64 rng;
  std::uniform_real_distribution<double> unit{0,1};
  std::function<bool()> expired;
  double left=1e100,right=-1e100,top=1e100,bottom=-1e100,stretch;
  int maxBends,maxBent;
  bool inside(Point p) const{return p.x>=left&&p.x<=right&&p.y>=top&&p.y<=bottom;}
  bool pathHits(const BoundedPath& path,int n,double padding=10) const {
    for(const auto& line:path.segments)if(hits(line,s.pos[n],s.nodes[n],padding))return true;return false;
  }
  long pathCross(const BoundedPath& a,const BoundedPath& b) const {
    long count=0;for(const auto& x:a.segments)for(const auto& y:b.segments)count+=crosses(x,y);return count;
  }
  bool adjacent(int e,int f) const {const auto& a=s.edges[e];const auto& b=s.edges[f];return a.s==b.s||a.s==b.t||a.t==b.s||a.t==b.t;}
  bool forbiddenContact(int e,const BoundedPath& path,int f,const BoundedPath& peer) const {
    const auto allowed=[&](Point p) {
      for(int a=0;a<2;a++)for(int b=0;b<2;b++)if((a?s.edges[e].t:s.edges[e].s)==(b?s.edges[f].t:s.edges[f].s)
        &&sameRoutePoint(p,a?path.points.back():path.points.front())&&sameRoutePoint(p,b?peer.points.back():peer.points.front()))return true;
      return false;
    };
    for(auto p:path.points)if(!allowed(p))for(const auto& line:peer.segments)if(routePointOnSegment(p,line))return true;
    for(auto p:peer.points)if(!allowed(p))for(const auto& line:path.segments)if(routePointOnSegment(p,line))return true;
    return false;
  }
  bool valid(int e,const BoundedPath& path) const {
    if(path.points.size()<2||path.points.size()>size_t(maxBends+2)||path.length>baseLength[e]*stretch+.01)return false;
    for(auto p:path.points)if(!inside(p))return false;
    for(int n:{s.edges[e].s,s.edges[e].t})if(pathHits(path,n,-.02))return false;
    for(size_t i=0;i<path.segments.size();i++)for(size_t j=i+1;j<path.segments.size();j++) {
      const auto& a=path.segments[i];const auto& b=path.segments[j];
      if(crosses(a,b)||collinearOverlap(a,b)||(j>i+1&&(routePointOnSegment(a.a,b)||routePointOnSegment(a.b,b)
        ||routePointOnSegment(b.a,a)||routePointOnSegment(b.b,a))))return false;
    }
    return true;
  }
  Score cost(int e,const BoundedPath& path,long cutoff=std::numeric_limits<long>::max(),bool check=false) const {
    Score result;
    for(int f=0;f<int(paths.size());f++)if(e!=f) {
      long count=pathCross(path,paths[f]);
      if(check) {
        if(count&&adjacent(e,f))return {cutoff+1,0,0,1};
        if(forbiddenContact(e,path,f,paths[f]))return {cutoff+1,0,0,1};
        for(const auto& a:path.segments)for(const auto& b:paths[f].segments)
          if(collinearOverlap(a,b))return {cutoff+1,0,0,1};
      }
      result.cross+=count;if(result.cross>cutoff)return result;
    }
    for(int n=0;n<int(s.nodes.size());n++)if(n!=s.edges[e].s&&n!=s.edges[e].t) {
      result.hit+=pathHits(path,n);if(result.visual()>cutoff)return result;
    }
    return result;
  }
  BoundedPath make(int e,const std::vector<Point>& bends,bool inherited) const {
    const auto& edge=s.edges[e];auto a=s.pos[edge.s],b=s.pos[edge.t];
    Point start=inherited?paths[e].points.front():port(a,s.nodes[edge.s],bends.empty()?b:bends.front());
    Point end=inherited?paths[e].points.back():port(b,s.nodes[edge.t],bends.empty()?a:bends.back());
    std::vector<Point> points{start};points.insert(points.end(),bends.begin(),bends.end());points.push_back(end);
    return boundedPath(points);
  }
  struct Leg {Point bend,endpoint;long cross;double length;};
  std::vector<Leg> clearLegs(int e,const std::vector<Point>& points,bool target,int beam,long cutoff) {
    std::vector<Leg> result;const auto& edge=s.edges[e];int n=target?edge.t:edge.s,other=target?edge.s:edge.t;
    for(size_t i=0;i<points.size();i++) {
      if(i%32==0&&expired())break;
      for(bool inherit:{true,false}) {
        auto endpoint=inherit?(target?paths[e].points.back():paths[e].points.front()):port(s.pos[n],s.nodes[n],points[i]);
        auto line=segment(endpoint,points[i]);double length=std::hypot(endpoint.x-points[i].x,endpoint.y-points[i].y);
        if(length<.01||length+std::hypot(points[i].x-s.pos[other].x,points[i].y-s.pos[other].y)
          >baseLength[e]*stretch+std::hypot(s.nodes[other].w,s.nodes[other].h)*.5)continue;
        bool valid=true;long count=0;
        for(int card=0;card<int(s.nodes.size());card++)if(hits(line,s.pos[card],s.nodes[card],card==n?-.02:10)){valid=false;break;}
        if(!valid)continue;
        const auto leg=boundedPath(target?std::vector<Point>{points[i],endpoint}:std::vector<Point>{endpoint,points[i]});
        for(int f=0;f<int(paths.size())&&valid;f++)if(f!=e) {
          if(forbiddenContact(e,leg,f,paths[f])){valid=false;break;}
          for(const auto& peer:paths[f].segments) {
          auto crossing=crosses(line,peer);count+=crossing;
          if((crossing&&adjacent(e,f))||collinearOverlap(line,peer)||count>cutoff){valid=false;break;}
          }
        }
        if(valid)result.push_back({points[i],endpoint,count,length});
      }
    }
    std::stable_sort(result.begin(),result.end(),[&](const auto& a,const auto& b) {
      double al=a.length+std::hypot(a.bend.x-s.pos[other].x,a.bend.y-s.pos[other].y);
      double bl=b.length+std::hypot(b.bend.x-s.pos[other].x,b.bend.y-s.pos[other].y);
      return a.cross!=b.cross?a.cross<b.cross:al<bl;
    });
    // Keep a deterministic random reserve, so zero-cost points near one card
    // do not exclude every farther leg that could complete a useful detour.
    if(int(result.size())>beam) {
      int keep=beam*2/3;std::shuffle(result.begin()+keep,result.end(),rng);result.resize(beam);
    }
    return result;
  }
  void joinClearLegs(int e,const std::vector<Point>& points,int beam,BoundedPath& bestPath,Score& bestCost) {
    if(!beam||maxBends<2)return;
    const auto first=clearLegs(e,points,false,beam,bestCost.visual());
    const auto last=clearLegs(e,points,true,beam,bestCost.visual());
    for(const auto& a:first) {
      if(expired())break;
      for(const auto& b:last) {
        if(a.cross+b.cross>bestCost.visual())continue;
        auto candidate=boundedPath({a.endpoint,a.bend,b.bend,b.endpoint});
        if(!valid(e,candidate))continue;
        auto value=cost(e,candidate,bestCost.visual(),true);
        if(value.spacing||value.hit)continue;
        if(value.visual()<bestCost.visual()||(value.visual()==bestCost.visual()&&(candidate.points.size()<bestPath.points.size()
          ||(candidate.points.size()==bestPath.points.size()&&candidate.length+1<bestPath.length)))) {
          bestPath=std::move(candidate);bestCost=value;
        }
      }
    }
  }
  std::vector<Point> proposals(int e,int samples) {
    std::vector<Point> result;const auto& edge=s.edges[e];Point a=s.pos[edge.s],b=s.pos[edge.t];
    const auto add=[&](Point p){p={rounded(p.x),rounded(p.y)};if(inside(p))result.push_back(p);};
    std::vector<int> obstacles;
    for(int n=0;n<int(s.nodes.size());n++)if(n!=edge.s&&n!=edge.t&&pathHits(paths[e],n))obstacles.push_back(n);
    for(int f=0;f<int(paths.size());f++)if(f!=e&&pathCross(paths[e],paths[f])){obstacles.push_back(s.edges[f].s);obstacles.push_back(s.edges[f].t);}
    std::sort(obstacles.begin(),obstacles.end());obstacles.erase(std::unique(obstacles.begin(),obstacles.end()),obstacles.end());
    std::shuffle(obstacles.begin(),obstacles.end(),rng);
    for(int n:obstacles) {
      auto p=s.pos[n];const auto& node=s.nodes[n];
      for(int dx:{-1,1})for(int dy:{-1,1})add({p.x+dx*(node.w*.5+24),p.y+dy*(node.h*.5+24)});
      if(int(result.size())>=samples/2)break;
    }
    double dx=b.x-a.x,dy=b.y-a.y,length=std::max(1.,std::hypot(dx,dy));Point normal{-dy/length,dx/length};
    for(double distance:{64.,128.,256.,512.,1024.,2048.,4096.})for(int sign:{-1,1})for(double mix:{.25,.5,.75})
      add({a.x+dx*mix+normal.x*distance*sign,a.y+dy*mix+normal.y*distance*sign});
    for(int attempt=0;int(result.size())<samples&&attempt<samples*3;attempt++) {
      double mix=unit(rng),offset=(unit(rng)-.5)*std::min(12000.,length*2);
      add({a.x+dx*mix+normal.x*offset,a.y+dy*mix+normal.y*offset});
    }
    if(int(result.size())>samples)result.resize(samples);return result;
  }
public:
  BoundedDetourSearch(State& state,uint64_t seed,double maxStretch,int bends,double fraction,std::function<bool()> deadline)
    :s(state),rng(seed),expired(std::move(deadline)),stretch(maxStretch),maxBends(bends),maxBent(std::max(1,int(s.edges.size()*fraction))) {
    for(int n=0;n<int(s.nodes.size());n++){left=std::min(left,s.pos[n].x-s.nodes[n].w*.5);right=std::max(right,s.pos[n].x+s.nodes[n].w*.5);top=std::min(top,s.pos[n].y-s.nodes[n].h*.5);bottom=std::max(bottom,s.pos[n].y+s.nodes[n].h*.5);}
    // Half-centipixel tolerance accounts for the existing rounded endpoints.
    left-=.005;right+=.005;top-=.005;bottom+=.005;
    for(const auto& line:s.routes){paths.push_back(boundedPath({line.a,line.b}));baseLength.push_back(std::max(1.,paths.back().length));}
  }
  Score full() const {
    Score result;
    for(int a=0;a<int(s.nodes.size());a++)for(int b=a+1;b<int(s.nodes.size());b++)result=result+s.pair(a,b);
    for(int e=0;e<int(paths.size());e++) {
      for(int f=e+1;f<int(paths.size());f++)result.cross+=pathCross(paths[e],paths[f]);
      for(int n=0;n<int(s.nodes.size());n++)if(n!=s.edges[e].s&&n!=s.edges[e].t)result.hit+=pathHits(paths[e],n);
    }
    return result;
  }
  void load(const std::string& file) {
    if(file=="none")return;
    const auto previous=full();const auto original=paths;
    std::ifstream input(file);if(!input)throw std::runtime_error("cannot read detour routes");
    std::unordered_map<std::string,int> ids;for(int e=0;e<int(s.edges.size());e++)ids[s.edges[e].id]=e;
    std::vector<char> present(paths.size(),false);std::string row;
    while(std::getline(input,row)) {
      auto fields=split(row);if(fields.size()!=2||!ids.count(fields[0])||present[ids.at(fields[0])])throw std::runtime_error("invalid detour route row");
      int e=ids.at(fields[0]);present[e]=true;std::istringstream coordinates(fields[1]);std::string pair;std::vector<Point> points;
      while(coordinates>>pair) {
        auto comma=pair.find(',');if(comma==std::string::npos)throw std::runtime_error("invalid detour coordinate");
        size_t xEnd=0,yEnd=0;Point p{std::stod(pair.substr(0,comma),&xEnd),std::stod(pair.substr(comma+1),&yEnd)};
        if(xEnd!=comma||yEnd!=pair.size()-comma-1)throw std::runtime_error("invalid detour coordinate suffix");
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x-rounded(p.x))>1e-7||std::abs(p.y-rounded(p.y))>1e-7)throw std::runtime_error("detour coordinates must be finite with two decimals");
        points.push_back(p);
      }
      paths[e]=boundedPath(points);if(!valid(e,paths[e]))throw std::runtime_error("invalid bounded detour geometry");
      if(paths[e].points.size()>2)for(int n=0;n<int(s.nodes.size());n++)if(n!=s.edges[e].s&&n!=s.edges[e].t&&pathHits(paths[e],n))throw std::runtime_error("detours must clear unrelated cards");
      for(int end=0;end<2;end++) {
        int n=end?s.edges[e].t:s.edges[e].s;auto p=end?paths[e].points.back():paths[e].points.front();
        double dx=std::abs(p.x-s.pos[n].x),dy=std::abs(p.y-s.pos[n].y);
        if(dx>s.nodes[n].w*.5+.011||dy>s.nodes[n].h*.5+.011||(std::abs(dx-s.nodes[n].w*.5)>.011&&std::abs(dy-s.nodes[n].h*.5)>.011))throw std::runtime_error("detour endpoint is off its actual card");
      }
    }
    if(std::find(present.begin(),present.end(),false)!=present.end())throw std::runtime_error("missing detour routes");
    int bent=0;for(const auto& path:paths)bent+=path.points.size()>2;
    if(bent>maxBent)throw std::runtime_error("detour route fraction exceeds cap");
    const auto overlap=[](const BoundedPath& a,const BoundedPath& b) {
      for(const auto& x:a.segments)for(const auto& y:b.segments)if(collinearOverlap(x,y))return true;return false;
    };
    for(int e=0;e<int(paths.size());e++)for(int f=e+1;f<int(paths.size());f++) {
      if(adjacent(e,f)&&pathCross(paths[e],paths[f]))throw std::runtime_error("detour routes cross adjacent relations");
      if(forbiddenContact(e,paths[e],f,paths[f]))throw std::runtime_error("detour routes touch away from a shared card endpoint");
      if(overlap(paths[e],paths[f])&&!overlap(original[e],original[f]))throw std::runtime_error("detour routes introduce a shared line");
    }
    if(better(previous,full()))paths=original;
  }
  void save(const std::string& output) const {
    s.save(output);std::ofstream routesOut(output+".routes.tsv");routesOut<<std::fixed<<std::setprecision(2);
    for(int e=0;e<int(paths.size());e++){routesOut<<s.edges[e].id<<'\t';for(size_t i=0;i<paths[e].points.size();i++){if(i)routesOut<<' ';routesOut<<paths[e].points[i].x<<','<<paths[e].points[i].y;}routesOut<<'\n';}
  }
  Score run(int rounds,int samples,int beam,const std::function<double()>& elapsed,const std::string& output) {
    Score current=full();int bent=0,accepted=0;for(const auto& path:paths)bent+=path.points.size()>2;
    for(int round=0;round<rounds&&!expired();round++) {
      std::vector<std::pair<long,int>> order;
      for(int e=0;e<int(paths.size());e++)order.emplace_back(cost(e,paths[e]).visual(),e);
      std::stable_sort(order.begin(),order.end(),[](auto a,auto b){return a.first>b.first;});
      int improved=0;
      for(const auto& item:order) {
        const long pressure=item.first;const int e=item.second;
        if(expired()||!pressure)break;
        if(bent>=maxBent&&paths[e].points.size()==2)continue;
        auto bestPath=paths[e];auto previous=cost(e,paths[e]),bestCost=previous;
        Point probeBend;bool hasProbe=false;long probeCost=previous.visual();
        const auto consider=[&](const std::vector<Point>& bends) {
          if(int(bends.size())>maxBends)return;
          for(bool inherit:{true,false}) {
            auto candidate=make(e,bends,inherit);if(!valid(e,candidate))continue;
            auto value=cost(e,candidate,bestCost.visual(),true);
            if(value.spacing==0&&candidate.points.size()==3&&value.visual()<probeCost){hasProbe=true;probeBend=candidate.points[1];probeCost=value.visual();}
            // A newly bent line must actually clear unrelated cards. Partial
            // one-bend probes with hits may only propose a second bend.
            if(candidate.points.size()>2&&value.hit)continue;
            bool improve=value.spacing==0&&(value.visual()<bestCost.visual()||(value.visual()==bestCost.visual()
              &&(candidate.points.size()<bestPath.points.size()||(candidate.points.size()==bestPath.points.size()&&candidate.length+1<bestPath.length))));
            if(improve){bestPath=std::move(candidate);bestCost=value;}
          }
        };
        consider({});auto points=proposals(e,samples);
        for(size_t i=0;i<points.size();i++) {
          if(i%16==0&&expired())break;
          consider({points[i]});
          if(hasProbe){auto anchor=probeBend;consider({points[i],anchor});consider({anchor,points[i]});}
          if(bestPath.points.size()==3){consider({points[i],bestPath.points[1]});consider({bestPath.points[1],points[i]});}
          if(paths[e].points.size()==4){consider({points[i],paths[e].points[2]});consider({paths[e].points[1],points[i]});}
        }
        const auto& edge=s.edges[e];auto a=s.pos[edge.s],b=s.pos[edge.t];double dx=b.x-a.x,dy=b.y-a.y,len=std::max(1.,std::hypot(dx,dy));
        for(double offset:{64.,128.,256.,512.,1024.,2048.})for(int sign:{-1,1})if(!expired())
          consider({{a.x+dx*.25-dy/len*offset*sign,a.y+dy*.25+dx/len*offset*sign},
                    {a.x+dx*.75-dy/len*offset*sign,a.y+dy*.75+dx/len*offset*sign}});
        joinClearLegs(e,points,beam,bestPath,bestCost);
        if(bestCost.visual()<previous.visual()||(bestCost.visual()==previous.visual()&&(bestPath.points.size()<paths[e].points.size()
          ||(bestPath.points.size()==paths[e].points.size()&&bestPath.length+1<paths[e].length)))) {
          bent+=int(bestPath.points.size()>2)-int(paths[e].points.size()>2);paths[e]=std::move(bestPath);current=current+bestCost-previous;accepted++;improved++;
          if(accepted%20==0){if(!equal(current,full()))throw std::runtime_error("detour score drift");save(output);report("detour-best",accepted,current,elapsed());}
        }
      }
      if(!equal(current,full()))throw std::runtime_error("detour round score drift");
      save(output);report("detour-round",round,current,elapsed());if(!improved)break;
    }
    save(output);int bends=0;double maximumStretch=0;for(int e=0;e<int(paths.size());e++){bends+=paths[e].points.size()-2;maximumStretch=std::max(maximumStretch,paths[e].length/baseLength[e]);}
    std::cerr<<"detour bentEdges="<<bent<<" bends="<<bends<<" maxBends="<<maxBends<<" maximumStretch="<<maximumStretch<<" accepted="<<accepted<<'\n';return current;
  }
};
Score runBoundedDetours(State& s,int argc,char** argv,const std::function<bool()>& expired,
                        const std::function<double()>& elapsed,const std::string& output) {
  int rounds=int(number(argc,argv,"--detour-rounds",0)),samples=int(number(argc,argv,"--detour-samples",250)),bends=int(number(argc,argv,"--detour-bends",2));
  int beam=int(number(argc,argv,"--detour-beam",0));
  double stretch=number(argc,argv,"--detour-stretch",2),fraction=number(argc,argv,"--detour-fraction",.25);
  if(rounds<0||samples<1||samples>4000||beam<0||beam>128||bends<1||bends>2||!std::isfinite(stretch)||stretch<1||stretch>4||!std::isfinite(fraction)||fraction<=0||fraction>1)
    throw std::runtime_error("invalid bounded detour limits");
  BoundedDetourSearch search(s,uint64_t(number(argc,argv,"--seed",42)),stretch,bends,fraction,expired);
  search.load(arg(argc,argv,"--detour-initial-routes","none"));
  return search.run(rounds,samples,beam,elapsed,output);
}
}
