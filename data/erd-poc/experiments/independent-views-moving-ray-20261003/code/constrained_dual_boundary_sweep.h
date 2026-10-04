#pragma once
// Exact one-endpoint interval search with non-regression in both presentations.
// Reuse the established roots, contacts and decimal grid; combine the event
// streams before choosing a point so an arbitrary overview midpoint cannot
// conceal a feasible individual-view alternative.
#include "constrained_boundary_sweep.h"

namespace {
struct DualBoundaryCost {
  BoundaryCost overview, individual;
};
bool dualEqual(DualBoundaryCost a, DualBoundaryCost b) {
  return boundaryEqual(a.overview,b.overview)&&boundaryEqual(a.individual,b.individual);
}
bool dualPermitted(DualBoundaryCost a, DualBoundaryCost ceiling) {
  return a.overview.hard<=ceiling.overview.hard&&a.individual.hard<=ceiling.individual.hard
    &&a.overview.visual()<=ceiling.overview.visual()&&a.individual.visual()<=ceiling.individual.visual();
}
bool dualBetter(DualBoundaryCost a, DualBoundaryCost b) {
  if(a.overview.hard!=b.overview.hard)return a.overview.hard<b.overview.hard;
  if(a.overview.visual()!=b.overview.visual())return a.overview.visual()<b.overview.visual();
  if(a.individual.hard!=b.individual.hard)return a.individual.hard<b.individual.hard;
  if(a.individual.visual()!=b.individual.visual())return a.individual.visual()<b.individual.visual();
  if(a.overview.hit!=b.overview.hit)return a.overview.hit<b.overview.hit;
  return a.individual.hit<b.individual.hit;
}
BoundaryCost dualLocalCost(const State& s,int e,const Segment& line) {
  auto cost=boundaryEdgeCost(s,e,line);const auto edge=s.edges[e];
  // A pair contact contributes to both edges in the global hard score; an
  // endpoint entering its own card contributes once. Match that global delta.
  const long own=hits(line,s.pos[edge.s],s.nodes[edge.s],-.02)
    +hits(line,s.pos[edge.t],s.nodes[edge.t],-.02);
  cost.hard=2*cost.hard-own;return cost;
}
DualBoundaryCost dualEdgeCost(const State& overview,int e,const State& individual,int f,const Segment& line) {
  return {dualLocalCost(overview,e,line),dualLocalCost(individual,f,line)};
}
void appendBoundaryFactors(const State& s,int e,const BoundaryFace& face,Point q,int end,
                           std::vector<BoundaryEvent>& events) {
  const auto edge=s.edges[e];
  const auto route=[&](int k){const Point p=face.at(k);return end?segment(q,p):segment(p,q);};
  for(int f=0;f<int(s.edges.size());f++)if(f!=e) {
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
    for(const Point p:{line.a,line.b})for(double tol:{-.001,-1e-6,0.,1e-6,.001}) {
      boundaryLinearRoots(cuts,face.start.x-p.x-tol,face.step.x,face.last);
      boundaryLinearRoots(cuts,face.start.y-p.y-tol,face.step.y,face.last);
    }
    boundaryFactor(events,cuts,face.last,[&](int k){const auto candidate=route(k);
      const bool crossing=crosses(candidate,line);
      return BoundaryCost{crossing,0,2*(long(adjacent&&crossing)+boundaryContact(s,e,candidate,f,line))};});
  }
  for(int n=0;n<int(s.nodes.size());n++) {
    std::vector<int> cuts;const bool isOwn=n==edge.s||n==edge.t;const double padding=isOwn?-.02:10;
    const double l=s.pos[n].x-s.nodes[n].w/2-padding,r=s.pos[n].x+s.nodes[n].w/2+padding;
    const double t=s.pos[n].y-s.nodes[n].h/2-padding,b=s.pos[n].y+s.nodes[n].h/2+padding;
    for(Point corner:std::array<Point,4>{{{l,t},{l,b},{r,t},{r,b}}}) {
      const auto [a,c]=boundaryOrientation(face,q,corner);boundaryLinearRoots(cuts,a,c,face.last);
    }
    for(double x:{l,r})boundaryLinearRoots(cuts,face.start.x-x,face.step.x,face.last);
    for(double y:{t,b})boundaryLinearRoots(cuts,face.start.y-y,face.step.y,face.last);
    boundaryFactor(events,cuts,face.last,[&](int k){const bool hit=hits(route(k),s.pos[n],s.nodes[n],padding);
      return BoundaryCost{0,long(hit&&!isOwn),long(hit&&isOwn)};});
  }
}
struct DualBoundaryResult {
  Segment route;DualBoundaryCost cost;long evaluatedIntervals=0;
};
DualBoundaryResult sweepDualBoundaryEndpoint(const State& overview,int e,const State& individual,int f,int end,
                                             Segment baseline,DualBoundaryCost ceiling) {
  DualBoundaryResult best{baseline,dualEdgeCost(overview,e,individual,f,baseline),0};
  const auto edge=overview.edges[e];const int own=end?edge.t:edge.s;
  const Point q=end?overview.routes[e].a:overview.routes[e].b;
  for(const auto& face:boundaryFaces(overview,own)) {
    std::vector<BoundaryEvent> a,b;
    appendBoundaryFactors(overview,e,face,q,end,a);
    appendBoundaryFactors(individual,f,face,q,end,b);
    const auto order=[](const auto& x,const auto& y){return x.at<y.at;};
    std::sort(a.begin(),a.end(),order);std::sort(b.begin(),b.end(),order);
    DualBoundaryCost cost{};size_t i=0,j=0;int at=0;
    while(at<=face.last) {
      while(i<a.size()&&a[i].at==at)cost.overview=cost.overview+a[i++].delta;
      while(j<b.size()&&b[j].at==at)cost.individual=cost.individual+b[j++].delta;
      const int next=std::min(i<a.size()?a[i].at:face.last+1,j<b.size()?b[j].at:face.last+1);
      if(next<=at)throw std::runtime_error("unordered dual boundary events");
      best.evaluatedIntervals++;
      if(dualPermitted(cost,ceiling)&&dualBetter(cost,best.cost)) {
        const Point p=face.at(at+(std::min(next-1,face.last)-at)/2);
        const auto candidate=end?segment(q,p):segment(p,q);
        const auto actual=dualEdgeCost(overview,e,individual,f,candidate);
        if(!dualEqual(cost,actual))throw std::runtime_error("dual boundary prediction disagrees with both edge scores");
        best.route=candidate;best.cost=actual;
      }
      at=next;
    }
  }
  return best;
}
}
