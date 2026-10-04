// Rebuild a selected set of real model cards in a temporary partial scene.
// The caller must evaluate the complete reconstructed scene before accepting it.
#include <functional>

namespace {
bool reconstructPatch(State& s, const std::vector<int>& patch, int samples,
                      std::mt19937_64& rng, double hitWeight,
                      const std::function<bool()>& expired,
                      std::vector<Point>& result, bool ordered=false, double lookahead=0) {
  const auto original=s.pos;
  std::vector<char> present(s.nodes.size(),true),live(s.edges.size(),true);
  for(int n:patch)present[n]=false;
  for(int e=0;e<int(s.edges.size());e++)live[e]=present[s.edges[e].s]&&present[s.edges[e].t];
  std::uniform_real_distribution<double> unit(0,1);
  const auto pick=[&](int size){return int(rng()%uint64_t(size));};
  struct Line {Point a,d;};
  bool success=true;
  for(size_t placed=0;placed<patch.size()&&!expired();placed++) {
    int n=-1;double priority=-1;
    for(int candidate:patch)if(!present[candidate]) {
      if(ordered){n=candidate;break;}
      int anchors=0;for(int neighbor:s.adj[candidate])anchors+=present[neighbor];
      // Anchor-rich vertices go first; vary ties to explore different orders.
      double value=anchors*4+s.adj[candidate].size()+unit(rng)*8;
      if(value>priority){n=candidate;priority=value;}
    }
    if(n<0)break;
    std::vector<int> edges;
    std::vector<Point> anchors;
    std::vector<Line> lines;
    const auto addLine=[&](Point a,Point b) {
      double length=std::hypot(b.x-a.x,b.y-a.y);
      if(length>1e-6)lines.push_back({a,{(b.x-a.x)/length,(b.y-a.y)/length}});
    };
    for(int e:s.incident[n]) {
      int other=s.edges[e].s==n?s.edges[e].t:s.edges[e].s;
      if(!present[other]&&lookahead==0)continue;
      edges.push_back(e);
      if(present[other])anchors.push_back(s.pos[other]);
      const auto route=s.route(e);
      for(int f=0;f<int(s.edges.size());f++)if(live[f]&&crosses(route,s.routes[f])) {
        addLine(s.pos[other],s.routes[f].a);addLine(s.pos[other],s.routes[f].b);
      }
      for(int obstacle=0;obstacle<int(s.nodes.size());obstacle++)if(present[obstacle]&&obstacle!=other&&hits(route,s.pos[obstacle],s.nodes[obstacle])) {
        for(int x:{-1,1})for(int y:{-1,1})addLine(s.pos[other],{s.pos[obstacle].x+x*(s.nodes[obstacle].w*.5+10),s.pos[obstacle].y+y*(s.nodes[obstacle].h*.5+10)});
      }
    }
    Point mean=original[n];
    if(!anchors.empty()) {
      mean={};for(auto p:anchors){mean.x+=p.x;mean.y+=p.y;}
      mean.x/=anchors.size();mean.y/=anchors.size();
    }
    double best=1e100;
    Point destination=original[n];
    const auto evaluate=[&](Point p) {
      if(!s.inside(n,p))return;
      s.pos[n]=p;
      for(int other=0;other<int(s.nodes.size());other++)if(present[other]&&s.pair(n,other).spacing)return;
      double cost=0;
      for(int e=0;e<int(s.edges.size());e++)if(s.edges[e].s!=n&&s.edges[e].t!=n&&(live[e]||lookahead>0))cost+=(live[e]?1.:lookahead)*hitWeight*hits(s.routes[e],p,s.nodes[n]);
      for(int e:edges) {
        const auto route=s.route(e);
        int other=s.edges[e].s==n?s.edges[e].t:s.edges[e].s;
        double weight=present[other]?1.:lookahead;
        for(int f=0;f<int(s.edges.size());f++)if(s.edges[f].s!=n&&s.edges[f].t!=n&&(live[f]||lookahead>0))cost+=weight*(live[f]?1.:lookahead)*crosses(route,s.routes[f]);
        for(int obstacle=0;obstacle<int(s.nodes.size());obstacle++)if((present[obstacle]||lookahead>0)&&obstacle!=s.edges[e].s&&obstacle!=s.edges[e].t)cost+=weight*(present[obstacle]?1.:lookahead)*hitWeight*hits(route,s.pos[obstacle],s.nodes[obstacle]);
        cost+=std::hypot(route.b.x-route.a.x,route.b.y-route.a.y)*1e-8;
        if(cost>=best)return;
      }
      if(cost<best){best=cost;destination=p;}
    };
    evaluate(original[n]);evaluate(mean);
    if(patch.size()<=240)for(int member:patch)evaluate(original[member]);
    else for(int i=0;i<128;i++)evaluate(original[patch[pick(patch.size())]]);
    for(int i=0;i<samples;i++) {
      if(i%64==0&&expired())break;
      Point p=original[n];
      int mode=pick(8);
      if(mode<=2&&!lines.empty()) {
        const auto a=lines[pick(lines.size())],b=lines[pick(lines.size())];
        double det=a.d.x*b.d.y-a.d.y*b.d.x;
        if(std::abs(det)<1e-7)continue;
        double dx=b.a.x-a.a.x,dy=b.a.y-a.a.y;
        double along=(dx*b.d.y-dy*b.d.x)/det;
        p={a.a.x+along*a.d.x,a.a.y+along*a.d.y};
      } else if(mode==3)p=mean;
      else if(mode==4&&!anchors.empty())p=anchors[pick(anchors.size())];
      else if(mode==5&&!anchors.empty()) {
        auto a=anchors[pick(anchors.size())],b=anchors[pick(anchors.size())];
        double mix=unit(rng);p={a.x*mix+b.x*(1-mix),a.y*mix+b.y*(1-mix)};
      } else if(mode==6)p={s.width*unit(rng),s.height*unit(rng)};
      double radius=std::pow(10.,1.+unit(rng)*3.),angle=unit(rng)*6.283185307179586;
      evaluate({p.x+radius*std::cos(angle),p.y+radius*std::sin(angle)});
    }
    if(best==1e100){success=false;break;}
    s.pos[n]=destination;present[n]=true;
    for(int e:edges){s.routes[e]=s.route(e);live[e]=present[s.edges[e].s]&&present[s.edges[e].t];}
  }
  result.clear();
  for(int n:patch){if(!present[n])success=false;result.push_back(s.pos[n]);}
  s.pos=original;s.allRoutes();
  return success;
}

Score reconstructPeeledShell(State& s, int rounds, int samples, uint64_t seed,
                            double lookahead,
                            const std::function<bool()>& expired,
                            const std::function<double()>& elapsed,
                            const std::string& output) {
  std::mt19937_64 rng(seed);
  auto current=s.full();
  for(int round=0;round<rounds&&!expired();round++) {
    std::vector<int> degree(s.nodes.size()),removable,peeled;
    std::vector<char> removed(s.nodes.size(),false);
    for(int n=0;n<int(s.nodes.size());n++) {
      degree[n]=s.adj[n].size();
      if(degree[n]<3)removable.push_back(n);
    }
    while(!removable.empty()) {
      int index=int(rng()%removable.size()),n=removable[index];
      removable[index]=removable.back();removable.pop_back();
      if(removed[n])continue;
      removed[n]=true;peeled.push_back(n);
      for(int other:s.adj[n])if(!removed[other]&&--degree[other]==2)removable.push_back(other);
    }
    std::reverse(peeled.begin(),peeled.end());
    std::vector<Point> rebuilt;
    if(peeled.empty()||!reconstructPatch(s,peeled,samples,rng,1.,expired,rebuilt,true,lookahead))break;
    const auto old=s.pos;
    for(size_t i=0;i<peeled.size();i++)s.pos[peeled[i]]=rebuilt[i];
    s.allRoutes();const auto candidate=s.full();
    std::cerr<<"shell round="<<round<<" core="<<s.nodes.size()-peeled.size()<<" rebuilt="<<peeled.size()<<" candidate="<<candidate.visual()<<" accepted="<<better(candidate,current)<<'\n';
    if(better(candidate,current)){current=candidate;s.save(output);}
    else{s.pos=old;s.allRoutes();}
    report("shell",round,current,elapsed());
  }
  return current;
}
}
