#include "constrained_scene.h"
#include "constrained_dual_boundary_sweep.h"
#include <set>
#include <sstream>

namespace {
State loadView(const std::string& directory,const std::string& prefix) {
  State s;std::unordered_map<std::string,int> nodes,edges;std::string line;
  std::ifstream ns(directory+"/"+prefix+"nodes.tsv");
  if(!ns)throw std::runtime_error("missing nodes file");
  while(std::getline(ns,line)) {
    const auto v=split(line);if(v.size()!=3||nodes.count(v[0]))throw std::runtime_error("invalid node");
    nodes[v[0]]=s.nodes.size();s.nodes.push_back({v[0],std::stod(v[1]),std::stod(v[2])});
  }
  if(s.nodes.empty())throw std::runtime_error("empty view");
  s.pos.resize(s.nodes.size());s.incident.resize(s.nodes.size());s.adj.resize(s.nodes.size());
  s.moved.resize(s.nodes.size());std::set<int> present;
  std::ifstream ps(directory+"/"+prefix+"positions.tsv");if(!ps)throw std::runtime_error("missing positions file");
  while(std::getline(ps,line)) {
    const auto v=split(line);if(v.size()!=3||!nodes.count(v[0])||!present.insert(nodes.at(v[0])).second)
      throw std::runtime_error("invalid position");
    s.pos[nodes.at(v[0])]={std::stod(v[1]),std::stod(v[2])};
  }
  if(present.size()!=s.nodes.size())throw std::runtime_error("missing position");
  std::ifstream es(directory+"/"+prefix+"edges.tsv");if(!es)throw std::runtime_error("missing edges file");
  while(std::getline(es,line)) {
    const auto v=split(line);if(v.size()!=3||edges.count(v[0])||!nodes.count(v[1])||!nodes.count(v[2]))
      throw std::runtime_error("invalid edge");
    const int a=nodes.at(v[1]),b=nodes.at(v[2]),e=s.edges.size();if(a==b)throw std::runtime_error("self edge");
    edges[v[0]]=e;s.edges.push_back({v[0],a,b});s.incident[a].push_back(e);s.incident[b].push_back(e);
    s.adj[a].push_back(b);s.adj[b].push_back(a);
  }
  s.routes.resize(s.edges.size());s.changedEdge.resize(s.edges.size());present.clear();
  std::ifstream rs(directory+"/"+prefix+"routes.tsv");if(!rs)throw std::runtime_error("missing routes file");
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
  long total=0;for(int e=0;e<int(s.edges.size());e++)total+=boundaryEdgeCost(s,e,s.routes[e]).hard;return total;
}
void selfTest() {
  std::mt19937_64 rng(314159);long checked=0,improved=0,alternatives=0;
  for(int test=0;test<80;test++) {
    State a,b;a.nodes.resize(8);a.pos.resize(8);
    for(int n=0;n<8;n++) {
      a.nodes[n]={std::to_string(n),.04+.01*(rng()%14),.04+.01*(rng()%14)};
      const int span=test<40?600:6000;
      a.pos[n]={double(int(rng()%span)-span/2)/100,double(int(rng()%span)-span/2)/100};
    }
    a.edges={{"a",0,1},{"b",2,3},{"c",0,4},{"d",1,5},{"e",6,7},{"f",2,7}};a.allRoutes();b=a;
    for(int n=8;n<12;n++) {
      b.nodes.push_back({std::to_string(n),.05+.01*(rng()%12),.05+.01*(rng()%12)});
      b.pos.push_back({double(int(rng()%6000)-3000)/100,double(int(rng()%6000)-3000)/100});
    }
    for(Edge e:std::vector<Edge>{{"g",0,8},{"h",1,9},{"i",10,11},{"j",6,9}})b.edges.push_back(e);
    b.allRoutes();
    const auto initialA=a.full(),initialB=b.full();
    const long hardA=hardScore(a),hardB=hardScore(b);
    for(int e=0;e<int(a.edges.size());e++)for(int end=0;end<2;end++) {
      const auto original=a.routes[e];const auto ceiling=dualEdgeCost(a,e,b,e,original);
      const auto result=sweepDualBoundaryEndpoint(a,e,b,e,end,original,ceiling);auto brute=ceiling;
      for(const auto& face:boundaryFaces(a,end?a.edges[e].t:a.edges[e].s))for(int k=0;k<=face.last;k++) {
        const auto p=face.at(k);const auto line=end?segment(original.a,p):segment(p,original.b);
        const auto cost=dualEdgeCost(a,e,b,e,line);
        if(dualPermitted(cost,ceiling)&&dualBetter(cost,brute))brute=cost;
      }
      if(!dualEqual(result.cost,brute)||!dualPermitted(result.cost,ceiling))
        throw std::runtime_error("dual optimum differs from feasible exhaustive enumeration");
      a.routes[e]=b.routes[e]=result.route;
      if(a.full().visual()-initialA.visual()!=result.cost.overview.visual()-ceiling.overview.visual()
        ||b.full().visual()-initialB.visual()!=result.cost.individual.visual()-ceiling.individual.visual()
        ||hardScore(a)-hardA!=result.cost.overview.hard-ceiling.overview.hard
        ||hardScore(b)-hardB!=result.cost.individual.hard-ceiling.individual.hard)
        throw std::runtime_error("local dual delta differs from global scores");
      a.routes[e]=b.routes[e]=original;
      const auto single=sweepBoundaryEndpoint(a,e,end);
      const auto singleCost=dualEdgeCost(a,e,b,e,single.route);
      const bool gain=result.cost.overview.visual()<ceiling.overview.visual();
      improved+=gain;alternatives+=gain&&!dualPermitted(singleCost,ceiling);checked++;
    }
  }
  // A primary-view midpoint removes one crossing but introduces two hidden
  // crossings. The opposite boundary interval improves both views instead.
  State a;a.nodes={{"0",4,4},{"1",4,4},{"2",.01,.01},{"3",.01,.01}};
  a.pos={{0,0},{12,0},{6,-.2},{6,.2}};a.edges={{"a",0,1},{"b",2,3}};a.allRoutes();
  State b=a;
  for(int n=4;n<8;n++)b.nodes.push_back({std::to_string(n),.01,.01});
  b.pos.insert(b.pos.end(),{{5,-3},{5,-.3},{7,-3},{7,-.3}});
  b.edges.push_back({"g",4,5});b.edges.push_back({"h",6,7});b.allRoutes();
  const auto original=a.routes[0];const auto ceiling=dualEdgeCost(a,0,b,0,original);
  const auto result=sweepDualBoundaryEndpoint(a,0,b,0,0,original,ceiling);auto brute=ceiling;
  for(const auto& face:boundaryFaces(a,0))for(int k=0;k<=face.last;k++) {
    const auto cost=dualEdgeCost(a,0,b,0,segment(face.at(k),original.b));
    if(dualPermitted(cost,ceiling)&&dualBetter(cost,brute))brute=cost;
  }
  const auto single=sweepBoundaryEndpoint(a,0,0);
  if(!dualEqual(result.cost,brute)||result.cost.overview.visual()>=ceiling.overview.visual()
    ||dualPermitted(dualEdgeCost(a,0,b,0,single.route),ceiling))
    throw std::runtime_error("hidden-crossing regression fixture did not find the feasible alternative");
  improved++;alternatives++;checked++;
  if(!improved||!alternatives)throw std::runtime_error("self-test lacks feasible alternative coverage");
  std::cout<<"{\"selfTest\":\"pass\",\"exhaustiveEndpointComparisons\":"<<checked
    <<",\"overviewImprovements\":"<<improved<<",\"feasibleAlternativesToRejectedSingleView\":"<<alternatives<<"}\n";
}
}
int main(int argc,char** argv) {
  try {
    if(argc==2&&std::string(argv[1])=="--self-test"){selfTest();return 0;}
    const auto directory=arg(argc,argv,"--directory"),output=arg(argc,argv,"--out");
    auto overview=loadView(directory,""),individual=loadView(directory,"individual.");
    const double seconds=number(argc,argv,"--seconds",20);const int rounds=number(argc,argv,"--rounds",2);
    const bool joint=number(argc,argv,"--joint-probes",1)!=0;
    std::unordered_map<std::string,int> individualEdges;std::vector<int> mapping(overview.edges.size(),-1),order;
    for(int f=0;f<int(individual.edges.size());f++)individualEdges[individual.edges[f].id]=f;
    std::set<std::string> selected;std::string id;std::ifstream selection(directory+"/selection.txt");
    if(!selection)throw std::runtime_error("missing selection");
    while(std::getline(selection,id))if(!id.empty())selected.insert(id);
    for(int e=0;e<int(overview.edges.size());e++)if(selected.count(overview.edges[e].id)) {
      if(!individualEdges.count(overview.edges[e].id))throw std::runtime_error("selected overview edge is not a singleton");
      const int f=individualEdges.at(overview.edges[e].id);mapping[e]=f;order.push_back(e);
      for(int end=0;end<2;end++) {
        const int n=end?overview.edges[e].t:overview.edges[e].s,m=end?individual.edges[f].t:individual.edges[f].s;
        if(overview.nodes[n].id!=individual.nodes[m].id||overview.nodes[n].w!=individual.nodes[m].w
          ||overview.nodes[n].h!=individual.nodes[m].h||!boundarySame(overview.pos[n],individual.pos[m]))
          throw std::runtime_error("selected endpoint geometry differs between views");
      }
      if(!boundarySame(overview.routes[e].a,individual.routes[f].a)||!boundarySame(overview.routes[e].b,individual.routes[f].b))
        throw std::runtime_error("selected route differs between views");
    }
    if(order.size()!=selected.size())throw std::runtime_error("unknown selected edge");
    const auto start=std::chrono::steady_clock::now();
    const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
    const auto initial=overview.full(),initialIndividual=individual.full();
    const long initialHard=hardScore(overview),initialIndividualHard=hardScore(individual);
    long queries=0,changed=0,intervals=0;int completed=0;
    std::cerr<<"dual start overview="<<initial.visual()<<" individual="<<initialIndividual.visual()<<" selected="<<order.size()<<'\n';
    std::ofstream journal(output+".changes.tsv");
    journal<<"round\tedge\tend\toldOverview\tnewOverview\toldIndividual\tnewIndividual\n";
    for(int pass=0;pass<rounds&&elapsed()<seconds;pass++) {
      const long beforeChanged=changed;std::vector<DualBoundaryCost> costs(overview.edges.size());
      for(int e:order)costs[e]=dualEdgeCost(overview,e,individual,mapping[e],overview.routes[e]);
      std::stable_sort(order.begin(),order.end(),[&](int a,int b){return dualBetter(costs[b],costs[a]);});
      for(int e:order)for(int end=0;end<2&&elapsed()<seconds;end++) {
        const int f=mapping[e];const auto old=overview.routes[e];
        const auto before=dualEdgeCost(overview,e,individual,f,old);
        auto result=sweepDualBoundaryEndpoint(overview,e,individual,f,end,old,before);
        queries++;intervals+=result.evaluatedIntervals;
        if(joint&&before.overview.visual()) {
          std::vector<Point> seeds;const int n=end?overview.edges[e].s:overview.edges[e].t;
          for(const auto& face:boundaryFaces(overview,n))for(int k:{0,face.last/2,face.last}) {
            const auto p=face.at(k);if(std::none_of(seeds.begin(),seeds.end(),[&](Point q){return boundarySame(p,q);}))seeds.push_back(p);
          }
          for(Point q:seeds) {
            if(elapsed()>=seconds)break;
            overview.routes[e]=end?segment(q,old.b):segment(old.a,q);
            const auto candidate=sweepDualBoundaryEndpoint(overview,e,individual,f,end,old,before);
            queries++;intervals+=candidate.evaluatedIntervals;
            if(dualBetter(candidate.cost,result.cost))result=candidate;
          }
          overview.routes[e]=old;
        }
        if(dualBetter(result.cost,before)) {
          if(!dualPermitted(result.cost,before))throw std::runtime_error("dual score regression");
          overview.routes[e]=individual.routes[f]=result.route;changed++;
          journal<<pass<<'\t'<<overview.edges[e].id<<'\t'<<end<<'\t'<<before.overview.visual()<<'\t'<<result.cost.overview.visual()
            <<'\t'<<before.individual.visual()<<'\t'<<result.cost.individual.visual()<<'\n';
        }
      }
      completed++;overview.save(output);
      std::cerr<<"dual pass="<<pass+1<<" overview="<<overview.full().visual()<<" individual="<<individual.full().visual()
        <<" changes="<<changed<<" seconds="<<elapsed()<<'\n';
      if(changed==beforeChanged)break;
    }
    overview.save(output);const auto final=overview.full(),finalIndividual=individual.full();
    const long finalHard=hardScore(overview),finalIndividualHard=hardScore(individual);
    if(final.visual()>initial.visual()||finalIndividual.visual()>initialIndividual.visual()
      ||finalHard>initialHard||finalIndividualHard>initialIndividualHard)throw std::runtime_error("full dual audit regressed");
    std::ostringstream stats;stats<<"{\"initialVisual\":"<<initial.visual()<<",\"initialHardConditions\":"<<initialHard
      <<",\"visual\":"<<final.visual()<<",\"cross\":"<<final.cross<<",\"hit\":"<<final.hit<<",\"hardConditions\":"<<finalHard
      <<",\"initialIndividualVisual\":"<<initialIndividual.visual()<<",\"individualVisual\":"<<finalIndividual.visual()
      <<",\"initialIndividualHardConditions\":"<<initialIndividualHard<<",\"individualHardConditions\":"<<finalIndividualHard
      <<",\"overlap\":"<<final.overlap<<",\"spacing\":"<<final.spacing<<",\"queries\":"<<queries<<",\"changes\":"<<changed
      <<",\"intervals\":"<<intervals<<",\"selectedEdges\":"<<order.size()<<",\"rounds\":"<<completed
      <<",\"timedOut\":"<<(elapsed()>=seconds?"true":"false")<<",\"seconds\":"<<elapsed()<<"}";
    std::ofstream(output+".stats.json")<<stats.str()<<'\n';std::cout<<stats.str()<<'\n';return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
