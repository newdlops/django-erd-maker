// Exhaustive geometry fixture for the JavaScript production-metric audit.
#include "../../scripts/erd-poc/constrained_scene.h"
#include "../../scripts/erd-poc/constrained_port_search.h"
#include "../../scripts/erd-poc/constrained_slot_assignment.h"
int main() {
  State s;s.width=s.height=7000;
  s.pos={{1000,2000},{3000,1000},{3000,2000},{3000,3000},{6000,3000},{6000,1000},{6000,2000},{1000,5000},{6000,5000}};
  for(int n=0;n<9;n++)s.nodes.push_back({std::to_string(n),280,120});
  s.incident.resize(9);s.adj.resize(9);s.moved.resize(9);
  const std::vector<std::pair<int,int>> ends{{0,1},{0,2},{0,3},{1,4},{2,5},{3,6},{7,8}};
  for(const auto& edge:ends) {
    int e=s.edges.size();s.edges.push_back({std::to_string(e),edge.first,edge.second});
    s.incident[edge.first].push_back(e);s.incident[edge.second].push_back(e);
    s.adj[edge.first].push_back(edge.second);s.adj[edge.second].push_back(edge.first);
  }
  s.changedEdge.resize(s.edges.size());s.allRoutes();const auto baseline=s.full();
  SlotAssignmentModel model(s,{1,2,3},{{3000,5000},{3100,1000}});model.build([]{return false;});
  const auto original=model.full({0,1,2});std::cout<<std::setprecision(12);
  for(int a=0;a<5;a++)for(int b=0;b<5;b++)for(int c=0;c<5;c++)if(a!=b&&a!=c&&b!=c) {
    const std::vector<int> labels{a,b,c};const auto predicted=model.full(labels);
    model.apply(labels);const auto actual=s.full();
    if(!equal(actual,baseline+predicted.score-original.score))return 2;
    std::cout<<R"({"score":[)"<<actual.visual()<<','<<actual.cross<<','<<actual.hit<<','<<actual.overlap<<','<<actual.spacing<<R"(],"positions":[)";
    for(int i=0;i<9;i++){if(i)std::cout<<',';std::cout<<'['<<s.pos[i].x<<','<<s.pos[i].y<<']';}
    std::cout<<R"(],"routes":[)";
    for(int i=0;i<int(s.routes.size());i++) {
      if(i)std::cout<<',';const auto& r=s.routes[i];std::cout<<'['<<r.a.x<<','<<r.a.y<<','<<r.b.x<<','<<r.b.y<<']';
    }
    std::cout<<"]}\n";
  }
}
