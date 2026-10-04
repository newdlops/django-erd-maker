#pragma once
// Constrained research optimizer for a complete, straight-line model-card scene.
// The canvas is fixed before search. Every input node and relationship remains.
// Delta scores include adjacent-edge crossings and non-incident card hits.
// Routes are explicit center-ray boundary ports, independent of obstacles;
// this makes node/group deltas exact and lets the production renderer verify
// the resulting two-point routes without running a different routing heuristic.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
struct Point { double x=0, y=0; };
struct Node { std::string id; double w=0,h=0; };
struct Edge { std::string id; int s=0,t=0; };
struct Segment { Point a,b; double l,r,t,bottom; };
struct Score {
  long cross=0, hit=0, overlap=0, spacing=0;
  long visual() const { return cross+hit+overlap; }
  double cost(double hitWeight) const { return cross+hit*hitWeight+overlap*10000.+spacing*1000.; }
};
Score operator+(Score a,Score b) {return {a.cross+b.cross,a.hit+b.hit,a.overlap+b.overlap,a.spacing+b.spacing};}
Score operator-(Score a,Score b) {return {a.cross-b.cross,a.hit-b.hit,a.overlap-b.overlap,a.spacing-b.spacing};}
bool equal(Score a,Score b) {return a.cross==b.cross&&a.hit==b.hit&&a.overlap==b.overlap&&a.spacing==b.spacing;}
std::vector<std::string> split(const std::string& line) {
  std::vector<std::string> f; size_t p=0;
  for (;;) {auto q=line.find('\t',p);f.push_back(line.substr(p,q-p));if(q==std::string::npos)return f;p=q+1;}
}
std::string arg(int argc,char**argv,const std::string& key,const std::string& fallback="") {
  for(int i=1;i+1<argc;i++)if(argv[i]==key)return argv[i+1];
  if(!fallback.empty())return fallback;throw std::runtime_error("missing "+key);
}
double number(int argc,char**argv,const std::string& key,double fallback) {return std::stod(arg(argc,argv,key,std::to_string(fallback)));}
double orient(Point a,Point b,Point c) {
  // Match the renderer's separately rounded JavaScript products. On ARM a
  // fused multiply/subtract can turn a shared endpoint into a false crossing.
#if defined(__clang__)
#pragma clang fp contract(off)
  return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
#else
  volatile double first=(b.x-a.x)*(c.y-a.y),second=(b.y-a.y)*(c.x-a.x);
  return first-second;
#endif
}
Segment segment(Point a,Point b) {return {a,b,std::min(a.x,b.x),std::max(a.x,b.x),std::min(a.y,b.y),std::max(a.y,b.y)};}
bool crosses(const Segment&a,const Segment&b) {
  if(a.l>b.r||b.l>a.r||a.t>b.bottom||b.t>a.bottom)return false;
  return orient(a.a,a.b,b.a)*orient(a.a,a.b,b.b)<-1e-9&&orient(b.a,b.b,a.a)*orient(b.a,b.b,a.b)<-1e-9;
}
double rounded(double x) {return std::round(x*100.)/100.;}
Point port(Point a,const Node&n,Point b) {
  double dx=b.x-a.x,dy=b.y-a.y;
  if(std::abs(dx)<1e-9&&std::abs(dy)<1e-9)dx=1.;
  double tx=std::abs(dx)<1e-12?1e100:n.w*.5/std::abs(dx);
  double ty=std::abs(dy)<1e-12?1e100:n.h*.5/std::abs(dy);
  double f=std::min(tx,ty);
  return {rounded(a.x+dx*f),rounded(a.y+dy*f)};
}
bool hits(const Segment&s,Point p,const Node&n,double padding=10) {
  double l=p.x-n.w*.5-padding,r=p.x+n.w*.5+padding,t=p.y-n.h*.5-padding,b=p.y+n.h*.5+padding;
  if(s.r<=l||s.l>=r||s.bottom<=t||s.t>=b)return false;
  double lo=0.,hi=1.,dx=s.b.x-s.a.x,dy=s.b.y-s.a.y;
  const auto clip=[&](double origin,double direction,double low,double high) {
    if(std::abs(direction)<1e-9)return origin>low&&origin<high;
    double a=(low-origin)/direction,b=(high-origin)/direction;
    if(a>b)std::swap(a,b);lo=std::max(lo,a);hi=std::min(hi,b);return hi-lo>1e-9;
  };
  return clip(s.a.x,dx,l,r)&&clip(s.a.y,dy,t,b)&&hi>1e-9&&lo<1-1e-9;
}
struct State {
  std::vector<Node> nodes;
  std::vector<Edge> edges;
  std::vector<Point> pos;
  std::vector<Segment> routes;
  std::vector<Point> sourceOffsets,targetOffsets;
  std::vector<std::vector<int>> incident,adj,groups;
  std::vector<std::vector<std::pair<int,double>>> elasticGroups;
  std::vector<char> moved,changedEdge;
  double width=31600,height=31600;
  Segment route(int e) const {
    const auto&v=edges[e];
    if(!sourceOffsets.empty())return segment({rounded(pos[v.s].x+sourceOffsets[e].x),rounded(pos[v.s].y+sourceOffsets[e].y)},
      {rounded(pos[v.t].x+targetOffsets[e].x),rounded(pos[v.t].y+targetOffsets[e].y)});
    return segment(port(pos[v.s],nodes[v.s],pos[v.t]),port(pos[v.t],nodes[v.t],pos[v.s]));
  }
  bool inside(int n,Point p) const {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=nodes[n].w*.5+1&&p.y>=nodes[n].h*.5+1&&p.x<=width-nodes[n].w*.5-1&&p.y<=height-nodes[n].h*.5-1;
  }
  Score pair(int a,int b) const {
    const double dx=std::abs(pos[a].x-pos[b].x)-(nodes[a].w+nodes[b].w)*.5;
    const double dy=std::abs(pos[a].y-pos[b].y)-(nodes[a].h+nodes[b].h)*.5;
    return {0,0,dx<0&&dy<0,dx<55.99&&dy<41.99};
  }
  Score full(std::vector<long>* pressure=nullptr) const {
    Score r;
    if(pressure)pressure->assign(nodes.size(),0);
    for(int a=0;a<int(nodes.size());a++)for(int b=a+1;b<int(nodes.size());b++)r=r+pair(a,b);
    for(int a=0;a<int(edges.size());a++) {
      for(int b=a+1;b<int(edges.size());b++)if(crosses(routes[a],routes[b])){
        r.cross++;
        if(pressure){(*pressure)[edges[a].s]++;(*pressure)[edges[a].t]++;(*pressure)[edges[b].s]++;(*pressure)[edges[b].t]++;}
      }
      for(int n=0;n<int(nodes.size());n++)if(n!=edges[a].s&&n!=edges[a].t&&hits(routes[a],pos[n],nodes[n])){
        r.hit++;
        if(pressure){(*pressure)[n]+=3;(*pressure)[edges[a].s]++;(*pressure)[edges[a].t]++;}
      }
    }return r;
  }
  Score local(const std::vector<int>&ns,const std::vector<int>&es) const {
    Score r;
    for(int a:ns)for(int b=0;b<int(nodes.size());b++)if(a!=b&&(!moved[b]||a<b))r=r+pair(a,b);
    for(int a:es) {
      for(int b=0;b<int(edges.size());b++)if(a!=b&&(!changedEdge[b]||a<b))r.cross+=crosses(routes[a],routes[b]);
      for(int n=0;n<int(nodes.size());n++)if(n!=edges[a].s&&n!=edges[a].t)r.hit+=hits(routes[a],pos[n],nodes[n]);
    }
    for(int a=0;a<int(edges.size());a++)if(!changedEdge[a])for(int n:ns)if(n!=edges[a].s&&n!=edges[a].t)r.hit+=hits(routes[a],pos[n],nodes[n]);
    return r;
  }
  void update(const std::vector<int>&es) {for(int e:es)routes[e]=route(e);}
  void allRoutes() {routes.resize(edges.size());for(int e=0;e<int(edges.size());e++)routes[e]=route(e);}
  void save(const std::string&file) const {
    std::ofstream out(file);if(!out)throw std::runtime_error("cannot write "+file);
    out<<std::fixed<<std::setprecision(9);
    for(int n=0;n<int(nodes.size());n++)out<<nodes[n].id<<'\t'<<pos[n].x<<'\t'<<pos[n].y<<'\n';
    std::ofstream rt(file+".routes.tsv");rt<<std::fixed<<std::setprecision(2);
    for(int e=0;e<int(edges.size());e++)rt<<edges[e].id<<'\t'<<routes[e].a.x<<','<<routes[e].a.y<<' '<<routes[e].b.x<<','<<routes[e].b.y<<'\n';
  }
};
bool better(Score a,Score b) {
  if((a.overlap+a.spacing==0)!=(b.overlap+b.spacing==0))return a.overlap+a.spacing==0;
  if(a.overlap!=b.overlap)return a.overlap<b.overlap;
  if(a.spacing!=b.spacing)return a.spacing<b.spacing;
  if(a.visual()!=b.visual())return a.visual()<b.visual();
  return a.hit<b.hit;
}
void report(const std::string&stage,long step,Score score,double seconds) {
  std::cerr<<stage<<" step="<<step<<" visual="<<score.visual()<<" cross="<<score.cross<<" hit="<<score.hit<<" overlap="<<score.overlap<<" spacing="<<score.spacing<<" seconds="<<seconds<<'\n';
}
void initialize(State&s,int fit,const std::function<bool()>& expired={}) {
  if(fit) {
    double l=1e100,r=-1e100,t=1e100,b=-1e100,maxW=0,maxH=0;
    for(int n=0;n<int(s.nodes.size());n++){l=std::min(l,s.pos[n].x);r=std::max(r,s.pos[n].x);t=std::min(t,s.pos[n].y);b=std::max(b,s.pos[n].y);maxW=std::max(maxW,s.nodes[n].w);maxH=std::max(maxH,s.nodes[n].h);}
    double fx=(s.width-maxW-300)/std::max(1.,r-l),fy=(s.height-maxH-300)/std::max(1.,b-t);
    if(fit!=2)fx=fy=std::min(fx,fy);
    for(auto&p:s.pos){p.x=s.width*.5+(p.x-(l+r)*.5)*fx;p.y=s.height*.5+(p.y-(t+b)*.5)*fy;}
  }
  std::vector<int> order(s.nodes.size());std::iota(order.begin(),order.end(),0);
  std::stable_sort(order.begin(),order.end(),[&](int a,int b){return s.adj[a].size()>s.adj[b].size();});
  std::vector<int> placed;
  for(int n:order) {
    if(expired&&expired())throw std::runtime_error("initialization deadline");
    Point wanted=s.pos[n];
    const auto free=[&](Point p){if(!s.inside(n,p))return false;s.pos[n]=p;for(int other:placed)if(s.pair(n,other).spacing)return false;return true;};
    bool found=free(wanted);
    for(int radius=1;!found&&radius<250;radius++) {
      if(expired&&expired())throw std::runtime_error("initialization deadline");
      const double r=radius*45.;
      const int samples=std::max(16,radius*8);
      for(int a=0;!found&&a<samples;a++){double angle=a*6.283185307179586/samples;Point p{wanted.x+r*std::cos(angle),wanted.y+r*std::sin(angle)};if(free(p))found=true;}
    }
    if(!found)throw std::runtime_error("cannot place "+s.nodes[n].id+" inside canvas");
    placed.push_back(n);
  }
  s.allRoutes();
}
}
