// Deterministic ablation of historical member-level transforms on real cards.
// Search metadata never substitutes for visible edges. All deltas include
// every canonical edge and every actual rectangle; the product rechecks output.
#include "constrained_scene.h"
#include "constrained_port_search.h"
#include <set>

namespace {
struct Candidate {
  bool valid=false;
  Score score;
  long center=0;
  std::vector<Point> positions;
  std::vector<Segment> routes;
  std::string description;
};
struct Counts {
  long proposed=0,positionRejected=0,routeVariants=0,routeRejected=0;
  long scored=0,contactRejected=0,centerBetterVisualWorse=0,visualBetterCenterWorse=0,accepted=0;
};
bool samePoint(Point a,Point b) {return std::abs(a.x-b.x)<.001&&std::abs(a.y-b.y)<.001;}
bool sameRoute(Segment a,Segment b) {return samePoint(a.a,b.a)&&samePoint(a.b,b.b);}
bool onSegment(Point p,const Segment& line) {
  return std::abs(orient(line.a,line.b,p))<1e-5&&p.x>=line.l-1e-6&&p.x<=line.r+1e-6
    &&p.y>=line.t-1e-6&&p.y<=line.bottom+1e-6;
}
bool badContact(const State& s,int e,int f) {
  const auto a=s.routes[e],b=s.routes[f];
  if(a.l>b.r+1e-6||b.l>a.r+1e-6||a.t>b.bottom+1e-6||b.t>a.bottom+1e-6)return false;
  const auto u=s.edges[e],v=s.edges[f];
  const auto allowed=[&](Point p) {
    return (u.s==v.s&&samePoint(a.a,b.a)&&samePoint(p,a.a))
      ||(u.s==v.t&&samePoint(a.a,b.b)&&samePoint(p,a.a))
      ||(u.t==v.s&&samePoint(a.b,b.a)&&samePoint(p,a.b))
      ||(u.t==v.t&&samePoint(a.b,b.b)&&samePoint(p,a.b));
  };
  return (!allowed(a.a)&&onSegment(a.a,b))||(!allowed(a.b)&&onSegment(a.b,b))
    ||(!allowed(b.a)&&onSegment(b.a,a))||(!allowed(b.b)&&onSegment(b.b,a));
}
struct Probe {
  State s;
  Score current,best;
  long center=0;
  bool useCenter=false,repairFans=true;
  std::vector<std::pair<int,int>> centerEdges;
  std::vector<std::string> groupNames;
  std::vector<int> groupOf;
  Counts counts;
  std::ofstream journal;
  std::string prefix;
  std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
  double maxSeconds=120;
  double elapsed()const{return std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();}
  bool expired()const{return elapsed()>maxSeconds;}
  long centerScore(bool local=false)const {
    long result=0;
    for(int i=0;i<int(centerEdges.size());i++) {
      const auto [a,b]=centerEdges[i];
      const bool first=s.moved[a]||s.moved[b];
      if(local&&!first)continue;
      for(int j=(local?0:i+1);j<int(centerEdges.size());j++) {
        const auto [c,d]=centerEdges[j];
        if(i==j||a==c||a==d||b==c||b==d)continue;
        if(local&&(s.moved[c]||s.moved[d])&&i>j)continue;
        result+=crosses(segment(s.pos[a],s.pos[b]),segment(s.pos[c],s.pos[d]));
      }
    }
    return result;
  }
  bool improvement(Score a,long ac,Score b,long bc)const {
    if(useCenter)return ac<bc||(ac==bc&&a.visual()<b.visual());
    return a.visual()<b.visual()||(a.visual()==b.visual()&&a.hit<b.hit);
  }
  bool contactsValid(const std::vector<int>& affected)const {
    for(int e:affected)for(int f=0;f<int(s.edges.size());f++)
      if(e!=f&&(!s.changedEdge[f]||e<f)&&badContact(s,e,f))return false;
    return true;
  }
  Candidate evaluate(const std::vector<int>& moving,const std::vector<Point>& proposed,const std::string& description) {
    counts.proposed++;
    Candidate bestCandidate;
    const auto oldPositions=s.pos;const auto oldRoutes=s.routes;
    std::fill(s.moved.begin(),s.moved.end(),false);
    for(int n:moving)s.moved[n]=true;
    const long centerBefore=centerScore(true);
    bool valid=true;
    for(size_t i=0;i<moving.size();i++){int n=moving[i];s.pos[n]=proposed[i];if(!s.inside(n,proposed[i]))valid=false;}
    if(valid)for(int n:moving)for(int m=0;m<int(s.nodes.size());m++)
      if(n!=m&&(!s.moved[m]||n<m)&&s.pair(n,m).spacing){valid=false;break;}
    if(!valid){counts.positionRejected++;s.pos=oldPositions;return bestCandidate;}
    const auto newPositions=s.pos;
    const long newCenter=center+centerScore(true)-centerBefore;
    std::vector<int> initialAffected;
    for(int e=0;e<int(s.edges.size());e++)if(s.moved[s.edges[e].s]||s.moved[s.edges[e].t])initialAffected.push_back(e);
    for(int variant=0;variant<2&&!expired();variant++) {
      counts.routeVariants++;s.pos=newPositions;s.routes=oldRoutes;
      for(int e:initialAffected) {
        const auto edge=s.edges[e];
        if(variant==1){s.routes[e]=s.route(e);continue;}
        auto a=oldRoutes[e].a,b=oldRoutes[e].b;
        a={rounded(a.x+s.pos[edge.s].x-oldPositions[edge.s].x),rounded(a.y+s.pos[edge.s].y-oldPositions[edge.s].y)};
        b={rounded(b.x+s.pos[edge.t].x-oldPositions[edge.t].x),rounded(b.y+s.pos[edge.t].y-oldPositions[edge.t].y)};
        s.routes[e]=segment(a,b);
        if(hits(s.routes[e],s.pos[edge.s],s.nodes[edge.s],-.02)||hits(s.routes[e],s.pos[edge.t],s.nodes[edge.t],-.02))
          s.routes[e]=s.route(e);
      }
      if(!validAttachedRoutes(s,initialAffected)&&repairFans)
        repairAdjacentBoundaryPorts(s,[&]{return expired();});
      std::vector<int> affected;
      std::fill(s.changedEdge.begin(),s.changedEdge.end(),false);
      for(int e=0;e<int(s.edges.size());e++)if(!sameRoute(s.routes[e],oldRoutes[e])||s.moved[s.edges[e].s]||s.moved[s.edges[e].t]) {
        affected.push_back(e);s.changedEdge[e]=true;
      }
      if(!validAttachedRoutes(s,affected)){counts.routeRejected++;continue;}
      const auto candidateRoutes=s.routes;
      const Score after=s.local(moving,affected);
      s.pos=oldPositions;s.routes=oldRoutes;
      const Score before=s.local(moving,affected);
      s.pos=newPositions;s.routes=candidateRoutes;
      const Score candidate=current+after-before;
      counts.scored++;
      counts.centerBetterVisualWorse+=newCenter<center&&candidate.visual()>current.visual();
      counts.visualBetterCenterWorse+=candidate.visual()<current.visual()&&newCenter>center;
      if(!improvement(candidate,newCenter,current,center))continue;
      if(bestCandidate.valid&&!improvement(candidate,newCenter,bestCandidate.score,bestCandidate.center))continue;
      if(!contactsValid(affected)){counts.contactRejected++;continue;}
      bestCandidate={true,candidate,newCenter,newPositions,candidateRoutes,description+(variant?" direct-ports":" retained-ports")};
    }
    s.pos=oldPositions;s.routes=oldRoutes;
    return bestCandidate;
  }
  void accept(const Candidate& c) {
    if(!c.valid)return;
    const auto oldPositions=s.pos;
    const auto old=current;const auto oldCenter=center;
    s.pos=c.positions;s.routes=c.routes;current=c.score;center=c.center;
    if(!equal(current,s.full())||center!=centerScore())throw std::runtime_error("full score disagrees with incremental delta");
    counts.accepted++;
    journal<<counts.accepted<<'\t'<<c.description<<'\t'<<oldCenter<<'\t'<<center<<'\t'
      <<old.visual()<<'\t'<<current.visual()<<'\t'<<current.cross<<'\t'<<current.hit;
    for(int n=0;n<int(s.nodes.size());n++)if(!samePoint(s.pos[n],oldPositions[n]))
      journal<<'\t'<<s.nodes[n].id<<':'<<std::setprecision(12)<<oldPositions[n].x<<','<<oldPositions[n].y<<'>'<<s.pos[n].x<<','<<s.pos[n].y;
    journal<<'\n';journal.flush();
    if(better(current,best)){best=current;s.save(prefix+".best.tsv");}
    report("accepted",counts.accepted,current,elapsed());
  }
  Point centroid(const std::vector<int>& ns)const {
    Point p;for(int n:ns){p.x+=s.pos[n].x;p.y+=s.pos[n].y;}p.x/=ns.size();p.y/=ns.size();return p;
  }
  void rotate() {
    for(int g=0;g<int(s.groups.size())&&!expired();g++) {
      const auto& ns=s.groups[g];if(ns.size()<3)continue;
      const auto origin=centroid(ns);Candidate winner;
      for(int mirror: {1,-1})for(int k=0;k<12&&!expired();k++) {
        if(mirror==1&&k==0)continue;
        const double angle=k*3.14159265358979323846/6,cs=std::cos(angle),sn=std::sin(angle);
        std::vector<Point> p;
        for(int n:ns){double x=(s.pos[n].x-origin.x)*mirror,y=s.pos[n].y-origin.y;p.push_back({origin.x+cs*x-sn*y,origin.y+sn*x+cs*y});}
        auto c=evaluate(ns,p,"rotate "+groupNames[g]+" angle="+std::to_string(k*30)+" mirror="+std::to_string(mirror));
        if(c.valid&&(!winner.valid||improvement(c.score,c.center,winner.score,winner.center)))winner=std::move(c);
      }
      accept(winner);
    }
  }
  void swapGroups() {
    // Both selectors use all size-compatible pairs in stable order. There is
    // no score-dependent shortlist that could bias one objective's candidates.
    for(int a=0;a<int(s.groups.size())&&!expired();a++)for(int b=a+1;b<int(s.groups.size())&&!expired();b++) {
      const auto& an=s.groups[a];const auto& bn=s.groups[b];if(an.size()<2||bn.size()<2)continue;
      const auto box=[&](const std::vector<int>& ns){
        double l=1e100,r=-1e100,t=1e100,b=-1e100;
        for(int n:ns){l=std::min(l,s.pos[n].x-s.nodes[n].w/2);r=std::max(r,s.pos[n].x+s.nodes[n].w/2);t=std::min(t,s.pos[n].y-s.nodes[n].h/2);b=std::max(b,s.pos[n].y+s.nodes[n].h/2);}
        return (r-l)*(b-t);
      };
      const double aa=box(an),ba=box(bn);if(std::max(aa,ba)/std::min(aa,ba)>5)continue;
      auto ac=centroid(an),bc=centroid(bn);std::vector<int> ns=an;ns.insert(ns.end(),bn.begin(),bn.end());std::vector<Point> p;
      for(int n:an)p.push_back({s.pos[n].x+bc.x-ac.x,s.pos[n].y+bc.y-ac.y});
      for(int n:bn)p.push_back({s.pos[n].x+ac.x-bc.x,s.pos[n].y+ac.y-bc.y});
      accept(evaluate(ns,p,"swap "+groupNames[a]+" "+groupNames[b]));
    }
  }
  void pull() {
    const double threshold=.04*std::hypot(s.width,s.height);
    for(int n=0;n<int(s.nodes.size())&&!expired();n++) {
      const auto& neighbors=s.adj[n];if(neighbors.empty()||neighbors.size()>4)continue;
      const auto p=centroid(neighbors),old=s.pos[n];if(std::hypot(old.x-p.x,old.y-p.y)<threshold)continue;
      Candidate winner;
      for(double f:{.25,.5,.75,1.}) {
        auto c=evaluate({n},{{old.x+(p.x-old.x)*f,old.y+(p.y-old.y)*f}},"pull "+s.nodes[n].id+" fraction="+std::to_string(f));
        if(c.valid&&(!winner.valid||improvement(c.score,c.center,winner.score,winner.center)))winner=std::move(c);
      }
      accept(winner);
    }
  }
};
}

int main(int argc,char**argv) {
  try {
    Probe p;auto& s=p.s;
    s.width=number(argc,argv,"--width",38720);s.height=number(argc,argv,"--height",38720);
    if(!std::isfinite(s.width)||!std::isfinite(s.height)||s.width<=0||s.height<=0||s.width*s.height>1.5e9)throw std::runtime_error("invalid canvas");
    const std::string output=arg(argc,argv,"--out");p.prefix=output.substr(0,output.size()-4);
    p.useCenter=arg(argc,argv,"--objective","visual")=="center";
    p.repairFans=number(argc,argv,"--repair-fans",1)!=0;
    p.maxSeconds=number(argc,argv,"--seconds",120);
    const std::string family=arg(argc,argv,"--family","all");
    std::unordered_map<std::string,int> ids;std::string line;
    std::ifstream nodes(arg(argc,argv,"--nodes"));if(!nodes)throw std::runtime_error("cannot read nodes");
    while(std::getline(nodes,line)){auto f=split(line);if(f.size()<3||ids.count(f[0]))throw std::runtime_error("invalid node");ids[f[0]]=s.nodes.size();s.nodes.push_back({f[0],std::stod(f[1]),std::stod(f[2])});}
    s.pos.resize(s.nodes.size());s.moved.resize(s.nodes.size());s.incident.resize(s.nodes.size());s.adj.resize(s.nodes.size());
    std::ifstream edges(arg(argc,argv,"--edges"));std::set<std::string> edgeIds;std::set<std::pair<int,int>> uniquePairs;
    if(!edges)throw std::runtime_error("cannot read edges");
    while(std::getline(edges,line)){auto f=split(line);if(f.size()<3||!ids.count(f[1])||!ids.count(f[2])||!edgeIds.insert(f[0]).second)throw std::runtime_error("invalid edge");int a=ids.at(f[1]),b=ids.at(f[2]);if(a==b)throw std::runtime_error("unexpected self edge");int e=s.edges.size();s.edges.push_back({f[0],a,b});s.incident[a].push_back(e);s.incident[b].push_back(e);uniquePairs.insert(std::minmax(a,b));}
    p.centerEdges.assign(uniquePairs.begin(),uniquePairs.end());for(auto [a,b]:p.centerEdges){s.adj[a].push_back(b);s.adj[b].push_back(a);}
    s.changedEdge.resize(s.edges.size());std::vector<bool> present(s.nodes.size());
    std::ifstream positions(arg(argc,argv,"--positions"));if(!positions)throw std::runtime_error("cannot read positions");
    while(std::getline(positions,line)){auto f=split(line);if(f.size()!=3||!ids.count(f[0])||present[ids.at(f[0])])throw std::runtime_error("invalid position");int n=ids.at(f[0]);s.pos[n]={std::stod(f[1]),std::stod(f[2])};present[n]=true;}
    if(std::find(present.begin(),present.end(),false)!=present.end())throw std::runtime_error("missing positions");
    for(int n=0;n<int(s.nodes.size());n++)if(!s.inside(n,s.pos[n]))throw std::runtime_error("initial position outside canvas");
    s.allRoutes();p.current=loadBoundaryPorts(s,arg(argc,argv,"--ports"),s.full());
    if(p.current.overlap||p.current.spacing)throw std::runtime_error("input is not geometrically feasible");
    p.best=p.current;p.center=p.centerScore();
    std::unordered_map<std::string,int> groups;std::ifstream groupFile(arg(argc,argv,"--groups"));p.groupOf.assign(s.nodes.size(),-1);
    if(!groupFile)throw std::runtime_error("cannot read groups");
    while(std::getline(groupFile,line)){auto f=split(line);if(f.size()!=2||!ids.count(f[1])||p.groupOf[ids.at(f[1])]!=-1)throw std::runtime_error("invalid or overlapping group");if(!groups.count(f[0])){groups[f[0]]=s.groups.size();s.groups.emplace_back();p.groupNames.push_back(f[0]);}int g=groups.at(f[0]),n=ids.at(f[1]);s.groups[g].push_back(n);p.groupOf[n]=g;}
    p.journal.open(arg(argc,argv,"--log"));p.journal<<"move\ttransform\tcenterBefore\tcenterAfter\tvisualBefore\tvisualAfter\tcross\thit\tpositions\n";
    const auto initial=p.current;const auto initialCenter=p.center;
    report("start",0,p.current,p.elapsed());s.save(output);s.save(p.prefix+".best.tsv");
    const int rounds=number(argc,argv,"--rounds",4);int completed=0;
    for(int round=0;round<rounds&&!p.expired();round++) {
      const auto before=p.counts.accepted;
      for(const std::string stage:{"rotate","swap","pull"})if((family=="all"||family==stage)&&!p.expired()) {
        if(stage=="rotate")p.rotate();else if(stage=="swap")p.swapGroups();else p.pull();
        s.save(p.prefix+".r"+std::to_string(round+1)+"-"+stage+".tsv");
        report(stage,round+1,p.current,p.elapsed());
      }
      completed++;if(p.counts.accepted==before)break;
    }
    if(!equal(p.current,s.full())||p.center!=p.centerScore())throw std::runtime_error("final metric mismatch");
    s.save(output);report("done",p.counts.proposed,p.current,p.elapsed());
    const auto c=p.counts;std::ostringstream summary;
    summary<<"{\"objective\":"<<std::quoted(p.useCenter?"center":"visual")<<",\"family\":"<<std::quoted(family)
      <<",\"initialVisual\":"<<initial.visual()<<",\"initialCenter\":"<<initialCenter<<",\"visual\":"<<p.current.visual()
      <<",\"cross\":"<<p.current.cross<<",\"hit\":"<<p.current.hit<<",\"center\":"<<p.center<<",\"bestVisual\":"<<p.best.visual()
      <<",\"proposed\":"<<c.proposed<<",\"positionRejected\":"<<c.positionRejected<<",\"routeVariants\":"<<c.routeVariants
      <<",\"routeRejected\":"<<c.routeRejected<<",\"scored\":"<<c.scored<<",\"contactRejected\":"<<c.contactRejected
      <<",\"centerBetterVisualWorse\":"<<c.centerBetterVisualWorse<<",\"visualBetterCenterWorse\":"<<c.visualBetterCenterWorse
      <<",\"accepted\":"<<c.accepted<<",\"rounds\":"<<completed<<",\"timedOut\":"<<(p.expired()?"true":"false")
      <<",\"seconds\":"<<p.elapsed()<<"}";
    std::ofstream(p.prefix+".stats.json")<<summary.str()<<'\n';std::cout<<summary.str()<<'\n';
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
