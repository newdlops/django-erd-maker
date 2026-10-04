#pragma once
// Shared geometry and delta tests only; no proposal/search entrypoint.
// Bounded translation of ordinary cards with attached straight boundary ports.
// Both rendered presentations must improve or remain equal at every step.
#include "constrained_scene.h"
#include "constrained_boundary_sweep.h"
#include <set>
#include <sstream>

namespace {
State loadView(const std::string& directory,const std::string& prefix) {
  State s;std::unordered_map<std::string,int> nodes,edges;std::string line;
  std::ifstream ns(directory+"/"+prefix+"nodes.tsv");if(!ns)throw std::runtime_error("missing nodes");
  while(std::getline(ns,line)) {
    const auto v=split(line);if(v.size()!=3||nodes.count(v[0]))throw std::runtime_error("invalid node");
    nodes[v[0]]=s.nodes.size();s.nodes.push_back({v[0],std::stod(v[1]),std::stod(v[2])});
  }
  s.pos.resize(s.nodes.size());s.incident.resize(s.nodes.size());s.adj.resize(s.nodes.size());s.moved.resize(s.nodes.size());
  std::set<int> present;std::ifstream ps(directory+"/"+prefix+"positions.tsv");
  if(!ps)throw std::runtime_error("missing positions");
  while(std::getline(ps,line)) {
    const auto v=split(line);if(v.size()!=3||!nodes.count(v[0])||!present.insert(nodes.at(v[0])).second)
      throw std::runtime_error("invalid position");
    s.pos[nodes.at(v[0])]={std::stod(v[1]),std::stod(v[2])};
  }
  if(present.size()!=s.nodes.size()||s.nodes.empty())throw std::runtime_error("missing position");
  std::ifstream es(directory+"/"+prefix+"edges.tsv");if(!es)throw std::runtime_error("missing edges");
  while(std::getline(es,line)) {
    const auto v=split(line);if(v.size()!=3||edges.count(v[0])||!nodes.count(v[1])||!nodes.count(v[2]))
      throw std::runtime_error("invalid edge");
    const int a=nodes.at(v[1]),b=nodes.at(v[2]),e=s.edges.size();if(a==b)throw std::runtime_error("self edge");
    edges[v[0]]=e;s.edges.push_back({v[0],a,b});s.incident[a].push_back(e);s.incident[b].push_back(e);
  }
  s.routes.resize(s.edges.size());s.changedEdge.resize(s.edges.size());present.clear();
  std::ifstream rs(directory+"/"+prefix+"routes.tsv");if(!rs)throw std::runtime_error("missing routes");
  while(std::getline(rs,line)) {
    const auto v=split(line);if(v.size()!=2||!edges.count(v[0])||!present.insert(edges.at(v[0])).second)
      throw std::runtime_error("invalid route");
    std::string text=v[1];std::replace(text.begin(),text.end(),',',' ');std::istringstream points(text);Point a,b;std::string extra;
    if(!(points>>a.x>>a.y>>b.x>>b.y)||(points>>extra))throw std::runtime_error("route is not straight");
    s.routes[edges.at(v[0])]=segment(a,b);
  }
  if(present.size()!=s.edges.size())throw std::runtime_error("missing route");
  return s;
}
long hardScore(const State& s) {
  long result=0;for(int e=0;e<int(s.edges.size());e++)result+=boundaryEdgeCost(s,e,s.routes[e]).hard;return result;
}
struct NodeCost {Score score;long hard=0;};
void moving(State& s,int n,bool value) {
  s.moved[n]=value;for(int e:s.incident[n])s.changedEdge[e]=value;
}
NodeCost localNode(const State& s,int n) {
  NodeCost c{s.local({n},s.incident[n]),0};
  for(int e:s.incident[n]) {
    const auto edge=s.edges[e];const auto line=s.routes[e];
    c.hard+=hits(line,s.pos[edge.s],s.nodes[edge.s],-.02)+hits(line,s.pos[edge.t],s.nodes[edge.t],-.02);
    for(int f=0;f<int(s.edges.size());f++)if(f!=e&&(!s.changedEdge[f]||e<f)) {
      const auto other=s.edges[f];
      const bool adjacent=edge.s==other.s||edge.s==other.t||edge.t==other.s||edge.t==other.t;
      c.hard+=2*(long(adjacent&&crosses(line,s.routes[f]))+boundaryContact(s,e,line,f,s.routes[f]));
    }
  }
  return c;
}
bool permitted(NodeCost a,NodeCost ceiling) {
  return !a.score.overlap&&!a.score.spacing&&a.score.visual()<=ceiling.score.visual()&&a.hard<=ceiling.hard;
}
bool betterPair(NodeCost a,NodeCost b,NodeCost x,NodeCost y) {
  if(a.score.visual()!=x.score.visual())return a.score.visual()<x.score.visual();
  if(a.hard!=x.hard)return a.hard<x.hard;
  if(b.score.visual()!=y.score.visual())return b.score.visual()<y.score.visual();
  if(b.hard!=y.hard)return b.hard<y.hard;
  return a.score.hit+b.score.hit<x.score.hit+y.score.hit;
}
std::vector<Segment> incidentRoutes(const State& s,int n) {
  std::vector<Segment> out;for(int e:s.incident[n])out.push_back(s.routes[e]);return out;
}
void translate(State& s,int n,Point origin,Point destination,const std::vector<Segment>& original) {
  s.pos[n]=destination;const double dx=destination.x-origin.x,dy=destination.y-origin.y;
  for(size_t i=0;i<s.incident[n].size();i++) {
    const int e=s.incident[n][i];auto a=original[i].a,b=original[i].b;
    auto& p=s.edges[e].s==n?a:b;p={rounded(p.x+dx),rounded(p.y+dy)};s.routes[e]=segment(a,b);
  }
}
bool spaced(const State& s,int n) {
  for(int m=0;m<int(s.nodes.size());m++)if(m!=n&&s.pair(n,m).spacing)return false;return true;
}
void selfTest() {
  std::mt19937_64 rng(8231);long checked=0;
  for(int test=0;test<32;test++) {
    State a;a.nodes.resize(8);a.pos.resize(8);
    for(int n=0;n<8;n++) {
      a.nodes[n]={std::to_string(n),double(20+rng()%80),double(20+rng()%80)};
      a.pos[n]={double(int(rng()%1000)-500),double(int(rng()%1000)-500)};
    }
    a.edges={{"a",0,1},{"b",2,3},{"c",0,4},{"d",1,5},{"e",6,7},{"f",2,7}};a.allRoutes();State b=a;
    for(int n=8;n<12;n++){b.nodes.push_back({std::to_string(n),50,50});b.pos.push_back({double(int(rng()%1000)-500),double(int(rng()%1000)-500)});}
    b.edges.insert(b.edges.end(),{{"g",0,8},{"h",1,9},{"i",10,11},{"j",6,9}});b.allRoutes();
    for(State* s:{&a,&b}) {
      s->incident.resize(s->nodes.size());s->moved.resize(s->nodes.size());s->changedEdge.resize(s->edges.size());
      for(int e=0;e<int(s->edges.size());e++){s->incident[s->edges[e].s].push_back(e);s->incident[s->edges[e].t].push_back(e);}
      const auto baseline=s->full();const auto baselineHard=hardScore(*s);
      for(int n=0;n<8;n++) {
        moving(*s,n,true);const auto before=localNode(*s,n);const auto origin=s->pos[n];const auto routes=incidentRoutes(*s,n);
        for(int k=0;k<8;k++) {
          const Point p{origin.x+double(int(rng()%401)-200),origin.y+double(int(rng()%401)-200)};
          translate(*s,n,origin,p,routes);const auto after=localNode(*s,n);
          if(!equal(s->full()-baseline,after.score-before.score)||hardScore(*s)-baselineHard!=after.hard-before.hard)
            throw std::runtime_error("node local delta differs from global geometry");
          checked++;
        }
        translate(*s,n,origin,origin,routes);moving(*s,n,false);
      }
    }
  }
  std::cout<<"{\"selfTest\":\"pass\",\"globalDeltaComparisons\":"<<checked<<"}\n";
}
}
