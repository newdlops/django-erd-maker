#include "finite_factor_optimizer.h"

namespace {
// A straight route depends only on its endpoint positions. Thus a crossing
// involves at most four position variables, and a card hit at most three.
// Internal edges between moving nodes are retained explicitly.
bool buildGeometryFactors(const State& s,const std::vector<int>& owner,
                          const std::vector<std::vector<Point>>& positions,
                          const std::vector<int>& domains,const std::function<bool()>& expired,
                          FiniteLabelFactors& model) {
  struct RouteChoices {std::vector<int> variables,strides;std::vector<Segment> routes;};
  struct Primitive {int kind,a,b;};
  std::vector<RouteChoices> routes(s.edges.size());
  std::vector<uint64_t> edgeMask(s.edges.size()),nodeMask(s.nodes.size());
  for(int n=0;n<int(s.nodes.size());n++)if(owner[n]>=0)nodeMask[n]=uint64_t(1)<<owner[n];
  std::vector<int> labels(domains.size(),0);
  const auto point=[&](int n){return owner[n]<0?s.pos[n]:positions[n][labels[owner[n]]];};
  for(int e=0;e<int(s.edges.size());e++) {
    if(expired())return false;
    const auto& edge=s.edges[e];auto& choices=routes[e];
    edgeMask[e]=nodeMask[edge.s]|nodeMask[edge.t];
    for(int v=0;v<int(domains.size());v++)if(edgeMask[e]&(uint64_t(1)<<v))choices.variables.push_back(v);
    int count=1;choices.strides.resize(choices.variables.size());
    for(int i=int(choices.variables.size())-1;i>=0;i--){choices.strides[i]=count;count*=domains[choices.variables[i]];}
    for(int index=0;index<count;index++) {
      for(int i=0;i<int(choices.variables.size());i++)labels[choices.variables[i]]=(index/choices.strides[i])%domains[choices.variables[i]];
      auto a=point(edge.s),b=point(edge.t);
      choices.routes.push_back(segment(port(a,s.nodes[edge.s],b),port(b,s.nodes[edge.t],a)));
    }
  }
  model.domains=domains;model.incident.resize(domains.size());
  std::unordered_map<uint64_t,int> byMask;
  std::vector<std::vector<Primitive>> primitives;
  size_t cells=0;
  const auto add=[&](uint64_t mask,Primitive primitive) {
    if(!mask)return true;
    auto found=byMask.find(mask);
    if(found==byMask.end()) {
      DenseLabelFactor factor;
      for(int v=0;v<int(domains.size());v++)if(mask&(uint64_t(1)<<v))factor.variables.push_back(v);
      if(factor.variables.size()>4)throw std::runtime_error("geometry factor has more than four variables");
      size_t count=1;factor.strides.resize(factor.variables.size());
      for(int i=int(factor.variables.size())-1;i>=0;i--){factor.strides[i]=int(count);count*=domains[factor.variables[i]];}
      // Reserve less than 80 MiB for factor cells under the 256 MiB runner.
      if(cells+count>20000000)return false;
      cells+=count;factor.values.resize(count);
      int index=model.factors.size();byMask[mask]=index;
      for(int v:factor.variables)model.incident[v].push_back(index);
      model.factors.push_back(std::move(factor));primitives.emplace_back();
      found=byMask.find(mask);
    }
    auto& factor=model.factors[found->second];
    if(primitive.kind==2&&factor.spacing.empty())factor.spacing.resize(factor.values.size());
    primitives[found->second].push_back(primitive);return true;
  };
  for(int a=0;a<int(s.edges.size());a++) {
    if(expired())return false;
    for(int b=a+1;b<int(s.edges.size());b++)if(!add(edgeMask[a]|edgeMask[b],{0,a,b}))return false;
    for(int n=0;n<int(s.nodes.size());n++)if(n!=s.edges[a].s&&n!=s.edges[a].t)
      if(!add(edgeMask[a]|nodeMask[n],{1,a,n}))return false;
  }
  for(int a=0;a<int(s.nodes.size());a++)for(int b=a+1;b<int(s.nodes.size());b++)
    if(!add(nodeMask[a]|nodeMask[b],{2,a,b}))return false;
  const auto route=[&](int e)->const Segment& {
    const auto& choices=routes[e];int index=0;
    for(int i=0;i<int(choices.variables.size());i++)index+=choices.strides[i]*labels[choices.variables[i]];
    return choices.routes[index];
  };
  for(int f=0;f<int(model.factors.size());f++) {
    auto& factor=model.factors[f];
    for(int index=0;index<int(factor.values.size());index++) {
      if(index%128==0&&expired())return false;
      for(int i=0;i<int(factor.variables.size());i++)labels[factor.variables[i]]=(index/factor.strides[i])%domains[factor.variables[i]];
      int value=0,spacing=0;
      for(const auto& primitive:primitives[f]) {
        int a=primitive.a,b=primitive.b;
        if(primitive.kind==0)value+=crosses(route(a),route(b));
        else if(primitive.kind==1)value+=hits(route(a),point(b),s.nodes[b]);
        else {
          auto p=point(a),q=point(b);
          double dx=std::abs(p.x-q.x)-(s.nodes[a].w+s.nodes[b].w)*.5;
          double dy=std::abs(p.y-q.y)-(s.nodes[a].h+s.nodes[b].h)*.5;
          value+=dx<0&&dy<0;
          spacing+=dx<55.99&&dy<41.99;
        }
      }
      factor.values[index]=value;
      if(!factor.spacing.empty())factor.spacing[index]=spacing;
    }
  }
  std::cerr<<"factor tables="<<model.factors.size()<<" cells="<<cells<<'\n';
  return true;
}
}
