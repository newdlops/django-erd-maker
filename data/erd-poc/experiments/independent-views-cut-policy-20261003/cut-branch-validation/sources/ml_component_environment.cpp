// Learned-policy environment. It never generates or scores alternate proposals.
// TRY deterministically decodes one model action: rigid translation, or nearest
// card-slot quantization followed by an atomic swap. Rejection has no fallback.
#include "constrained_dual_node_geometry.h"
#include <memory>
#include <map>

namespace {
long policyHardScore(const State& s) {
  long result=hardScore(s);
  for(int e=0;e<int(s.edges.size());e++) {
    const auto edge=s.edges[e];const auto line=s.routes[e];
    result+=!outwardBoundary(s,edge.s,line.a,line.b);
    result+=!outwardBoundary(s,edge.t,line.b,line.a);
  }
  return result;
}
constexpr int FEATURE_COUNT=64;
struct Result {bool legal=false,accepted=false;long gain=0,individualGain=0;const char* reason="frame";int decodedTarget=-1;};
double length(const Segment& s){return std::hypot(s.b.x-s.a.x,s.b.y-s.a.y);}
double distance(Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y);}
struct ComponentEnvironment {
  State a,b;
  std::vector<std::vector<int>> components,groups,nodeGroups;
  std::vector<int> owner,edgeGroup;
  std::vector<std::pair<int,int>> initialEndpoints;
  long initialVisual=0,initialIndividualVisual=0,visual=0,individualVisual=0,initialHard=0,initialIndividualHard=0;
  long attempts=0,accepted=0;
  long acceptedNeutral=0;
  long explorationLimit=0,admissionAllowance=0,savedActionPrefix=-1,acceptedExploratory=0;
  bool allowNeutral=false;
  bool attachedRepresentatives=false;
  bool attachAllPorts=false;
  bool neuralPerimeterPorts=false;
  bool movingRayPorts=false;
  bool jointMoves=false;
  bool neighborMoves=false;
  bool branchMoves=false;
  std::string branchMode="core";
  std::vector<std::vector<int>> branchGroups;
  bool overviewOnly=false; // Isolated-view experiment; never a shared-layout result.
  bool swapSlots=false;
  bool pairActions=false;
  bool portActions=false;
  bool residualPorts=false;
  bool globalScale=false;
  bool areaExploration=false;
  long temporaryRegressionBudget=0;
  double bboxLimit=0;
  std::vector<int> leafParent;
  bool canonicalStart=false;
  long sourceVisual=0,sourceIndividualVisual=0,sourceHard=0,sourceIndividualHard=0;
  double left=1e100,right=-1e100,top=1e100,bottom=-1e100;
  ComponentEnvironment(State overview,State full,std::vector<std::vector<int>> ns,std::vector<std::vector<int>> es,bool verifyProjection=true)
    :a(std::move(overview)),b(std::move(full)),components(std::move(ns)),groups(std::move(es)) {
    if(components.size()!=a.nodes.size()||groups.size()!=a.edges.size())throw std::runtime_error("mapping size");
    owner.assign(b.nodes.size(),-1);edgeGroup.assign(b.edges.size(),-1);nodeGroups.resize(a.nodes.size());
    for(int n=0;n<int(components.size());n++) {
      for(int m:components[n]){if(owner[m]>=0)throw std::runtime_error("duplicate owner");owner[m]=n;}
      left=std::min(left,a.pos[n].x-a.nodes[n].w/2);right=std::max(right,a.pos[n].x+a.nodes[n].w/2);
      top=std::min(top,a.pos[n].y-a.nodes[n].h/2);bottom=std::max(bottom,a.pos[n].y+a.nodes[n].h/2);
    }
    for(int e=0;e<int(groups.size());e++)for(int f:groups[e]) {
      if(edgeGroup[f]>=0)throw std::runtime_error("duplicate relationship");edgeGroup[f]=e;
      nodeGroups[owner[b.edges[f].s]].push_back(e);nodeGroups[owner[b.edges[f].t]].push_back(e);
    }
    for(auto& v:nodeGroups){std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end());}
    for(int n:owner)if(n<0)throw std::runtime_error("unowned node");
    for(int e:edgeGroup)if(e<0)throw std::runtime_error("unowned relationship");
    leafParent.assign(components.size(),-1);
    for(int n=0;n<int(components.size());n++)if(components[n].size()>1) {
      std::map<int,int> counts;
      for(int m:components[n])for(int e:b.incident[m]) {
        const int other=owner[b.edges[e].s==m?b.edges[e].t:b.edges[e].s];
        if(other!=n&&components[other].size()==1)counts[other]++;
      }
      int most=0;for(auto [parent,count]:counts)if(count>most){most=count;leafParent[n]=parent;}
    }
    for(const auto e:a.edges)initialEndpoints.push_back({std::min(e.s,e.t),std::max(e.s,e.t)});
    for(int e=0;e<int(groups.size());e++) {
      const auto old=a.routes[e];if(!project(e))throw std::runtime_error("initial projection type mismatch: "+a.edges[e].id);
      if(verifyProjection&&(!boundarySame(old.a,a.routes[e].a)||!boundarySame(old.b,a.routes[e].b)))throw std::runtime_error("initial product projection mismatch: "+a.edges[e].id);
    }
    initialVisual=visual=a.full().visual();initialIndividualVisual=individualVisual=b.full().visual();
    initialHard=policyHardScore(a);initialIndividualHard=policyHardScore(b);
  }
  double parameter(Point start,Point end,int n,bool exiting) const {
    double enter=0,leave=1;
    for(auto axis:std::array<std::array<double,4>,2>{{
      {{start.x,end.x-start.x,a.pos[n].x-a.nodes[n].w/2,a.pos[n].x+a.nodes[n].w/2}},
      {{start.y,end.y-start.y,a.pos[n].y-a.nodes[n].h/2,a.pos[n].y+a.nodes[n].h/2}}}}) {
      if(std::abs(axis[1])<1e-12)continue;
      const double x=(axis[2]-axis[0])/axis[1],y=(axis[3]-axis[0])/axis[1];
      enter=std::max(enter,std::min(x,y));leave=std::min(leave,std::max(x,y));
    }
    return exiting?leave:enter;
  }
  void canonicalize() {
    // A fixed geometry representation, used only by an explicitly requested
    // experimental reset. No coordinate selection, objective search or repair.
    canonicalStart=true;sourceVisual=visual;sourceIndividualVisual=individualVisual;sourceHard=initialHard;sourceIndividualHard=initialIndividualHard;
    b.allRoutes();for(int e=0;e<int(groups.size());e++)if(!project(e))throw std::runtime_error("canonical representation changes a Leaf pair");
    initialVisual=visual=a.full().visual();initialIndividualVisual=individualVisual=b.full().visual();
    initialHard=policyHardScore(a);initialIndividualHard=policyHardScore(b);
  }
  bool project(int e) {
    // Product projection selects the shortest external member when the June
    // representative joins two models inside the same Leaf card.
    int representative=-1;
    for(int f:groups[e])if(owner[b.edges[f].s]!=owner[b.edges[f].t]&&(representative<0
      ||length(b.routes[f])<length(b.routes[representative])
      ||(length(b.routes[f])==length(b.routes[representative])&&b.edges[f].id<b.edges[representative].id)))representative=f;
    if(representative<0)return false;
    const auto v=b.edges[representative];const int s=owner[v.s],t=owner[v.t];
    if(s==t)return false;
    const auto old=initialEndpoints[e];
    const bool wasLeaf=components[old.first].size()>1||components[old.second].size()>1;
    const bool isLeaf=components[s].size()>1||components[t].size()>1;
    if(wasLeaf!=isLeaf||(wasLeaf&&old!=std::make_pair(std::min(s,t),std::max(s,t))))return false;
    a.edges[e].s=s;a.edges[e].t=t;
    const auto r=b.routes[representative];
    const double x=components[s].size()>1?parameter(r.a,r.b,s,true):0;
    const double y=components[t].size()>1?parameter(r.a,r.b,t,false):1;
    a.routes[e]=segment({rounded(r.a.x+(r.b.x-r.a.x)*x),rounded(r.a.y+(r.b.y-r.a.y)*x)},
      {rounded(r.a.x+(r.b.x-r.a.x)*y),rounded(r.a.y+(r.b.y-r.a.y)*y)});
    return true;
  }
  std::vector<int> incident(int n) const {
    std::vector<int> es;for(int m:components[n])for(int e:b.incident[m])es.push_back(e);
    std::sort(es.begin(),es.end());es.erase(std::unique(es.begin(),es.end()),es.end());return es;
  }
  struct Context {
    std::vector<int> physical,nodes,edges,groups;
    double l=1e100,r=-1e100,t=1e100,b=-1e100;
    Point center() const{return {(l+r)/2,(t+b)/2};}
  };
  int nearestNeighbor(int n) const {
    int chosen=-1;double gapBest=1e100,centerBest=1e100;
    for(int m=0;m<int(a.nodes.size());m++)if(m!=n) {
      const double dx=std::max(0.,std::abs(a.pos[n].x-a.pos[m].x)-(a.nodes[n].w+a.nodes[m].w)/2);
      const double dy=std::max(0.,std::abs(a.pos[n].y-a.pos[m].y)-(a.nodes[n].h+a.nodes[m].h)/2);
      const double gap=std::round(std::hypot(dx,dy)*1e6),center=std::round(distance(a.pos[n],a.pos[m])*1e6);
      if(gap<gapBest||(gap==gapBest&&center<centerBest)){gapBest=gap;centerBest=center;chosen=m;}
    }
    if(chosen<0)throw std::runtime_error("neighbor action requires two cards");return chosen;
  }
  void setBranches(std::vector<std::vector<int>> rows,const std::string& mode="core") {
    if(mode!="core"&&mode!="cut")throw std::runtime_error("unknown branch mode");
    if(!movingRayPorts||jointMoves||neighborMoves||swapSlots||attachAllPorts||attachedRepresentatives)
      throw std::runtime_error("branch contexts require the moving-ray decoder without other grouping");
    if(rows.size()!=components.size())throw std::runtime_error("branch map size");
    for(int n=0;n<int(rows.size());n++) {
      auto& row=rows[n];std::sort(row.begin(),row.end());
      if(row.empty()||row.front()<0||row.back()>=int(rows.size())
         ||!std::binary_search(row.begin(),row.end(),n)
         ||std::adjacent_find(row.begin(),row.end())!=row.end())throw std::runtime_error("invalid branch map row");
    }
    branchGroups=std::move(rows);branchMoves=true;branchMode=mode;
  }
  Context context(int n,int extra=-1) const {
    Context c;c.physical.push_back(n);
    if(branchMoves)c.physical.insert(c.physical.end(),branchGroups.at(n).begin(),branchGroups.at(n).end());
    if(extra>=0)c.physical.push_back(extra);
    if(neighborMoves)c.physical.push_back(nearestNeighbor(n));
    if(jointMoves) {
      if(leafParent[n]>=0)c.physical.push_back(leafParent[n]);
      else for(int m=0;m<int(leafParent.size());m++)if(leafParent[m]==n)c.physical.push_back(m);
    }
    const auto unique=[](std::vector<int>& v){std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end());};
    unique(c.physical);
    for(int m:c.physical) {
      c.nodes.insert(c.nodes.end(),components[m].begin(),components[m].end());
      c.groups.insert(c.groups.end(),nodeGroups[m].begin(),nodeGroups[m].end());
      c.l=std::min(c.l,a.pos[m].x-a.nodes[m].w/2);c.r=std::max(c.r,a.pos[m].x+a.nodes[m].w/2);
      c.t=std::min(c.t,a.pos[m].y-a.nodes[m].h/2);c.b=std::max(c.b,a.pos[m].y+a.nodes[m].h/2);
    }
    unique(c.nodes);unique(c.groups);
    for(int m:c.nodes)c.edges.insert(c.edges.end(),b.incident[m].begin(),b.incident[m].end());unique(c.edges);
    return c;
  }
  static void mark(State& s,const std::vector<int>& ns,const std::vector<int>& es,bool value) {
    for(int n:ns)s.moved[n]=value;for(int e:es)s.changedEdge[e]=value;
  }
  static NodeCost cost(const State& s,const std::vector<int>& ns,const std::vector<int>& es) {
    NodeCost c{s.local(ns,es),0};
    for(int e:es) {
      const auto v=s.edges[e];const auto line=s.routes[e];
      c.hard+=!outwardBoundary(s,v.s,line.a,line.b)+!outwardBoundary(s,v.t,line.b,line.a);
      c.hard+=hits(line,s.pos[v.s],s.nodes[v.s],-.02)+hits(line,s.pos[v.t],s.nodes[v.t],-.02);
      for(int f=0;f<int(s.edges.size());f++)if(f!=e&&(!s.changedEdge[f]||e<f)) {
        const auto w=s.edges[f];const bool adjacent=v.s==w.s||v.s==w.t||v.t==w.s||v.t==w.t;
        c.hard+=2*(long(adjacent&&crosses(line,s.routes[f]))+boundaryContact(s,e,line,f,s.routes[f]));
      }
    }
    return c;
  }
  Result trial(int n,Point delta,bool commit) {
    if(n<0||n>=int(components.size())||!std::isfinite(delta.x)||!std::isfinite(delta.y))throw std::runtime_error("invalid policy action");
    attempts++;
    delta={rounded(delta.x),rounded(delta.y)};
    Result r;int target=-1;
    if(neighborMoves){if(jointMoves||swapSlots||!attachAllPorts)throw std::runtime_error("neighbor decoder requires attached ports and no other grouping");r.decodedTarget=nearestNeighbor(n);}
    if(swapSlots) {
      if(jointMoves||attachedRepresentatives||attachAllPorts)throw std::runtime_error("swap decoder requires single components and ray ports");
      // Vector quantization of the model's desired point into a card slot.
      // No metric is consulted, and rejection never tries a second slot.
      const Point desired{a.pos[n].x+delta.x,a.pos[n].y+delta.y};double nearest=1e100;
      for(int m=0;m<int(components.size());m++) {
        const double d=std::round(distance(desired,a.pos[m])*1e6)/1e6;
        if(d<nearest){nearest=d;target=m;}
      }
      r.decodedTarget=target;
      if(target==n){r.reason="same-slot";return r;}
      delta={a.pos[target].x-a.pos[n].x,a.pos[target].y-a.pos[n].y};
    }
    const auto c=context(n,target);const auto& an=c.physical;const auto& es=c.edges;const auto& gs=c.groups;const auto& ns=c.nodes;
    const auto movement=[&](int m){return m==target?Point{-delta.x,-delta.y}:delta;};
    for(int m:an) {
      const auto d=movement(m);const Point p{a.pos[m].x+d.x,a.pos[m].y+d.y};
      if(p.x-a.nodes[m].w/2<left||p.x+a.nodes[m].w/2>right||p.y-a.nodes[m].h/2<top||p.y+a.nodes[m].h/2>bottom)return r;
    }
    mark(a,an,gs,true);mark(b,ns,es,true);
    const auto ca=cost(a,an,gs),cb=cost(b,ns,es);
    std::vector<Segment> ar,br;std::vector<Edge> ae;std::vector<Point> ap,bp;
    for(int m:an)ap.push_back(a.pos[m]);
    for(int e:gs){ar.push_back(a.routes[e]);ae.push_back(a.edges[e]);}
    for(int e:es)br.push_back(b.routes[e]);for(int m:ns)bp.push_back(b.pos[m]);
    std::set<int> attached;
    if(attachAllPorts)attached.insert(es.begin(),es.end());
    if(attachedRepresentatives)for(int g:gs) {
      int representative=-1;
      for(int e:groups[g])if(owner[b.edges[e].s]!=owner[b.edges[e].t]&&(representative<0||length(b.routes[e])<length(b.routes[representative])
        ||(length(b.routes[e])==length(b.routes[representative])&&b.edges[e].id<b.edges[representative].id)))representative=e;
      if(representative>=0)attached.insert(representative);
    }
    for(int m:an){const auto d=movement(m);a.pos[m].x+=d.x;a.pos[m].y+=d.y;}
    // Quantize the supplied displacement once; preserve the input's original
    // center precision in both physical and individual representations.
    for(int m:ns){const auto d=movement(owner[m]);b.pos[m].x+=d.x;b.pos[m].y+=d.y;}
    for(int e:es){const auto v=b.edges[e];
      if(attached.count(e)) {
        auto r=b.routes[e];
        if(b.moved[v.s]){r.a.x=rounded(r.a.x+delta.x);r.a.y=rounded(r.a.y+delta.y);}
        if(b.moved[v.t]){r.b.x=rounded(r.b.x+delta.x);r.b.y=rounded(r.b.y+delta.y);}
        b.routes[e]=segment(r.a,r.b);
      }else if(movingRayPorts) {
        const auto old=b.routes[e];
        if(b.moved[v.s]&&b.moved[v.t])b.routes[e]=segment({rounded(old.a.x+delta.x),rounded(old.a.y+delta.y)},
          {rounded(old.b.x+delta.x),rounded(old.b.y+delta.y)});
        else if(b.moved[v.s])b.routes[e]=segment(port(b.pos[v.s],b.nodes[v.s],old.b),old.b);
        else if(b.moved[v.t])b.routes[e]=segment(old.a,port(b.pos[v.t],b.nodes[v.t],old.a));
      }else b.routes[e]=segment(port(b.pos[v.s],b.nodes[v.s],b.pos[v.t]),port(b.pos[v.t],b.nodes[v.t],b.pos[v.s]));
    }
    bool projected=true;for(int e:gs)projected=project(e)&&projected;
    r.reason=projected?"spacing":"projection";bool valid=projected;
    for(int m:an)valid=valid&&spaced(a,m);for(int m:ns)valid=valid&&spaced(b,m);
    if(valid) {
      const auto da=cost(a,an,gs),db=cost(b,ns,es);
      r.gain=ca.score.visual()-da.score.visual();r.individualGain=cb.score.visual()-db.score.visual();
      r.legal=da.hard<=ca.hard&&db.hard<=cb.hard;
      const bool neutral=allowNeutral&&(delta.x!=0||delta.y!=0);
      const bool monotone=r.gain>=0&&(overviewOnly?(r.gain>0||neutral):r.individualGain>=0&&(r.gain||r.individualGain||neutral));
      const long allowance=std::clamp(admissionAllowance,0L,explorationLimit);
      const bool exploratory=explorationLimit>0&&r.gain>=-allowance&&visual-r.gain<=initialVisual+explorationLimit
        &&(overviewOnly||(r.individualGain>=-allowance&&individualVisual-r.individualGain<=initialIndividualVisual+explorationLimit))
        &&(r.gain||(!overviewOnly&&r.individualGain)||neutral);
      r.accepted=r.legal&&(monotone||exploratory);
      r.reason=da.hard>ca.hard?"overview-hard":db.hard>cb.hard?"individual-hard":
        r.accepted?(r.gain<0||(!overviewOnly&&r.individualGain<0)?"accepted-exploratory":r.gain==0&&(overviewOnly||r.individualGain==0)?"accepted-neutral":"accepted"):
        r.gain<0?"overview-regression":!overviewOnly&&r.individualGain<0?"individual-regression":"equal";
    }
    if(commit&&r.accepted){visual-=r.gain;individualVisual-=r.individualGain;accepted++;acceptedNeutral+=r.gain==0&&(overviewOnly||r.individualGain==0);acceptedExploratory+=r.gain<0||(!overviewOnly&&r.individualGain<0);}
    else {
      for(size_t i=0;i<an.size();i++)a.pos[an[i]]=ap[i];for(size_t i=0;i<ns.size();i++)b.pos[ns[i]]=bp[i];
      for(size_t i=0;i<gs.size();i++){a.routes[gs[i]]=ar[i];a.edges[gs[i]]=ae[i];}
      for(size_t i=0;i<es.size();i++)b.routes[es[i]]=br[i];
    }
    mark(a,an,gs,false);mark(b,ns,es,false);return r;
  }
  std::vector<double> features(int n,double& scale) {
    const auto contextValue=context(n);const Point p=contextValue.center();const auto& es=contextValue.edges;const auto& gs=contextValue.groups;
    const auto& an=contextValue.physical;const auto& ns=contextValue.nodes;
    mark(a,an,gs,true);mark(b,ns,es,true);
    const auto ca=cost(a,an,gs),cb=cost(b,ns,es);
    std::vector<Point> neighbors;std::vector<double> lengths;
    for(int e:es){const auto v=b.edges[e];const int m=b.moved[v.s]?v.t:v.s;if(b.moved[m])continue;neighbors.push_back(b.pos[m]);lengths.push_back(distance(p,b.pos[m]));}
    std::sort(lengths.begin(),lengths.end());scale=std::max(512.,lengths.empty()?512.:lengths[lengths.size()/2]);
    std::vector<double> out;const auto add=[&](double x){out.push_back(std::clamp(x,-8.,8.));};
    add((contextValue.r-contextValue.l)/scale);add((contextValue.b-contextValue.t)/scale);add(std::log1p(ns.size()));add(std::log1p(es.size()));
    add(std::log1p(ca.score.cross));add(std::log1p(ca.score.hit));add(std::log1p(cb.score.cross));add(std::log1p(cb.score.hit));
    add(std::log1p(ca.hard));add(std::log1p(cb.hard));
    double x=0,y=0,xx=0,yy=0,ux=0,uy=0;
    for(Point q:neighbors){const double dx=(q.x-p.x)/scale,dy=(q.y-p.y)/scale,len=std::max(.001,std::hypot(dx,dy));x+=dx;y+=dy;xx+=dx*dx;yy+=dy*dy;ux+=dx/len;uy+=dy/len;}
    const double count=std::max(size_t(1),neighbors.size());add(x/count);add(y/count);add(xx/count);add(yy/count);add(ux/count);add(uy/count);
    std::vector<std::pair<double,int>> nearby;
    for(int m=0;m<int(a.nodes.size());m++)if(!a.moved[m])nearby.push_back({std::round(distance(p,a.pos[m])*1e6)/1e6,m});std::sort(nearby.begin(),nearby.end());
    for(int k=0;k<4;k++) {
      if(k>=int(nearby.size())){for(int j=0;j<4;j++)add(0);continue;}
      const int m=nearby[k].second;add((a.pos[m].x-p.x)/scale);add((a.pos[m].y-p.y)/scale);add(a.nodes[m].w/scale);add(a.nodes[m].h/scale);
    }
    struct Conflict {Segment line;double distance;bool hit;long cross;};std::vector<Conflict> conflicts;
    for(int f=0;f<int(b.edges.size());f++)if(!b.moved[b.edges[f].s]&&!b.moved[b.edges[f].t]) {
      const auto line=b.routes[f];bool hit=false;long crossing=0;
      for(int m:ns)hit=hit||hits(line,b.pos[m],b.nodes[m]);for(int e:es)crossing+=crosses(line,b.routes[e]);
      if(!hit&&!crossing)continue;
      const double dx=line.b.x-line.a.x,dy=line.b.y-line.a.y,t=std::clamp(((p.x-line.a.x)*dx+(p.y-line.a.y)*dy)/std::max(.01,dx*dx+dy*dy),0.,1.);
      conflicts.push_back({line,std::hypot(p.x-line.a.x-t*dx,p.y-line.a.y-t*dy),hit,crossing});
    }
    std::stable_sort(conflicts.begin(),conflicts.end(),[](auto x,auto y){return std::round(x.distance*1e6)<std::round(y.distance*1e6);});
    for(int k=0;k<4;k++) {
      if(k>=int(conflicts.size())){for(int j=0;j<6;j++)add(0);continue;}
      auto c=conflicts[k];if(c.line.a.x>c.line.b.x||(c.line.a.x==c.line.b.x&&c.line.a.y>c.line.b.y))std::swap(c.line.a,c.line.b);
      add((c.line.a.x-p.x)/scale);add((c.line.a.y-p.y)/scale);add((c.line.b.x-p.x)/scale);add((c.line.b.y-p.y)/scale);add(c.hit);add(std::log1p(c.cross));
    }
    add((contextValue.l-left)/scale);add((right-contextValue.r)/scale);
    add((contextValue.t-top)/scale);add((bottom-contextValue.b)/scale);
    add(lengths.empty()?0:lengths.front()/scale);add(lengths.empty()?0:lengths.back()/scale);
    add(std::log1p(scale/512));add(std::log1p(gs.size()));
    mark(a,an,gs,false);mark(b,ns,es,false);
    if(out.size()!=FEATURE_COUNT)throw std::runtime_error("feature schema");return out;
  }
  void save(const std::string& path) {
    const auto x=a.full(),y=b.full();const auto h=policyHardScore(a),ih=policyHardScore(b);
    if(x.visual()!=visual||y.visual()!=individualVisual||h>initialHard||ih>initialIndividualHard||x.overlap||x.spacing||y.overlap||y.spacing)
      throw std::runtime_error("global final audit disagrees with rollout");
    a.save(path);b.save(path+".individual");
    std::ofstream f(path+".stats.json");f<<"{\"initialVisual\":"<<(canonicalStart?sourceVisual:initialVisual)<<",\"visual\":"<<visual
      <<",\"initialIndividualVisual\":"<<(canonicalStart?sourceIndividualVisual:initialIndividualVisual)
      <<",\"individualVisual\":"<<individualVisual<<",\"hardConditions\":"<<h<<",\"initialHardConditions\":"<<(canonicalStart?sourceHard:initialHard)
      <<",\"individualHardConditions\":"<<ih<<",\"initialIndividualHardConditions\":"<<(canonicalStart?sourceIndividualHard:initialIndividualHard)
      <<",\"canonicalStart\":"<<(canonicalStart?"true":"false")<<",\"rolloutInitialVisual\":"<<initialVisual<<",\"rolloutInitialIndividualVisual\":"<<initialIndividualVisual
      <<",\"jointMoves\":"<<(jointMoves?"true":"false")
      <<",\"neighborMoves\":"<<(neighborMoves?"true":"false")
      <<",\"branchMoves\":"<<(branchMoves?"true":"false")
      <<",\"branchMode\":\""<<(branchMoves?branchMode:"none")<<'"'
      <<",\"movingRayPorts\":"<<(movingRayPorts?"true":"false")
      <<",\"overviewOnly\":"<<(overviewOnly?"true":"false")
      <<",\"allowNeutral\":"<<(allowNeutral?"true":"false")<<",\"acceptedNeutralActions\":"<<acceptedNeutral
      <<",\"explorationLimit\":"<<explorationLimit<<",\"savedActionPrefix\":"<<savedActionPrefix<<",\"acceptedExploratoryActions\":"<<acceptedExploratory
      <<",\"swapSlots\":"<<(swapSlots?"true":"false")
      <<",\"pairActions\":"<<(pairActions?"true":"false")
      <<",\"portActions\":"<<(portActions?"true":"false")
      <<",\"residualPorts\":"<<(residualPorts?"true":"false")
      <<",\"globalScale\":"<<(globalScale?"true":"false")<<",\"bboxLimit\":"<<std::setprecision(15)<<bboxLimit
      <<",\"areaExploration\":"<<(areaExploration?"true":"false")<<",\"temporaryRegressionBudget\":"<<temporaryRegressionBudget
      <<",\"globalAttachedPorts\":"<<(globalScale&&attachAllPorts?"true":"false")
      <<",\"overlap\":0,\"spacing\":0,\"policyActionsEvaluated\":"<<attempts<<",\"acceptedActions\":"<<accepted
      <<",\"heuristicSearchCalls\":0,\"proposalAuthority\":\"external-policy\",\"decoder\":\""
      <<(neuralPerimeterPorts?"model card translations and boundary-perimeter endpoint offsets":pairActions?"model-selected card pair; exact center swap with ray ports":movingRayPorts?"model component translation; reattach moving endpoints to fixed opposite ports":neighborMoves?"model translation of a card and its nearest rectangle neighbor; all ports attached":globalScale?(attachAllPorts?"model log scales about physical bounds; rigid Leaf translation and attached boundary ports":"model log scales about current physical bounds; rigid Leaf translation and center-ray ports"):portActions?"model boundary parameters on original endpoint sides; straight lines":swapSlots?"model desired point quantized to nearest card slot; atomic center swap with ray ports":attachAllPorts?"rigid component translation with every port attached":attachedRepresentatives?"rigid translation, attached overview ports, center-ray other ports":"rigid component translation plus center-ray incident ports")<<"\"}\n";
  }
};
std::vector<std::vector<int>> mapping(const std::string& file,const std::vector<std::string>& outer,const std::vector<std::string>& inner) {
  std::unordered_map<std::string,int> o,i;for(int n=0;n<int(outer.size());n++)o[outer[n]]=n;for(int n=0;n<int(inner.size());n++)i[inner[n]]=n;
  std::vector<std::vector<int>> rows(outer.size());std::ifstream input(file);std::string line;if(!input)throw std::runtime_error("missing mapping");
  while(std::getline(input,line)){auto fields=split(line);auto& row=rows.at(o.at(fields[0]));if(!row.empty())throw std::runtime_error("duplicate mapping");for(size_t k=1;k<fields.size();k++)row.push_back(i.at(fields[k]));}
  for(const auto& row:rows)if(row.empty())throw std::runtime_error("empty mapping");return rows;
}
ComponentEnvironment load(const std::string& dir) {
  State a=loadView(dir,""),b=loadView(dir,"individual.");std::vector<std::string> an,bn,ae,be;
  for(auto n:a.nodes)an.push_back(n.id);for(auto n:b.nodes)bn.push_back(n.id);for(auto e:a.edges)ae.push_back(e.id);for(auto e:b.edges)be.push_back(e.id);
  auto ns=mapping(dir+"/components.tsv",an,bn),es=mapping(dir+"/groups.tsv",ae,be);return ComponentEnvironment(a,b,ns,es);
}
std::vector<std::vector<int>> cutBranches(const State& state) {
  const int count=state.nodes.size();std::vector<std::set<int>> graph(count);
  for(const auto edge:state.edges)if(edge.s!=edge.t){graph[edge.s].insert(edge.t);graph[edge.t].insert(edge.s);}
  std::vector<int> component(count,-1);std::vector<std::vector<int>> components,rows(count);
  for(int n=0;n<count;n++)if(component[n]<0) {
    const int id=components.size();std::vector<int> row{n};component[n]=id;
    for(size_t k=0;k<row.size();k++)for(int m:graph[row[k]])if(component[m]<0){component[m]=id;row.push_back(m);}
    components.push_back(std::move(row));
  }
  for(int n=0;n<count;n++) {
    rows[n]={n};if(graph[n].size()<2)continue;
    std::vector<bool> seen(count,false);seen[n]=true;std::vector<std::vector<int>> parts;
    for(int start:components[component[n]])if(!seen[start]) {
      std::vector<int> part{start};seen[start]=true;
      for(size_t k=0;k<part.size();k++)for(int m:graph[part[k]])if(!seen[m]){seen[m]=true;part.push_back(m);}
      std::sort(part.begin(),part.end());parts.push_back(std::move(part));
    }
    if(parts.size()<2)continue;
    size_t trunk=0;
    for(size_t k=1;k<parts.size();k++)if(parts[k].size()>parts[trunk].size()
      ||(parts[k].size()==parts[trunk].size()&&parts[k].front()<parts[trunk].front()))trunk=k;
    for(size_t k=0;k<parts.size();k++)if(k!=trunk)rows[n].insert(rows[n].end(),parts[k].begin(),parts[k].end());
    std::sort(rows[n].begin(),rows[n].end());
  }
  return rows;
}
std::vector<std::vector<int>> graphBranches(const State& state,const std::string& mode="core") {
  // Membership only: source coordinates, scores and proposed actions are unused.
  if(mode=="cut")return cutBranches(state);
  if(mode!="core")throw std::runtime_error("unknown branch mode");
  const int count=state.nodes.size();std::vector<std::set<int>> graph(count);
  for(const auto edge:state.edges)if(edge.s!=edge.t){graph[edge.s].insert(edge.t);graph[edge.t].insert(edge.s);}
  std::vector<bool> core(count,true);std::vector<int> degree(count),queue,owner(count,-1);
  for(int n=0;n<count;n++){degree[n]=graph[n].size();if(degree[n]<2)queue.push_back(n);}
  for(size_t k=0;k<queue.size();k++) {
    const int n=queue[k];if(!core[n])continue;core[n]=false;
    for(int m:graph[n])if(core[m]&&--degree[m]<2)queue.push_back(m);
  }
  std::vector<std::vector<int>> rows(count);
  for(int n=0;n<count;n++) {
    rows[n].push_back(n);if(!core[n])continue;std::set<int> seen{n};
    for(size_t k=0;k<rows[n].size();k++)for(int m:graph[rows[n][k]])if(!core[m]&&seen.insert(m).second) {
      if(owner[m]>=0)throw std::runtime_error("shared tree outside graph core");
      owner[m]=n;rows[n].push_back(m);
    }
    std::sort(rows[n].begin(),rows[n].end());
  }
  return rows;
}
void loadBranches(ComponentEnvironment& env,const std::string& dir,const std::string& mode="core") {
  std::vector<std::string> ids;for(const auto& node:env.a.nodes)ids.push_back(node.id);
  env.setBranches(mapping(dir+"/branches.tsv",ids,ids),mode);
  if(env.branchGroups!=graphBranches(env.a,mode))throw std::runtime_error("branch map differs from source graph");
}
bool improvesCheckpoint(const ComponentEnvironment& env,const ComponentEnvironment& best) {
  return env.visual<best.visual&&(env.overviewOnly||env.individualVisual<=best.individualVisual);
}
void restoreCheckpoint(ComponentEnvironment& env,const ComponentEnvironment& best) {
  const auto attempts=env.attempts,accepted=env.accepted,neutral=env.acceptedNeutral,exploratory=env.acceptedExploratory;
  env=best;env.savedActionPrefix=best.attempts;env.attempts=attempts;env.accepted=accepted;
  env.acceptedNeutral=neutral;env.acceptedExploratory=exploratory;env.admissionAllowance=0;
}
ComponentEnvironment simpleSynthetic(uint64_t seed) {
  std::mt19937_64 rng(seed);State s;const int count=24+rng()%25,columns=8;
  s.nodes.resize(count);s.pos.resize(count);s.incident.resize(count);s.moved.resize(count);
  const double gap=400+rng()%600;
  const bool dense=seed%4==0;
  for(int n=0;n<count;n++){s.nodes[n]={std::to_string(n),double(100+rng()%241),double(60+rng()%121)};s.pos[n]={double(n%columns)*gap+300,double(n/columns)*gap+300};}
  if(dense)for(int n=0;n<count;n++) {
    s.nodes[n].w=260;s.nodes[n].h=100;
    s.pos[n]={300.+(n%columns)*324.+double(int(rng()%7)-3),300.+(n/columns)*150.+double(int(rng()%7)-3)};
  }
  std::set<std::pair<int,int>> pairs;while(pairs.size()<size_t(count*1.4)){int a=rng()%count,b=rng()%count;if(a==b)continue;if(a>b)std::swap(a,b);pairs.insert({a,b});}
  if(seed%8==0)for(int n=1;n<count;n++)pairs.insert({0,n});
  for(auto [a,b]:pairs){const int e=s.edges.size();s.edges.push_back({std::to_string(e),a,b});s.incident[a].push_back(e);s.incident[b].push_back(e);}
  s.changedEdge.resize(s.edges.size());s.allRoutes();std::vector<std::vector<int>> ns,es;
  for(int n=0;n<count;n++)ns.push_back({n});for(int e=0;e<int(s.edges.size());e++)es.push_back({e});return ComponentEnvironment(s,s,ns,es);
}
ComponentEnvironment synthetic(uint64_t seed) {
  if(seed%2==0)return simpleSynthetic(seed);
  std::mt19937_64 rng(seed);State a,b;const int count=24+rng()%17,columns=6;
  std::vector<std::vector<int>> components,groups;std::vector<int> ordinary,leaves;
  for(int n=0;n<count;n++) {
    const int members=n%4==0?(n==0&&seed%3==0?16+rng()%37:2+rng()%7):1;
    const double w=120+rng()%141,h=60+rng()%101;
    const int cols=members>8?8:members>1?2:1,rows=(members+cols-1)/cols;
    const double aw=cols*w+(cols-1)*56+(members>1?48:0),ah=rows*h+(rows-1)*42+(members>1?48:0);
    const Point p{1200.+(n%columns)*(seed%3==0?3000.:1100.),650.+(n/columns)*1200.};
    a.nodes.push_back({"component:"+std::to_string(n),aw,ah});a.pos.push_back(p);
    std::vector<int> ids;
    for(int k=0;k<members;k++) {
      ids.push_back(b.nodes.size());b.nodes.push_back({std::to_string(b.nodes.size()),w,h});
      b.pos.push_back({p.x-aw/2+(members>1?24:0)+w/2+(k%cols)*(w+56),p.y-ah/2+(members>1?24:0)+h/2+(k/cols)*(h+42)});
    }
    // Odd final rows still occupy the exact same bounding rectangle.
    components.push_back(ids);(members>1?leaves:ordinary).push_back(n);
  }
  std::set<std::pair<int,int>> pairs;
  while(pairs.size()<size_t(ordinary.size()*1.4)) {
    int n=ordinary[rng()%ordinary.size()],m=ordinary[rng()%ordinary.size()];if(n==m)continue;if(n>m)std::swap(n,m);pairs.insert({n,m});
  }
  for(int n:leaves){int m=seed%4==1?ordinary[0]:ordinary[rng()%ordinary.size()];pairs.insert({std::min(n,m),std::max(n,m)});}
  if(seed%4==1)for(int n:ordinary)if(n!=ordinary[0])pairs.insert({ordinary[0],n});
  for(auto [n,m]:pairs) {
    const int e=a.edges.size();a.edges.push_back({"group:"+std::to_string(e),n,m});std::vector<int> ids;
    for(int source:components[n])for(int target:components[m]) {
      ids.push_back(b.edges.size());b.edges.push_back({std::to_string(b.edges.size()),source,target});
    }
    groups.push_back(ids);
  }
  for(State* s:{&a,&b}) {
    s->incident.resize(s->nodes.size());s->moved.resize(s->nodes.size());s->changedEdge.resize(s->edges.size());
    for(int e=0;e<int(s->edges.size());e++){s->incident[s->edges[e].s].push_back(e);s->incident[s->edges[e].t].push_back(e);}s->allRoutes();
  }
  return ComponentEnvironment(a,b,components,groups,false);
}
void array(std::ostream& out,const std::vector<double>& values){out<<'[';for(size_t i=0;i<values.size();i++){if(i)out<<',';out<<values[i];}out<<']';}
void dataset(int argc,char**argv) {
  std::ofstream out(arg(argc,argv,"--dataset"));out<<std::setprecision(9);
  const int graphs=number(argc,argv,"--graphs",96),samples=number(argc,argv,"--samples",64);const uint64_t seed=number(argc,argv,"--seed",52381);
  const double seconds=number(argc,argv,"--seconds",20);if(graphs<8||graphs>256||samples<8||samples>128||seconds>30)throw std::runtime_error("dataset budget");
  std::mt19937_64 rng(seed);std::normal_distribution<double> normal;std::uniform_real_distribution<double> unit(0,1);
  auto start=std::chrono::steady_clock::now();long rows=0,actions=0,positives=0;
  for(int g=0;g<graphs;g++) {
    if(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>seconds)break;auto env=synthetic(seed+g);
    env.attachedRepresentatives=arg(argc,argv,"--decoder","ray")=="attached";
    env.attachAllPorts=arg(argc,argv,"--decoder","ray")=="attached-all";
    env.movingRayPorts=arg(argc,argv,"--decoder","ray")=="moving-ray";
    env.jointMoves=arg(argc,argv,"--joint","0")=="1";
    env.neighborMoves=arg(argc,argv,"--neighbors","0")=="1";
    env.swapSlots=arg(argc,argv,"--decoder","ray")=="swap";
    env.overviewOnly=arg(argc,argv,"--overview-only","0")=="1";
    if(arg(argc,argv,"--branches","0")=="1") {
      const auto mode=arg(argc,argv,"--branch-mode","core");env.setBranches(graphBranches(env.a,mode),mode);
    }
    for(int n=0;n<int(env.components.size());n++) {
      double scale;auto features=env.features(n,scale);out<<"{\"graph\":"<<g<<",\"branchMoves\":"<<(env.branchMoves?"true":"false")<<",\"branchMode\":\""<<(env.branchMoves?env.branchMode:"none")<<"\",\"contextCards\":"<<env.context(n).physical.size()<<",\"objective\":\""<<(env.overviewOnly?"overview":"both")<<"\",\"features\":";array(out,features);out<<",\"actions\":[";
      for(int k=0;k<samples;k++) {
        const double radius=env.swapSlots?std::exp(std::log(.08)+unit(rng)*std::log(25.)):std::exp(std::log(.0005)+unit(rng)*std::log(3000.));const Point action{normal(rng)*radius,normal(rng)*radius};
        const auto r=env.trial(n,{action.x*scale,action.y*scale},false);const long gain=r.accepted?r.gain+(env.overviewOnly?0:r.individualGain):0;
        if(k)out<<',';out<<'['<<action.x<<','<<action.y<<','<<gain<<']';actions++;positives+=gain>0;
      }
      out<<"]}\n";rows++;
    }
  }
  std::cout<<"{\"states\":"<<rows<<",\"randomActions\":"<<actions<<",\"positiveActions\":"<<positives<<",\"seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<"}\n";
}
void adaptationDataset(int argc,char**argv) {
  auto env=load(arg(argc,argv,"--directory"));
  env.attachAllPorts=arg(argc,argv,"--decoder","attached-all")=="attached-all";
  env.movingRayPorts=arg(argc,argv,"--decoder","attached-all")=="moving-ray";
  env.attachedRepresentatives=arg(argc,argv,"--decoder","attached-all")=="attached";
  env.jointMoves=arg(argc,argv,"--joint","0")=="1";
  env.neighborMoves=arg(argc,argv,"--neighbors","0")=="1";
  env.swapSlots=arg(argc,argv,"--decoder","attached-all")=="swap";
  env.overviewOnly=arg(argc,argv,"--overview-only","0")=="1";
  if(arg(argc,argv,"--branches","0")=="1")loadBranches(env,arg(argc,argv,"--directory"),arg(argc,argv,"--branch-mode","core"));
  const int samples=number(argc,argv,"--samples",64);const double seconds=number(argc,argv,"--seconds",20);
  if(samples<8||samples>64||seconds<=0||seconds>20)throw std::runtime_error("adaptation data budget");
  std::ofstream out(arg(argc,argv,"--adapt-dataset"));out<<std::setprecision(10);
  const uint64_t seed=number(argc,argv,"--seed",95031);std::mt19937_64 rng(seed);
  std::normal_distribution<double> normal;std::uniform_real_distribution<double> unit(0,1);
  std::vector<long> ap,bp;env.a.full(&ap);env.b.full(&bp);if(!env.overviewOnly)for(int n=0;n<int(ap.size());n++)for(int m:env.components[n])ap[n]+=bp[m];
  std::vector<int> order(ap.size());std::iota(order.begin(),order.end(),0);std::stable_sort(order.begin(),order.end(),[&](int n,int m){return ap[n]>ap[m];});
  auto start=std::chrono::steady_clock::now();long rows=0,actions=0,positives=0;std::map<std::string,long> reasons;
  for(int n:order) {
    if(!ap[n]||std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=seconds)break;
    double scale;auto features=env.features(n,scale);out<<"{\"graph\":100000,\"branchMoves\":"<<(env.branchMoves?"true":"false")<<",\"branchMode\":\""<<(env.branchMoves?env.branchMode:"none")<<"\",\"contextCards\":"<<env.context(n).physical.size()<<",\"objective\":\""<<(env.overviewOnly?"overview":"both")<<"\",\"features\":";array(out,features);out<<",\"actions\":[";
    for(int k=0;k<samples*(ap[n]>64?2:1);k++) {
      const double radius=env.swapSlots?std::exp(std::log(.08)+unit(rng)*std::log(25.)):std::exp(std::log(.0005)+unit(rng)*std::log(3000.));const Point action{normal(rng)*radius,normal(rng)*radius};
      const auto r=env.trial(n,{action.x*scale,action.y*scale},false);const long gain=r.accepted?r.gain+(env.overviewOnly?0:r.individualGain):0;
      if(k)out<<',';out<<'['<<action.x<<','<<action.y<<','<<gain<<']';actions++;positives+=gain>0;reasons[r.reason]++;
    }
    out<<"]}\n";rows++;
  }
  if(env.accepted||env.a.full().visual()!=env.initialVisual||env.b.full().visual()!=env.initialIndividualVisual)throw std::runtime_error("adaptation collection changed source state");
  std::cout<<"{\"domain\":\"Captain reward observations, no committed moves\",\"graphId\":100000,\"states\":"<<rows<<",\"randomActions\":"<<actions
    <<",\"positiveActions\":"<<positives<<",\"seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<",\"reasons\":{";
  bool first=true;for(auto [key,value]:reasons){if(!first)std::cout<<',';first=false;std::cout<<'"'<<key<<"\":"<<value;}std::cout<<"}}\n";
}
void componentSelfTest() {
  long checked=0,swapsAccepted=0,neighborsAccepted=0,movingRayAccepted=0,neutralAccepted=0;std::mt19937_64 rng(75501);
  for(int g=0;g<48;g++) {
    auto env=synthetic(41000+g);env.attachedRepresentatives=g%3==1;env.attachAllPorts=g%3==2;const auto a=env.a.full(),b=env.b.full();
    env.jointMoves=g%2==1;
    env.overviewOnly=g>=8;
    if(g>=16&&g<24){env.swapSlots=true;env.overviewOnly=g%2;env.jointMoves=false;env.attachedRepresentatives=false;env.attachAllPorts=false;}
    if(g>=24&&g<32){env.neighborMoves=true;env.jointMoves=false;env.attachedRepresentatives=false;env.attachAllPorts=true;env.overviewOnly=g%2;}
    if(g>=32){env.movingRayPorts=true;env.jointMoves=false;env.attachedRepresentatives=false;env.attachAllPorts=false;env.overviewOnly=g%2;}
    if(g>=40)env.allowNeutral=true;
    if(g%2==0) {
      for(State* state:{&env.a,&env.b})for(auto& p:state->pos){p.x+=.003479343;p.y+=.003419839;}
      env.left+=.003479343;env.right+=.003479343;env.top+=.003419839;env.bottom+=.003419839;
      env.canonicalize();
    }
    if(a.spacing||b.spacing)throw std::runtime_error("synthetic starting spacing");
    for(int n=0;n<int(env.components.size());n++)for(int k=0;k<4;k++) {
      double scale;env.features(n,scale);const long oldVisual=env.visual,oldIndividual=env.individualVisual;
      const int radius=env.swapSlots?2000:500;const auto oldA=env.a.pos,oldB=env.b.pos;const auto oldRoutes=env.b.routes;
      const auto r=env.trial(n,{double(int(rng()%(2*radius+1))-radius),double(int(rng()%(2*radius+1))-radius)},true);
      swapsAccepted+=env.swapSlots&&r.accepted;
      neighborsAccepted+=env.neighborMoves&&r.accepted;
      movingRayAccepted+=env.movingRayPorts&&r.accepted;
      if(env.a.full().visual()!=env.visual||env.b.full().visual()!=env.individualVisual)throw std::runtime_error("delta mismatch");checked++;
      if(!r.accepted) {
        for(int m=0;m<int(oldA.size());m++)if(distance(oldA[m],env.a.pos[m])>1e-10)throw std::runtime_error("rejected action moved overview");
        for(int m=0;m<int(oldB.size());m++)if(distance(oldB[m],env.b.pos[m])>1e-10)throw std::runtime_error("rejected action moved individual");
      }
      if(env.swapSlots&&r.accepted) {
        if(distance(env.a.pos[n],oldA[r.decodedTarget])>1e-8||distance(env.a.pos[r.decodedTarget],oldA[n])>1e-8)
          throw std::runtime_error("swap did not exchange exact input centers");
      }
      if(env.neighborMoves&&r.accepted)for(int m=0;m<int(oldA.size());m++) {
        const Point expected=m==n||m==r.decodedTarget?Point{oldA[m].x+env.a.pos[n].x-oldA[n].x,oldA[m].y+env.a.pos[n].y-oldA[n].y}:oldA[m];
        if(distance(expected,env.a.pos[m])>1e-8)throw std::runtime_error("neighbor action did not move exactly its rigid pair");
      }
      if(env.movingRayPorts&&r.accepted)for(int e=0;e<int(env.b.edges.size());e++) {
        const auto v=env.b.edges[e];
        if(env.owner[v.s]!=n&&distance(env.b.routes[e].a,oldRoutes[e].a)>1e-10)throw std::runtime_error("stationary source port moved");
        if(env.owner[v.t]!=n&&distance(env.b.routes[e].b,oldRoutes[e].b)>1e-10)throw std::runtime_error("stationary target port moved");
      }
      if(env.visual>oldVisual||(!env.overviewOnly&&env.individualVisual>oldIndividual)
        ||(env.overviewOnly&&!env.allowNeutral&&r.accepted&&env.visual>=oldVisual))throw std::runtime_error("objective admission mismatch");
      for(int m=0;m<int(env.components.size());m++)if(env.components[m].size()==1
        &&distance(env.a.pos[m],env.b.pos[env.components[m][0]])>1e-8)throw std::runtime_error("fractional center precision drift");
    }
    neutralAccepted+=env.acceptedNeutral;
    auto shifted=env;for(State* s:{&shifted.a,&shifted.b}){for(auto& p:s->pos){p.x+=10000;p.y+=20000;}for(auto& r:s->routes)r=segment({r.a.x+10000,r.a.y+20000},{r.b.x+10000,r.b.y+20000});}
    shifted.left+=10000;shifted.right+=10000;shifted.top+=20000;shifted.bottom+=20000;
    for(int n=0;n<8;n++){double x,y;auto f=env.features(n,x),h=shifted.features(n,y);for(size_t j=0;j<f.size();j++)if(std::abs(f[j]-h[j])>1e-8)
      throw std::runtime_error("translation invariance: graph="+std::to_string(g)+" node="+std::to_string(n)+" feature="+std::to_string(j)+" original="+std::to_string(f[j])+" shifted="+std::to_string(h[j]));}
  }
  if(!swapsAccepted)throw std::runtime_error("swap fixture did not exercise accepted swaps");
  if(!neighborsAccepted)throw std::runtime_error("neighbor fixture accepted no actions");
  if(!movingRayAccepted)throw std::runtime_error("moving-ray fixture accepted no actions");
  if(!neutralAccepted)throw std::runtime_error("neutral fixture accepted no neutral actions");
  std::cout<<"{\"selfTest\":\"pass\",\"globalDeltaComparisons\":"<<checked<<",\"acceptedSwaps\":"<<swapsAccepted<<",\"acceptedNeighborMoves\":"<<neighborsAccepted<<",\"acceptedMovingRayMoves\":"<<movingRayAccepted<<",\"acceptedNeutralMoves\":"<<neutralAccepted<<",\"translationInvariance\":true}\n";
}
void explorationSelfTest() {
  std::mt19937_64 rng(20261003);long checked=0,uphill=0,restored=0,improved=0;
  for(int g=0;g<12;g++) {
    auto env=synthetic(77100+g);env.overviewOnly=g%2;env.allowNeutral=true;
    env.movingRayPorts=g%3==1;env.attachAllPorts=g%3==2;env.explorationLimit=12;
    auto best=env;
    for(int step=0;step<256;step++) {
      env.admissionAllowance=step%7;
      const long before=env.visual,beforeIndividual=env.individualVisual;
      const auto result=env.trial(rng()%env.components.size(),{double(int(rng()%701)-350),double(int(rng()%701)-350)},true);
      const auto a=env.a.full(),b=env.b.full();checked++;
      if(a.visual()!=env.visual||b.visual()!=env.individualVisual||a.spacing||b.spacing||a.overlap||b.overlap
         ||policyHardScore(env.a)>env.initialHard||policyHardScore(env.b)>env.initialIndividualHard)
        throw std::runtime_error("exploration geometry audit");
      if(env.visual>env.initialVisual+12||(!env.overviewOnly&&env.individualVisual>env.initialIndividualVisual+12)
         ||env.visual>before+env.admissionAllowance||(!env.overviewOnly&&env.individualVisual>beforeIndividual+env.admissionAllowance))
        throw std::runtime_error("exploration ceiling violated");
      if(result.accepted&&result.gain<0)uphill++;
      if(improvesCheckpoint(env,best)){best=env;improved++;}
    }
    const auto attempts=env.attempts,accepted=env.accepted;
    restored+=best.attempts<attempts;
    restoreCheckpoint(env,best);
    if(env.attempts!=attempts||env.accepted!=accepted||env.savedActionPrefix!=best.attempts
       ||env.visual>env.initialVisual||(!env.overviewOnly&&env.individualVisual>env.initialIndividualVisual)
       ||env.a.full().visual()!=env.visual||env.b.full().visual()!=env.individualVisual)
      throw std::runtime_error("exploration checkpoint restore");
    for(size_t n=0;n<env.a.pos.size();n++)if(distance(env.a.pos[n],best.a.pos[n])>1e-10)throw std::runtime_error("checkpoint positions changed");
    for(size_t e=0;e<env.b.routes.size();e++)if(distance(env.b.routes[e].a,best.b.routes[e].a)>1e-10
      ||distance(env.b.routes[e].b,best.b.routes[e].b)>1e-10)throw std::runtime_error("checkpoint routes changed");
  }
  if(!uphill||!restored||!improved)throw std::runtime_error("exploration fixtures did not exercise uphill/checkpoint recovery");
  std::cout<<"{\"explorationSelfTest\":\"pass\",\"globalDeltaComparisons\":"<<checked
    <<",\"acceptedUphillMoves\":"<<uphill<<",\"restoredCheckpoints\":"<<restored<<",\"improvedCheckpoints\":"<<improved<<"}\n";
}
void branchSelfTest() {
  State fixture;fixture.nodes.resize(9);
  for(auto pair:std::vector<std::pair<int,int>>{{0,1},{1,2},{2,0},{0,3},{3,4},{1,5},{1,5},{7,8}})
    fixture.edges.push_back({"",pair.first,pair.second});
  const std::vector<std::vector<int>> expected{{0,3,4},{1,5},{2},{3},{4},{5},{6},{7},{8}};
  if(graphBranches(fixture)!=expected)throw std::runtime_error("graph branch fixture differs");
  State cutFixture;cutFixture.nodes.resize(9);
  for(auto pair:std::vector<std::pair<int,int>>{{0,1},{1,2},{2,0},{2,3},{3,4},{4,5},{5,3},{3,6},{7,8},{3,4},{5,5}})
    cutFixture.edges.push_back({"",pair.first,pair.second});
  const std::vector<std::vector<int>> cutExpected{{0},{1},{0,1,2},{3,4,5,6},{4},{5},{6},{7},{8}};
  if(graphBranches(cutFixture,"cut")!=cutExpected)throw std::runtime_error("cyclic cut fixture differs");
  State star;star.nodes.resize(4);for(int n=1;n<4;n++)star.edges.push_back({"",0,n});
  if(cutBranches(star)[0]!=std::vector<int>{0,2,3})throw std::runtime_error("cut tie fixture differs");
  long checked=0,accepted=0,groupedAccepted=0,cutChecked=0,cutGroupedAccepted=0;std::mt19937_64 rng(94901);
  for(int g=0;g<16;g++) {
    auto env=synthetic(81000+g);env.movingRayPorts=true;env.allowNeutral=true;env.overviewOnly=g%2;
    std::vector<std::vector<int>> rows(env.components.size());
    for(int n=0;n<int(rows.size());n++) {
      rows[n].push_back(n);
      if(n%3==0&&n+2<int(rows.size())){rows[n].push_back(n+1);rows[n].push_back(n+2);}
    }
    const std::string mode=g>=8?"cut":"core";
    if(mode=="cut")rows=graphBranches(env.a,mode);
    auto invalid=rows;invalid[0].push_back(0);bool rejected=false;
    try{env.setBranches(invalid);}catch(const std::runtime_error&){rejected=true;}
    if(!rejected||env.branchMoves)throw std::runtime_error("invalid branch map accepted");
    env.setBranches(rows,mode);
    for(int step=0;step<192;step++) {
      const int n=rng()%rows.size();const double radius=step%2?.02:400.;
      const Point delta{rounded(radius*(int(rng()%2001)-1000)/1000.),rounded(radius*(int(rng()%2001)-1000)/1000.)};
      const auto beforeA=env.a.pos,beforeB=env.b.pos;const auto beforeRoutes=env.b.routes;
      const auto c=env.context(n);if(c.physical!=rows[n])throw std::runtime_error("branch context differs from supplied map");
      env.trial(n,delta,false);
      for(size_t i=0;i<beforeA.size();i++)if(distance(beforeA[i],env.a.pos[i])>1e-12)throw std::runtime_error("branch measurement moved a card");
      for(size_t i=0;i<beforeB.size();i++)if(distance(beforeB[i],env.b.pos[i])>1e-12)throw std::runtime_error("branch measurement moved a member");
      for(size_t e=0;e<beforeRoutes.size();e++)if(distance(beforeRoutes[e].a,env.b.routes[e].a)>1e-12||distance(beforeRoutes[e].b,env.b.routes[e].b)>1e-12)
        throw std::runtime_error("branch measurement changed a route");
      const auto result=env.trial(n,delta,true);checked++;
      cutChecked+=mode=="cut";
      const auto a=env.a.full(),b=env.b.full();
      if(a.visual()!=env.visual||b.visual()!=env.individualVisual||a.spacing||b.spacing||a.overlap||b.overlap
         ||policyHardScore(env.a)>env.initialHard||policyHardScore(env.b)>env.initialIndividualHard)
        throw std::runtime_error("branch local/global score mismatch");
      accepted+=result.accepted;groupedAccepted+=result.accepted&&rows[n].size()>1&&(delta.x||delta.y);
      cutGroupedAccepted+=mode=="cut"&&result.accepted&&rows[n].size()>1&&(delta.x||delta.y);
      for(int i=0;i<int(beforeA.size());i++) {
        const bool moved=result.accepted&&std::binary_search(rows[n].begin(),rows[n].end(),i);
        if(distance(env.a.pos[i],{beforeA[i].x+(moved?delta.x:0),beforeA[i].y+(moved?delta.y:0)})>1e-10)
          throw std::runtime_error("branch physical translation mismatch");
      }
      for(int i=0;i<int(beforeB.size());i++) {
        const bool moved=result.accepted&&std::binary_search(c.nodes.begin(),c.nodes.end(),i);
        if(distance(env.b.pos[i],{beforeB[i].x+(moved?delta.x:0),beforeB[i].y+(moved?delta.y:0)})>1e-10)
          throw std::runtime_error("branch full-member translation mismatch");
      }
    }
  }
  if(!groupedAccepted)throw std::runtime_error("branch fixtures accepted no grouped moves");
  if(!cutGroupedAccepted)throw std::runtime_error("cut fixtures accepted no grouped moves");
  std::cout<<"{\"branchSelfTest\":\"pass\",\"globalDeltaComparisons\":"<<checked<<",\"acceptedMoves\":"<<accepted
    <<",\"acceptedGroupedMoves\":"<<groupedAccepted<<",\"cutModeComparisons\":"<<cutChecked
    <<",\"acceptedCutGroupedMoves\":"<<cutGroupedAccepted<<",\"measurementsPreserveState\":true}"<<std::endl;
}
}
#ifndef ERD_COMPONENT_HELPERS_ONLY
int main(int argc,char**argv) {
  try {
    if(argc==2&&std::string(argv[1])=="--self-test"){componentSelfTest();explorationSelfTest();branchSelfTest();return 0;}
    if(argc==2&&std::string(argv[1])=="--branch-self-test"){branchSelfTest();return 0;}
    if(arg(argc,argv,"--dataset","none")!="none"){dataset(argc,argv);return 0;}
    if(arg(argc,argv,"--adapt-dataset","none")!="none"){adaptationDataset(argc,argv);return 0;}
    if(arg(argc,argv,"--canonical-audit","none")!="none") {
      auto env=load(arg(argc,argv,"--canonical-audit"));std::cout<<"{\"sourceVisual\":"<<env.visual<<",\"sourceIndividualVisual\":"<<env.individualVisual;
      env.canonicalize();std::cout<<",\"canonicalVisual\":"<<env.visual<<",\"canonicalIndividualVisual\":"<<env.individualVisual
        <<",\"canonicalHard\":"<<env.initialHard<<",\"canonicalIndividualHard\":"<<env.initialIndividualHard<<"}\n";return 0;
    }
    const auto seed=arg(argc,argv,"--synthetic-seed","none"),output=arg(argc,argv,"--out");
    auto env=seed=="none"?load(arg(argc,argv,"--directory")):synthetic(std::stoull(seed));
    env.attachedRepresentatives=arg(argc,argv,"--decoder","ray")=="attached";
    env.attachAllPorts=arg(argc,argv,"--decoder","ray")=="attached-all";
    env.movingRayPorts=arg(argc,argv,"--decoder","ray")=="moving-ray";
    env.jointMoves=arg(argc,argv,"--joint","0")=="1";
    env.neighborMoves=arg(argc,argv,"--neighbors","0")=="1";
    env.swapSlots=arg(argc,argv,"--decoder","ray")=="swap";
    env.overviewOnly=arg(argc,argv,"--overview-only","0")=="1";
    env.allowNeutral=arg(argc,argv,"--allow-neutral","0")=="1";
    if(arg(argc,argv,"--branches","0")=="1") {
      if(seed!="none")throw std::runtime_error("branch maps require an exported source directory");
      loadBranches(env,arg(argc,argv,"--directory"),arg(argc,argv,"--branch-mode","core"));
    }
    env.explorationLimit=std::stol(arg(argc,argv,"--exploration-limit","0"));
    if(env.explorationLimit<0||env.explorationLimit>200)throw std::runtime_error("exploration limit must be between 0 and 200");
    if(arg(argc,argv,"--canonical-start","0")=="1")env.canonicalize();
    std::unique_ptr<ComponentEnvironment> best;
    if(env.explorationLimit)best=std::make_unique<ComponentEnvironment>(env);
    std::cout<<std::setprecision(10)<<"{\"ready\":true,\"eligible\":"<<env.components.size()<<",\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual
      <<",\"canonicalStart\":"<<(env.canonicalStart?"true":"false")<<",\"sourceVisual\":"<<(env.canonicalStart?env.sourceVisual:env.visual)
      <<",\"sourceIndividualVisual\":"<<(env.canonicalStart?env.sourceIndividualVisual:env.individualVisual)<<"}"<<std::endl;
    std::string line;while(std::getline(std::cin,line)) {
      std::istringstream command(line);std::string op;command>>op;
      if(op=="OBS") {
        std::vector<long> ap,bp;env.a.full(&ap);env.b.full(&bp);std::vector<int> order(env.components.size());std::iota(order.begin(),order.end(),0);
        std::vector<long> pressure=ap;
        if(!env.overviewOnly)for(int n=0;n<int(order.size());n++)for(int m:env.components[n])pressure[n]+=bp[m];
        std::stable_sort(order.begin(),order.end(),[&](int n,int m){return pressure[n]>pressure[m];});
        int limit=192;command>>limit;limit=std::clamp(limit,1,1100);
        std::cout<<"{\"nodes\":[";bool first=true;int emitted=0;
        for(int n:order)if(pressure[n]&&emitted++<limit) {double scale;auto f=env.features(n,scale);if(!first)std::cout<<',';first=false;
          std::cout<<"{\"id\":"<<n<<",\"actionScale\":"<<scale<<",\"features\":";array(std::cout,f);std::cout<<'}';}
        std::cout<<"],\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<"}"<<std::endl;
      }else if(op=="TRY") {
        int n;Point delta;if(!(command>>n>>delta.x>>delta.y))throw std::runtime_error("invalid action");
        if(env.explorationLimit&&(!(command>>env.admissionAllowance)||env.admissionAllowance<0||env.admissionAllowance>env.explorationLimit))
          throw std::runtime_error("invalid exploratory admission allowance");
        auto r=env.trial(n,delta,true);
        if(best&&improvesCheckpoint(env,*best))*best=env;
        std::cout<<"{\"accepted\":"<<(r.accepted?"true":"false")<<",\"legal\":"<<(r.legal?"true":"false")<<",\"gain\":"<<r.gain<<",\"individualGain\":"<<r.individualGain
          <<",\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<",\"reason\":\""<<r.reason<<"\",\"decodedTarget\":"<<r.decodedTarget<<"}"<<std::endl;
      }else if(op=="SAVE"){if(best)restoreCheckpoint(env,*best);env.save(output);std::cout<<"{\"saved\":true}"<<std::endl;}
      else if(op=="QUIT")return 0;else throw std::runtime_error("unknown command");
    }
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#endif
