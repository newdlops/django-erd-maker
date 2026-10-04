// Search every constant-cost interval of a rectangular boundary on the saved
// 0.01 coordinate grid. The other endpoint and all other routes stay fixed.
// This is a local straight-port proposal, not a global layout optimum.
#pragma once
#include <array>

namespace {
struct BoundaryCost {
  long cross=0,hit=0,hard=0;
  long visual()const{return cross+hit;}
};
BoundaryCost operator+(BoundaryCost a,BoundaryCost b){return {a.cross+b.cross,a.hit+b.hit,a.hard+b.hard};}
BoundaryCost operator-(BoundaryCost a,BoundaryCost b){return {a.cross-b.cross,a.hit-b.hit,a.hard-b.hard};}
bool boundaryBetter(BoundaryCost a,BoundaryCost b){
  if(a.hard!=b.hard)return a.hard<b.hard;
  if(a.visual()!=b.visual())return a.visual()<b.visual();
  return a.hit<b.hit;
}
bool boundaryEqual(BoundaryCost a,BoundaryCost b){return a.cross==b.cross&&a.hit==b.hit&&a.hard==b.hard;}
bool boundarySame(Point a,Point b){return std::abs(a.x-b.x)<.001&&std::abs(a.y-b.y)<.001;}
bool boundaryOn(Point p,const Segment& l){return std::abs(orient(l.a,l.b,p))<1e-5
  &&p.x>=l.l-1e-6&&p.x<=l.r+1e-6&&p.y>=l.t-1e-6&&p.y<=l.bottom+1e-6;}
bool boundaryContact(const State& s,int e,const Segment& a,int f,const Segment& b){
  if(a.l>b.r+1e-6||b.l>a.r+1e-6||a.t>b.bottom+1e-6||b.t>a.bottom+1e-6)return false;
  const auto u=s.edges[e],v=s.edges[f];
  const auto allowed=[&](Point p){return
    (u.s==v.s&&boundarySame(a.a,b.a)&&boundarySame(p,a.a))||
    (u.s==v.t&&boundarySame(a.a,b.b)&&boundarySame(p,a.a))||
    (u.t==v.s&&boundarySame(a.b,b.a)&&boundarySame(p,a.b))||
    (u.t==v.t&&boundarySame(a.b,b.b)&&boundarySame(p,a.b));};
  return (!allowed(a.a)&&boundaryOn(a.a,b))||(!allowed(a.b)&&boundaryOn(a.b,b))||
    (!allowed(b.a)&&boundaryOn(b.a,a))||(!allowed(b.b)&&boundaryOn(b.b,a));
}
BoundaryCost boundaryEdgeCost(const State& s,int e,const Segment& line){
  BoundaryCost c;const auto edge=s.edges[e];
  for(int f=0;f<int(s.edges.size());f++)if(e!=f){
    const auto other=s.edges[f];const bool adjacent=edge.s==other.s||edge.s==other.t||edge.t==other.s||edge.t==other.t;
    const bool crossing=crosses(line,s.routes[f]);c.cross+=crossing;
    c.hard+=crossing&&adjacent;c.hard+=boundaryContact(s,e,line,f,s.routes[f]);
  }
  for(int n=0;n<int(s.nodes.size());n++){
    const bool own=n==edge.s||n==edge.t;
    const bool hit=hits(line,s.pos[n],s.nodes[n],own?-.02:10);
    if(own)c.hard+=hit;else c.hit+=hit;
  }return c;
}
struct BoundaryEvent {int at;BoundaryCost delta;};
struct BoundarySweepResult {Segment route;BoundaryCost cost;long evaluatedIntervals=0;};
struct BoundaryFace {
  Point start,step;
  int last=0;
  Point at(int k)const{return {rounded(start.x+step.x*k),rounded(start.y+step.y*k)};}
};
std::array<BoundaryFace,4> boundaryFaces(const State&s,int n){
  const double l=rounded(s.pos[n].x-s.nodes[n].w/2),r=rounded(s.pos[n].x+s.nodes[n].w/2);
  const double t=rounded(s.pos[n].y-s.nodes[n].h/2),b=rounded(s.pos[n].y+s.nodes[n].h/2);
  const int w=int(std::llround((r-l)*100)),h=int(std::llround((b-t)*100));
  return {{{{l,t},{0,.01},h},{{r,t},{0,.01},h},{{l,t},{.01,0},w},{{l,b},{.01,0},w}}};
}
// Add a small integer neighborhood around every analytic event. Testing the
// grid points immediately around a root also covers decimal output rounding.
void boundaryRoot(std::vector<int>& cuts,long double value,int last){
  if(!std::isfinite(value)||value < -4 ||value>last+4)return;
  const int at=int(std::floor(value));
  for(int d=-2;d<=3;d++)if(at+d>0&&at+d<=last)cuts.push_back(at+d);
}
void boundaryLinearRoots(std::vector<int>&cuts,long double a,long double b,int last){
  if(b!=0)boundaryRoot(cuts,-a/b,last);
}
void boundaryQuadraticRoots(std::vector<int>&cuts,long double a,long double b,long double c,int last){
  if(a==0){boundaryLinearRoots(cuts,c,b,last);return;}
  const long double disc=b*b-4*a*c;if(disc<0)return;
  const long double q=-.5L*(b+std::copysign(std::sqrt(disc),b));
  if(q==0){boundaryRoot(cuts,-b/(2*a),last);return;}
  boundaryRoot(cuts,q/a,last);boundaryRoot(cuts,c/q,last);
}
std::pair<long double,long double> boundaryOrientation(const BoundaryFace& face,Point q,Point r){
  const Point p=face.start,d=face.step;
  const long double a=(static_cast<long double>(q.x)-p.x)*(static_cast<long double>(r.y)-p.y)
    -(static_cast<long double>(q.y)-p.y)*(static_cast<long double>(r.x)-p.x);
  const long double b=static_cast<long double>(d.x)*(q.y-r.y)-static_cast<long double>(d.y)*(q.x-r.x);
  return {a,b};
}
template<class Eval>
void boundaryFactor(std::vector<BoundaryEvent>&events,std::vector<int>&cuts,int last,Eval eval){
  cuts.push_back(0);cuts.push_back(last+1);
  std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
  BoundaryCost previous{};
  for(size_t i=0;i+1<cuts.size();i++){
    const int lo=cuts[i],hi=cuts[i+1]-1;
    const auto c=eval(lo+(hi-lo)/2);
    // A mismatch means an event was omitted. Refuse an inaccurate score.
    if(!boundaryEqual(c,eval(lo))||!boundaryEqual(c,eval(hi)))throw std::runtime_error("boundary factor interval is not constant");
    if(!boundaryEqual(c,previous))events.push_back({lo,c-previous});
    previous=c;
  }
  if(!boundaryEqual(previous,{}))events.push_back({last+1,BoundaryCost{}-previous});
}
BoundarySweepResult sweepBoundaryEndpoint(const State&s,int e,int end){
  const auto current=s.routes[e];
  BoundarySweepResult best{current,boundaryEdgeCost(s,e,current),0};
  const auto edge=s.edges[e];const int own=end?edge.t:edge.s;
  const Point q=end?current.a:current.b;
  for(const auto&face:boundaryFaces(s,own)){
    std::vector<BoundaryEvent> events;
    const auto route=[&](int k){const Point p=face.at(k);return end?segment(q,p):segment(p,q);};
    for(int f=0;f<int(s.edges.size());f++)if(f!=e){
      std::vector<int> cuts;const auto line=s.routes[f];const auto other=s.edges[f];
      const bool adjacent=edge.s==other.s||edge.s==other.t||edge.t==other.s||edge.t==other.t;
      const auto [a,b]=boundaryOrientation(face,q,line.a);
      const auto [c,d]=boundaryOrientation(face,q,line.b);
      const long double g=orient(line.a,line.b,face.start);
      const long double h=(static_cast<long double>(line.b.x)-line.a.x)*face.step.y
        -(static_cast<long double>(line.b.y)-line.a.y)*face.step.x;
      const long double fixed=orient(line.a,line.b,q);
      boundaryQuadraticRoots(cuts,b*d,a*d+b*c,a*c+1e-9L,face.last);
      boundaryLinearRoots(cuts,g*fixed+1e-9L,h*fixed,face.last);
      for(const auto& terms:std::array<std::pair<long double,long double>,3>{{{a,b},{c,d},{g,h}}})
        for(long double tol:{-1e-5L,0.L,1e-5L})boundaryLinearRoots(cuts,terms.first-tol,terms.second,face.last);
      for(const Point p:{line.a,line.b})for(double tol:{-.001,-1e-6,0.,1e-6,.001}){
        boundaryLinearRoots(cuts,face.start.x-p.x-tol,face.step.x,face.last);
        boundaryLinearRoots(cuts,face.start.y-p.y-tol,face.step.y,face.last);
      }
      boundaryFactor(events,cuts,face.last,[&](int k){const auto candidate=route(k);
        const bool crossing=crosses(candidate,line);
        return BoundaryCost{crossing,0,long(adjacent&&crossing)+boundaryContact(s,e,candidate,f,line)};});
    }
    for(int n=0;n<int(s.nodes.size());n++){
      std::vector<int> cuts;const bool isOwn=n==edge.s||n==edge.t;const double padding=isOwn?-.02:10;
      const double l=s.pos[n].x-s.nodes[n].w/2-padding,r=s.pos[n].x+s.nodes[n].w/2+padding;
      const double t=s.pos[n].y-s.nodes[n].h/2-padding,b=s.pos[n].y+s.nodes[n].h/2+padding;
      for(Point corner:std::array<Point,4>{{{l,t},{l,b},{r,t},{r,b}}}){
        const auto [a,c]=boundaryOrientation(face,q,corner);boundaryLinearRoots(cuts,a,c,face.last);
      }
      for(double x:{l,r})boundaryLinearRoots(cuts,face.start.x-x,face.step.x,face.last);
      for(double y:{t,b})boundaryLinearRoots(cuts,face.start.y-y,face.step.y,face.last);
      boundaryFactor(events,cuts,face.last,[&](int k){const bool hit=hits(route(k),s.pos[n],s.nodes[n],padding);
        return BoundaryCost{0,long(hit&&!isOwn),long(hit&&isOwn)};});
    }
    std::sort(events.begin(),events.end(),[](const auto&a,const auto&b){return a.at<b.at;});
    BoundaryCost cost{};size_t i=0;int at=0;
    while(at<=face.last){
      while(i<events.size()&&events[i].at==at)cost=cost+events[i++].delta;
      const int next=i<events.size()?events[i].at:face.last+1;
      if(next<=at)throw std::runtime_error("unordered boundary events");
      best.evaluatedIntervals++;
      if(boundaryBetter(cost,best.cost)){
        const int k=at+(std::min(next-1,face.last)-at)/2;
        const auto candidate=route(k);
        const auto actual=boundaryEdgeCost(s,e,candidate);
        if(!boundaryEqual(cost,actual))throw std::runtime_error("boundary sweep prediction disagrees with full edge score");
        best.route=candidate;best.cost=actual;
      }
      at=next;
    }
  }return best;
}
}
