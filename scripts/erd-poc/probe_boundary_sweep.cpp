#include "constrained_scene.h"
#include "constrained_boundary_sweep.h"
#include <set>
#include <sstream>

namespace {
void selfTest(){
  std::mt19937_64 rng(2718);long checked=0;
  for(int test=0;test<160;test++){
    State s;s.nodes.resize(8);s.pos.resize(8);s.incident.resize(8);s.adj.resize(8);
    for(int n=0;n<8;n++){
      s.nodes[n]={std::to_string(n),.04+.01*(rng()%14),.04+.01*(rng()%14)};
      // The wider half also makes the 10-unit card-hit padding enter and
      // leave the variable route, rather than remaining a constant cost.
      const int span=test<80?600:6000;
      s.pos[n]={double(int(rng()%span)-span/2)/100,double(int(rng()%span)-span/2)/100};
    }
    s.edges={{"a",0,1},{"b",2,3},{"c",0,4},{"d",1,5},{"e",6,7},{"f",2,7}};s.allRoutes();
    for(int e=0;e<int(s.edges.size());e++)for(int end=0;end<2;end++){
      const auto result=sweepBoundaryEndpoint(s,e,end);auto brute=boundaryEdgeCost(s,e,s.routes[e]);
      for(const auto& face:boundaryFaces(s,end?s.edges[e].t:s.edges[e].s))for(int k=0;k<=face.last;k++){
        const auto p=face.at(k);const auto route=end?segment(s.routes[e].a,p):segment(p,s.routes[e].b);
        const auto cost=boundaryEdgeCost(s,e,route);if(boundaryBetter(cost,brute))brute=cost;
      }
      if(!boundaryEqual(result.cost,brute))throw std::runtime_error("boundary optimum differs from exhaustive grid enumeration");
      checked++;
    }
  }
  std::cout<<"{\"selfTest\":\"pass\",\"exhaustiveEndpointComparisons\":"<<checked<<"}\n";
}
Point parsePoint(const std::string& p){const auto comma=p.find(',');if(comma==std::string::npos)throw std::runtime_error("bad point");
  return {std::stod(p.substr(0,comma)),std::stod(p.substr(comma+1))};}
State load(int argc,char**argv){
  State s;s.width=38720;s.height=38720;
  std::unordered_map<std::string,int> ids,edgeIds;std::string line;
  std::ifstream nodes(arg(argc,argv,"--nodes"));if(!nodes)throw std::runtime_error("missing nodes file");
  while(std::getline(nodes,line)){auto f=split(line);if(f.size()!=3||ids.count(f[0]))throw std::runtime_error("invalid node");
    ids[f[0]]=s.nodes.size();s.nodes.push_back({f[0],std::stod(f[1]),std::stod(f[2])});}
  s.pos.resize(s.nodes.size());s.incident.resize(s.nodes.size());s.adj.resize(s.nodes.size());s.moved.resize(s.nodes.size());
  std::ifstream positions(arg(argc,argv,"--positions"));std::set<int> present;
  if(!positions)throw std::runtime_error("missing positions file");
  while(std::getline(positions,line)){auto f=split(line);if(f.size()!=3||!ids.count(f[0])||!present.insert(ids.at(f[0])).second)throw std::runtime_error("invalid position");
    s.pos[ids.at(f[0])]={std::stod(f[1]),std::stod(f[2])};}
  if(present.size()!=s.nodes.size())throw std::runtime_error("missing node position");
  std::ifstream edges(arg(argc,argv,"--edges"));if(!edges)throw std::runtime_error("missing edges file");
  while(std::getline(edges,line)){auto f=split(line);if(f.size()!=3||!ids.count(f[1])||!ids.count(f[2])||edgeIds.count(f[0]))throw std::runtime_error("invalid edge");
    int a=ids.at(f[1]),b=ids.at(f[2]),e=s.edges.size();if(a==b)throw std::runtime_error("self edge");
    edgeIds[f[0]]=e;s.edges.push_back({f[0],a,b});s.incident[a].push_back(e);s.incident[b].push_back(e);s.adj[a].push_back(b);s.adj[b].push_back(a);}
  s.changedEdge.resize(s.edges.size());s.routes.resize(s.edges.size());present.clear();
  std::ifstream routes(arg(argc,argv,"--routes"));if(!routes)throw std::runtime_error("missing routes file");
  while(std::getline(routes,line)){auto f=split(line);if(f.size()!=2||!edgeIds.count(f[0])||!present.insert(edgeIds.at(f[0])).second)throw std::runtime_error("invalid route");
    std::istringstream points(f[1]);std::string a,b,extra;if(!(points>>a>>b)||points>>extra)throw std::runtime_error("route is not straight");
    s.routes[edgeIds.at(f[0])]=segment(parsePoint(a),parsePoint(b));}
  if(present.size()!=s.edges.size())throw std::runtime_error("missing route");
  return s;
}
long hardScore(const State&s){long total=0;for(int e=0;e<int(s.edges.size());e++)total+=boundaryEdgeCost(s,e,s.routes[e]).hard;return total;}
}
int main(int argc,char**argv){
 try{
  if(argc==2&&std::string(argv[1])=="--self-test"){selfTest();return 0;}
  auto s=load(argc,argv);const auto start=std::chrono::steady_clock::now();
  const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
  const double seconds=number(argc,argv,"--seconds",90);const int rounds=number(argc,argv,"--rounds",2);
  const bool joint=number(argc,argv,"--joint-probes",0)!=0;
  const std::string output=arg(argc,argv,"--out"),selection=arg(argc,argv,"--selection","all");
  std::set<std::string> selected;if(selection!="all"){
    std::ifstream input(selection);std::string id;if(!input)throw std::runtime_error("selection missing");
    while(std::getline(input,id))selected.insert(id);
  }
  std::vector<int> order;for(int e=0;e<int(s.edges.size());e++)if(selection=="all"||selected.count(s.edges[e].id))order.push_back(e);
  if(selection!="all"&&order.size()!=selected.size())throw std::runtime_error("unknown selected edge");
  const auto initial=s.full();const long initialHard=hardScore(s);long queries=0,changed=0,intervals=0;int completed=0;
  std::cerr<<"boundary start visual="<<initial.visual()<<" hard="<<initialHard<<" selected="<<order.size()<<'\n';
  std::ofstream journal(output+".changes.tsv");journal<<"round\tedge\tend\toldVisual\tnewVisual\toldHard\tnewHard\toldPoints\tnewPoints\n";
  for(int pass=0;pass<rounds&&elapsed()<seconds;pass++){
    const long oldChanged=changed;
    std::vector<BoundaryCost> costs(s.edges.size());
    for(int e:order)costs[e]=boundaryEdgeCost(s,e,s.routes[e]);
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){return boundaryBetter(costs[b],costs[a]);});
    for(int e:order){
      for(int end=0;end<2&&elapsed()<seconds;end++){
        const auto old=s.routes[e];const auto before=boundaryEdgeCost(s,e,old);
        auto result=sweepBoundaryEndpoint(s,e,end);queries++;intervals+=result.evaluatedIntervals;
        // The sweep itself is exact for one fixed endpoint. These eight
        // explicit opposing-end seeds permit a two-endpoint move; this is
        // deliberately not described as an exact two-dimensional optimum.
        if(joint&&(before.visual()||before.hard)){
          const int otherNode=end?s.edges[e].s:s.edges[e].t;
          std::vector<Point> seeds;
          for(const auto& face:boundaryFaces(s,otherNode))for(int k:{0,face.last/2,face.last}){
            Point p=face.at(k);if(std::none_of(seeds.begin(),seeds.end(),[&](Point q){return boundarySame(p,q);}))seeds.push_back(p);
          }
          for(Point q:seeds){
            if(elapsed()>=seconds)break;
            s.routes[e]=end?segment(q,old.b):segment(old.a,q);
            const auto candidate=sweepBoundaryEndpoint(s,e,end);queries++;intervals+=candidate.evaluatedIntervals;
            if(boundaryBetter(candidate.cost,result.cost))result=candidate;
          }
          s.routes[e]=old;
        }
        if(boundaryBetter(result.cost,before)){
          s.routes[e]=result.route;changed++;
          journal<<pass<<'\t'<<s.edges[e].id<<'\t'<<end<<'\t'<<before.visual()<<'\t'<<result.cost.visual()<<'\t'<<before.hard<<'\t'<<result.cost.hard<<'\t'
            <<old.a.x<<','<<old.a.y<<' '<<old.b.x<<','<<old.b.y<<'\t'<<result.route.a.x<<','<<result.route.a.y<<' '<<result.route.b.x<<','<<result.route.b.y<<'\n';
        }
      }
      if(elapsed()>=seconds)break;
    }
    completed++;s.save(output);
    std::cerr<<"boundary pass="<<pass+1<<" visual="<<s.full().visual()<<" hard="<<hardScore(s)<<" changes="<<changed<<" seconds="<<elapsed()<<'\n';
    if(changed==oldChanged)break;
  }
  s.save(output);const auto final=s.full();const long hard=hardScore(s);
  std::ostringstream stats;stats<<"{\"initialVisual\":"<<initial.visual()<<",\"initialHardConditions\":"<<initialHard
    <<",\"visual\":"<<final.visual()<<",\"cross\":"<<final.cross<<",\"hit\":"<<final.hit<<",\"hardConditions\":"<<hard
    <<",\"overlap\":"<<final.overlap<<",\"spacing\":"<<final.spacing<<",\"queries\":"<<queries<<",\"changes\":"<<changed
    <<",\"intervals\":"<<intervals<<",\"selectedEdges\":"<<order.size()<<",\"rounds\":"<<completed<<",\"timedOut\":"<<(elapsed()>=seconds?"true":"false")
    <<",\"seconds\":"<<elapsed()<<"}";
  std::ofstream(output+".stats.json")<<stats.str()<<'\n';std::cout<<stats.str()<<'\n';return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
