#pragma once
// Source-only schema: no positions, routes, clusters, or cached embeddings.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace source_input_layout {
inline void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Node {std::string id;double width=0,height=0;};
struct Edge {std::string id;std::size_t source=0,target=0;};
struct Source {
  std::vector<Node> nodes;
  std::vector<Edge> edges;
};
using Features=std::array<double,64>;
inline std::uint32_t fnv(const std::string& text){std::uint32_t value=2166136261u;for(unsigned char byte:text)value=(value^byte)*16777619u;return value;}
inline std::array<double,24> nameFeatures(std::string name){
  // Unsupported name encodings fall back to the ordinary native layout.
  for(char& c:name){check(static_cast<unsigned char>(c)<128,"unsupported source model name encoding");if(c>='A'&&c<='Z')c=char(c-'A'+'a');}
  const std::string text="^"+name+"$";std::array<double,24> result{};
  for(std::size_t length:{2u,3u})for(std::size_t offset=0;offset+length<=text.size();++offset){const auto code=fnv(text.substr(offset,length));result[code%24]+=code&0x80000000u?1:-1;}
  double norm=0;for(double value:result)norm+=value*value;norm=std::sqrt(norm);if(!norm)norm=1;for(auto& value:result)value/=norm;return result;
}
struct BuiltFeatures {std::vector<Features> rows;double physicalUnit=0;std::size_t coreModels=0,isolates=0;};
inline BuiltFeatures features(const Source& source){
  const auto count=source.nodes.size();check(count>0,"missing source features");BuiltFeatures result;result.rows.resize(count);
  std::vector<std::vector<std::size_t>> adjacent(count);std::vector<std::size_t> incoming(count),outgoing(count),components(count),wave(count);
  for(auto edge:source.edges){check(edge.source<count&&edge.target<count&&edge.source!=edge.target,"invalid original source relation");
    adjacent[edge.source].push_back(edge.target);adjacent[edge.target].push_back(edge.source);++outgoing[edge.source];++incoming[edge.target];}
  auto orderById=[&](std::size_t a,std::size_t b){return source.nodes[a].id<source.nodes[b].id;};
  for(auto& list:adjacent){std::sort(list.begin(),list.end(),orderById);list.erase(std::unique(list.begin(),list.end()),list.end());}
  std::vector<unsigned char> visited(count,0),alive(count,1);std::vector<std::size_t> order(count);std::iota(order.begin(),order.end(),0);std::sort(order.begin(),order.end(),orderById);
  for(auto start:order)if(!visited[start]){std::vector<std::size_t> todo{start},members;visited[start]=1;
    while(!todo.empty()){const auto node=todo.back();todo.pop_back();members.push_back(node);for(auto peer:adjacent[node])if(!visited[peer]){visited[peer]=1;todo.push_back(peer);}}
    for(auto node:members)components[node]=members.size();
  }
  for(std::size_t iteration=1;;++iteration){std::vector<std::size_t> removed;
    for(auto node:order)if(alive[node]){std::size_t degree=0;for(auto peer:adjacent[node])degree+=alive[peer];if(degree<2)removed.push_back(node);}
    if(removed.empty())break;for(auto node:removed){alive[node]=0;wave[node]=iteration;}
  }
  std::vector<double> diagonal(count),sorted;std::vector<std::array<double,24>> names(count);
  for(std::size_t n=0;n<count;++n){const auto node=source.nodes[n];check(node.width>0&&node.height>0,"source feature card dimensions invalid");
    diagonal[n]=std::hypot(node.width,node.height);names[n]=nameFeatures(node.id);result.coreModels+=alive[n];result.isolates+=adjacent[n].empty();}
  sorted=diagonal;std::sort(sorted.begin(),sorted.end());const double median=count%2?sorted[count/2]:(sorted[count/2-1]+sorted[count/2])/2;
  result.physicalUnit=median*std::sqrt(double(count));const double denominator=std::log1p(double(count));
  for(std::size_t n=0;n<count;++n){const auto node=source.nodes[n];const auto degree=adjacent[n].size();std::set<std::size_t> second;
    double meanDegree=0,meanDiagonal=0;for(auto peer:adjacent[n]){second.insert(adjacent[peer].begin(),adjacent[peer].end());meanDegree+=adjacent[peer].size();meanDiagonal+=diagonal[peer];}
    second.erase(n);if(degree){meanDegree/=degree;meanDiagonal/=degree;}
    auto& f=result.rows[n];const std::array<double,16> graph{std::log1p(node.width)/8,std::log1p(node.height)/8,node.width/diagonal[n],node.height/diagonal[n],
      std::log1p(double(degree))/denominator,std::log1p(double(incoming[n]))/denominator,std::log1p(double(outgoing[n]))/denominator,
      (double(outgoing[n])-double(incoming[n]))/(1+incoming[n]+outgoing[n]),double(alive[n]),std::log1p(double(wave[n]))/denominator,double(components[n])/count,
      std::log1p(meanDegree)/denominator,std::log1p(double(second.size()))/denominator,std::log1p(meanDiagonal/median),double(degree==0),std::log1p(double(incoming[n]+outgoing[n]))/denominator};
    std::copy(graph.begin(),graph.end(),f.begin());std::copy(names[n].begin(),names[n].end(),f.begin()+16);
    for(std::size_t k=0;k<24;++k){double value=0;for(auto peer:adjacent[n])value+=names[peer][k];f[k+40]=degree?value/degree:0;}
    for(double value:f)check(std::isfinite(value),"nonfinite source input feature");
  }return result;
}
} // namespace source_input_layout
