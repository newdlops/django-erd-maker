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
int main(int argc,char** argv) {
  try {
    if(argc==2&&std::string(argv[1])=="--self-test"){selfTest();return 0;}
    const auto directory=arg(argc,argv,"--directory"),output=arg(argc,argv,"--out");
    auto a=loadView(directory,""),b=loadView(directory,"individual.");
    const double seconds=number(argc,argv,"--seconds",20),radius=number(argc,argv,"--max-displacement",384);
    const int rounds=number(argc,argv,"--rounds",2),maxNodes=number(argc,argv,"--max-nodes",256);
    if(!std::isfinite(seconds)||seconds<=0||seconds>20||!std::isfinite(radius)||radius<=0||radius>512
      ||rounds<1||rounds>8||maxNodes<1||maxNodes>256)throw std::runtime_error("invalid bounded search limits");
    std::set<std::string> singleton;std::ifstream allowed(directory+"/eligible-singletons.txt");std::string id;
    if(!allowed)throw std::runtime_error("missing eligible singletons");while(std::getline(allowed,id))if(!id.empty())singleton.insert(id);
    std::unordered_map<std::string,int> nodes,edges;for(int n=0;n<int(b.nodes.size());n++)nodes[b.nodes[n].id]=n;
    for(int e=0;e<int(b.edges.size());e++)edges[b.edges[e].id]=e;
    std::vector<int> mapping(a.nodes.size(),-1),order;std::vector<long> pressure;
    const auto initial=a.full(&pressure),initialIndividual=b.full();const auto initialPositions=a.pos;
    const long initialHard=hardScore(a),initialIndividualHard=hardScore(b);
    long expectedVisual=initial.visual(),expectedIndividual=initialIndividual.visual();
    long expectedHard=initialHard,expectedIndividualHard=initialIndividualHard;
    for(int n=0;n<int(a.nodes.size());n++)if(nodes.count(a.nodes[n].id)) {
      const int m=nodes.at(a.nodes[n].id);bool valid=boundarySame(a.pos[n],b.pos[m])
        &&a.nodes[n].w==b.nodes[m].w&&a.nodes[n].h==b.nodes[m].h&&a.incident[n].size()==b.incident[m].size();
      for(int e:a.incident[n]) {
        if(!singleton.count(a.edges[e].id)||!edges.count(a.edges[e].id)){valid=false;break;}
        const int f=edges.at(a.edges[e].id);
        valid=valid&&a.nodes[a.edges[e].s].id==b.nodes[b.edges[f].s].id&&a.nodes[a.edges[e].t].id==b.nodes[b.edges[f].t].id
          &&boundarySame(a.routes[e].a,b.routes[f].a)&&boundarySame(a.routes[e].b,b.routes[f].b);
      }
      if(valid&&pressure[n]){mapping[n]=m;order.push_back(n);}
    }
    std::stable_sort(order.begin(),order.end(),[&](int x,int y){return pressure[x]>pressure[y];});
    const int eligible=order.size();if(order.size()>size_t(maxNodes))order.resize(maxNodes);
    double left=1e100,right=-1e100,top=1e100,bottom=-1e100;
    for(int n=0;n<int(a.nodes.size());n++){left=std::min(left,a.pos[n].x-a.nodes[n].w/2);right=std::max(right,a.pos[n].x+a.nodes[n].w/2);top=std::min(top,a.pos[n].y-a.nodes[n].h/2);bottom=std::max(bottom,a.pos[n].y+a.nodes[n].h/2);}
    const auto start=std::chrono::steady_clock::now();const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
    long queries=0,changes=0;int completed=0;std::set<int> movedNodes;
    std::ofstream journal(output+".changes.tsv");journal<<"round\tnode\toldX\toldY\tnewX\tnewY\toverviewDelta\tindividualDelta\n";
    for(int pass=0;pass<rounds&&elapsed()<seconds;pass++) {
      const long beforeChanges=changes;
      for(int n:order) {
        if(elapsed()>=seconds)break;const int m=mapping[n];const auto origin=a.pos[n];
        const auto routesA=incidentRoutes(a,n),routesB=incidentRoutes(b,m);moving(a,n,true);moving(b,m,true);
        const auto beforeA=localNode(a,n),beforeB=localNode(b,m);auto bestA=beforeA,bestB=beforeB;Point best=origin;
        std::vector<Point> candidates;
        for(double step:{16,32,64,128,256})for(int dx=-1;dx<=1;dx++)for(int dy=-1;dy<=1;dy++)if(dx||dy)
          candidates.push_back({origin.x+step*dx,origin.y+step*dy});
        for(int e=0;e<int(a.edges.size());e++)if(!a.changedEdge[e]&&hits(a.routes[e],origin,a.nodes[n])) {
          const auto line=a.routes[e];const double dx=line.b.x-line.a.x,dy=line.b.y-line.a.y,length=std::hypot(dx,dy);if(length<.01)continue;
          const Point normal{-dy/length,dx/length};const double distance=(origin.x-line.a.x)*normal.x+(origin.y-line.a.y)*normal.y;
          const double clear=(std::abs(normal.x)*a.nodes[n].w+std::abs(normal.y)*a.nodes[n].h)/2+10.02;
          for(double sign:{-1,1})candidates.push_back({origin.x+normal.x*(sign*clear-distance),origin.y+normal.y*(sign*clear-distance)});
        }
        for(int e:a.incident[n])for(int f=0;f<int(a.edges.size());f++)if(f!=e&&crosses(a.routes[e],a.routes[f])) {
          const auto line=a.routes[f];const double dx=line.b.x-line.a.x,dy=line.b.y-line.a.y,length=std::hypot(dx,dy);if(length<.01)continue;
          const Point normal{-dy/length,dx/length},q=a.edges[e].s==n?a.routes[e].b:a.routes[e].a;
          const double distance=(origin.x-line.a.x)*normal.x+(origin.y-line.a.y)*normal.y;
          const double sign=(q.x-line.a.x)*normal.x+(q.y-line.a.y)*normal.y>=0?1:-1;
          const double clear=(std::abs(normal.x)*a.nodes[n].w+std::abs(normal.y)*a.nodes[n].h)/2+.02;
          candidates.push_back({origin.x+normal.x*(sign*clear-distance),origin.y+normal.y*(sign*clear-distance)});
        }
        for(Point p:candidates) {
          if(elapsed()>=seconds)break;p={rounded(p.x),rounded(p.y)};
          if(std::hypot(p.x-initialPositions[n].x,p.y-initialPositions[n].y)>radius||p.x-a.nodes[n].w/2<left
            ||p.x+a.nodes[n].w/2>right||p.y-a.nodes[n].h/2<top||p.y+a.nodes[n].h/2>bottom)continue;
          queries++;translate(a,n,origin,p,routesA);translate(b,m,origin,p,routesB);
          if(!spaced(a,n)||!spaced(b,m))continue;
          const auto costA=localNode(a,n),costB=localNode(b,m);
          if(permitted(costA,beforeA)&&permitted(costB,beforeB)&&betterPair(costA,costB,bestA,bestB))
            {best=p;bestA=costA;bestB=costB;}
        }
        translate(a,n,origin,best,routesA);translate(b,m,origin,best,routesB);moving(a,n,false);moving(b,m,false);
        if(!boundarySame(best,origin)) {
          changes++;movedNodes.insert(n);expectedVisual+=bestA.score.visual()-beforeA.score.visual();expectedIndividual+=bestB.score.visual()-beforeB.score.visual();
          expectedHard+=bestA.hard-beforeA.hard;expectedIndividualHard+=bestB.hard-beforeB.hard;
          journal<<pass<<'\t'<<a.nodes[n].id<<'\t'<<origin.x<<'\t'<<origin.y<<'\t'<<best.x<<'\t'<<best.y<<'\t'
            <<bestA.score.visual()-beforeA.score.visual()<<'\t'<<bestB.score.visual()-beforeB.score.visual()<<'\n';
        }
      }
      completed++;std::cerr<<"node pass="<<completed<<" overview="<<expectedVisual<<" individual="<<expectedIndividual<<" changes="<<changes<<" seconds="<<elapsed()<<'\n';
      if(changes==beforeChanges)break;
    }
    const auto final=a.full(),finalIndividual=b.full();const long finalHard=hardScore(a),finalIndividualHard=hardScore(b);
    if(final.visual()!=expectedVisual||finalIndividual.visual()!=expectedIndividual||finalHard!=expectedHard||finalIndividualHard!=expectedIndividualHard
      ||final.visual()>initial.visual()||finalIndividual.visual()>initialIndividual.visual()||finalHard>initialHard||finalIndividualHard>initialIndividualHard
      ||final.overlap||final.spacing||finalIndividual.overlap||finalIndividual.spacing)throw std::runtime_error("full node audit failed");
    a.save(output);b.save(output+".individual");
    std::ostringstream stats;stats<<"{\"initialVisual\":"<<initial.visual()<<",\"visual\":"<<final.visual()<<",\"cross\":"<<final.cross<<",\"hit\":"<<final.hit
      <<",\"initialHardConditions\":"<<initialHard<<",\"hardConditions\":"<<finalHard<<",\"initialIndividualVisual\":"<<initialIndividual.visual()
      <<",\"individualVisual\":"<<finalIndividual.visual()<<",\"initialIndividualHardConditions\":"<<initialIndividualHard<<",\"individualHardConditions\":"<<finalIndividualHard
      <<",\"overlap\":"<<final.overlap<<",\"spacing\":"<<final.spacing<<",\"queries\":"<<queries<<",\"changes\":"<<changes<<",\"changedNodes\":"<<movedNodes.size()
      <<",\"eligibleNodes\":"<<eligible<<",\"selectedNodes\":"<<order.size()<<",\"rounds\":"<<completed<<",\"maxDisplacement\":"<<radius
      <<",\"timedOut\":"<<(elapsed()>=seconds?"true":"false")<<",\"seconds\":"<<elapsed()<<"}";
    std::ofstream(output+".stats.json")<<stats.str()<<'\n';std::cout<<stats.str()<<'\n';return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
