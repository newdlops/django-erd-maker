// Included after State: a bounded large-neighborhood search over real nodes.
// A patch may explore worse states; only its best complete scene is retained.
#include <functional>
#include <unordered_set>
#include "constrained_patch_reinsertion.h"

namespace {
struct NeighborhoodOptions {
  int rounds = 0;
  int steps = 10000;
  int size = 60;
  double temperature = 8;
  double hitWeight = 1;
  bool reorder = true;
  bool destroy = true;
  bool assignment = false;
  int starCandidates = 0;
  int reinsertCandidates = 0;
  uint64_t seed = 42;
};

class NeighborhoodSearch {
  State& s;
  NeighborhoodOptions options;
  std::mt19937_64 rng;
  std::uniform_real_distribution<double> unit{0, 1};
  std::vector<long> pressure;
  std::vector<std::pair<int, int>> crossingPairs, nodeHits;
  std::vector<char> inPatch;
  Score current, patchBest;
  std::vector<Point> patchBestPositions;
  long attempts = 0, accepted = 0, orderAccepted = 0, assignmentAccepted = 0, patchWins = 0;
  long starEvaluations = 0, starWins = 0;
  long reinsertionAccepted = 0;
  int pick(int count) { return int(rng() % uint64_t(count)); }
  double uniform() { return unit(rng); }
  double distance(Point a, Point b) { return std::hypot(a.x-b.x, a.y-b.y); }

  void refreshConflicts() {
    const auto audited = s.full(&pressure);
    if (!equal(audited, current)) throw std::runtime_error("neighborhood score drift");
    crossingPairs.clear();
    nodeHits.clear();
    for (int e = 0; e < int(s.edges.size()); e++) {
      for (int other = e+1; other < int(s.edges.size()); other++) {
        if (crosses(s.routes[e], s.routes[other])) crossingPairs.emplace_back(e, other);
      }
      for (int n = 0; n < int(s.nodes.size()); n++) {
        if (n != s.edges[e].s && n != s.edges[e].t && hits(s.routes[e], s.pos[n], s.nodes[n])) nodeHits.emplace_back(e, n);
      }
    }
  }

  void remember() {
    if (better(current, patchBest)) {
      patchBest = current;
      patchBestPositions = s.pos;
    }
  }

  // Atomic exact evaluation, including every edge outside the selected patch.
  bool move(const std::vector<int>& ns, const std::vector<Point>& proposed,
            double temperature, bool force = false) {
    attempts++;
    std::vector<int> es;
    std::vector<Point> old;
    for (int n : ns) {
      if (s.moved[n]) throw std::runtime_error("duplicate neighborhood node");
      s.moved[n] = true;
      old.push_back(s.pos[n]);
      for (int e : s.incident[n]) if (!s.changedEdge[e]) {
        s.changedEdge[e] = true;
        es.push_back(e);
      }
    }
    bool valid = true;
    for (size_t i = 0; i < ns.size(); i++) {
      if (!s.inside(ns[i], proposed[i])) valid = false;
      s.pos[ns[i]] = proposed[i];
    }
    if (valid) for (int n : ns) {
      for (int other = 0; other < int(s.nodes.size()); other++) {
        if (n != other && (!s.moved[other] || n < other) && s.pair(n, other).spacing) {
          valid = false;
          break;
        }
      }
      if (!valid) break;
    }
    for (size_t i = 0; i < ns.size(); i++) s.pos[ns[i]] = old[i];
    bool committed = false;
    if (valid) {
      const auto before = s.local(ns, es);
      for (size_t i = 0; i < ns.size(); i++) s.pos[ns[i]] = proposed[i];
      s.update(es);
      const auto next = current + s.local(ns, es) - before;
      const double delta = next.cost(options.hitWeight) - current.cost(options.hitWeight);
      if (delta <= 0 || (temperature > 0 && uniform() < std::exp(-delta / temperature))
          || (force && next.visual() <= patchBest.visual() + std::max(50., patchBest.visual()*.15))) {
        current = next;
        committed = true;
        accepted++;
        remember();
      } else {
        for (size_t i = 0; i < ns.size(); i++) s.pos[ns[i]] = old[i];
        s.update(es);
      }
    }
    for (int n : ns) s.moved[n] = false;
    for (int e : es) s.changedEdge[e] = false;
    return committed;
  }

  // Reordering slots changes which card occupies a slot. Repair with actual
  // card sizes; no rectangle may be shrunk, dropped, or moved outside canvas.
  bool repairSlots(const std::vector<int>& ns, std::vector<Point>& proposed) {
    const auto old = s.pos;
    std::vector<char> unplaced(s.nodes.size(), false);
    for (int n : ns) unplaced[n] = true;
    std::vector<int> order(ns.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
      return s.nodes[ns[a]].w*s.nodes[ns[a]].h > s.nodes[ns[b]].w*s.nodes[ns[b]].h;
    });
    bool valid = true;
    for (int index : order) {
      int n = ns[index];
      const auto wanted = proposed[index];
      const auto free = [&](Point p) {
        if (!s.inside(n, p)) return false;
        s.pos[n] = p;
        for (int other = 0; other < int(s.nodes.size()); other++) {
          if (n != other && !unplaced[other] && s.pair(n, other).spacing) return false;
        }
        return true;
      };
      bool found = free(wanted);
      for (int ring = 1; !found && ring <= 12; ring++) {
        for (int sample = 0; !found && sample < 24; sample++) {
          double angle = sample * 6.283185307179586 / 24;
          Point p{wanted.x+ring*65*std::cos(angle), wanted.y+ring*65*std::sin(angle)};
          if (free(p)) { proposed[index] = p; found = true; }
        }
      }
      if (!found) { valid = false; break; }
      unplaced[n] = false;
    }
    s.pos = old;
    return valid;
  }

  std::pair<int, std::vector<int>> choosePatch(int round) {
    int root = pick(s.nodes.size());
    for (int i = 0; i < 30; i++) {
      int n = pick(s.nodes.size());
      if (pressure[n] > pressure[root]) root = n;
    }
    std::vector<double> priority(s.nodes.size(), 0);
    priority[root] = 1e12;
    for (int n : s.adj[root]) {
      priority[n] += 500 + pressure[n]*2;
      for (int other : s.adj[n]) priority[other] += 20 + std::sqrt(double(pressure[other]));
    }
    for (auto [e, n] : nodeHits) {
      if (s.edges[e].s == root || s.edges[e].t == root || n == root) {
        priority[n] += 250;
        priority[s.edges[e].s] += 100;
        priority[s.edges[e].t] += 100;
      }
    }
    if (round%3 && !crossingPairs.empty()) {
      auto pair = crossingPairs[pick(crossingPairs.size())];
      for (int e : {pair.first, pair.second}) for (int n : {s.edges[e].s, s.edges[e].t}) {
        priority[n] += 1e10;
        for (int other : s.adj[n]) priority[other] += 500 + pressure[other];
      }
    }
    // Some variation prevents the largest hub from choosing the same 60
    // neighbors every round while its remaining incident nodes never move.
    for (int n = 0; n < int(s.nodes.size()); n++) if (priority[n] > 0 && n != root) priority[n] *= .5 + uniform();
    std::vector<int> patch(s.nodes.size());
    std::iota(patch.begin(), patch.end(), 0);
    std::stable_sort(patch.begin(), patch.end(), [&](int a, int b) {return priority[a] > priority[b];});
    patch.resize(std::min(options.size, int(patch.size())));
    inPatch.assign(s.nodes.size(), false);
    for (int n : patch) inPatch[n] = true;
    return {root, patch};
  }

  void reorderNeighbors(int root, int round) {
    std::vector<int> neighbors;
    for (int n : s.adj[root]) if (inPatch[n]) neighbors.push_back(n);
    if (neighbors.size() < 3) return;
    const auto center = s.pos[root];
    auto desired = neighbors;
    const auto destinationAngle = [&](int n) {
      Point target{}; double weight = 0;
      for (int other : s.adj[n]) if (other != root) {
        target.x += s.pos[other].x; target.y += s.pos[other].y; weight++;
      }
      if (weight) {target.x/=weight;target.y/=weight;} else target = s.pos[n];
      return std::atan2(target.y-center.y, target.x-center.x);
    };
    std::sort(neighbors.begin(), neighbors.end(), [&](int a, int b) {
      return std::atan2(s.pos[a].y-center.y, s.pos[a].x-center.x) < std::atan2(s.pos[b].y-center.y, s.pos[b].x-center.x);
    });
    std::stable_sort(desired.begin(), desired.end(), [&](int a, int b) {return destinationAngle(a) < destinationAngle(b);});
    std::vector<Point> slots;
    for (int n : neighbors) slots.push_back(s.pos[n]);
    const int trials = std::min(12, int(slots.size()));
    for (int trial = 0; trial < trials; trial++) {
      std::vector<Point> proposed;
      int shift = trial == 0 ? 0 : pick(slots.size());
      for (int i = 0; i < int(slots.size()); i++) proposed.push_back(slots[(i+shift)%slots.size()]);
      if (!repairSlots(desired, proposed)) continue;
      if (move(desired, proposed, 0, options.destroy && trial == trials-1 && round%2)) orderAccepted++;
    }
  }

  // Minimum-cost rectangular assignment. This solves only the separable
  // proposal cost; interactions between assigned nodes are audited by move().
  static std::vector<int> assignment(const std::vector<std::vector<double>>& cost) {
    const int rows=cost.size(),columns=cost[0].size();
    std::vector<double> rowPotential(rows+1),columnPotential(columns+1);
    std::vector<int> owner(columns+1),previous(columns+1);
    for(int row=1;row<=rows;row++) {
      owner[0]=row;int column=0;
      std::vector<double> slack(columns+1,1e100);
      std::vector<char> visited(columns+1,false);
      do {
        visited[column]=true;
        int active=owner[column],next=0;double delta=1e100;
        for(int j=1;j<=columns;j++)if(!visited[j]) {
          const double reduced=cost[active-1][j-1]-rowPotential[active]-columnPotential[j];
          if(reduced<slack[j]){slack[j]=reduced;previous[j]=column;}
          if(slack[j]<delta){delta=slack[j];next=j;}
        }
        for(int j=0;j<=columns;j++) {
          if(visited[j]){rowPotential[owner[j]]+=delta;columnPotential[j]-=delta;}
          else slack[j]-=delta;
        }
        column=next;
      }while(owner[column]!=0);
      do {int prior=previous[column];owner[column]=owner[prior];column=prior;}while(column);
    }
    std::vector<int> result(rows,-1);
    for(int j=1;j<=columns;j++)if(owner[j])result[owner[j]-1]=j-1;
    return result;
  }

  void assignPatch(int root,const std::vector<int>& patch,int round) {
    std::vector<int> ns;
    std::vector<char> active(s.nodes.size(),false),fixedEdge(s.edges.size(),true);
    for(int n:patch)if(n!=root){ns.push_back(n);active[n]=true;}
    if(ns.size()<2)return;
    for(int e=0;e<int(s.edges.size());e++)fixedEdge[e]=!active[s.edges[e].s]&&!active[s.edges[e].t];
    std::vector<Point> slots;
    for(int n:ns)slots.push_back(s.pos[n]);
    const auto center=s.pos[root];
    // Existing slots provide a feasible fallback. Additional slots offer room
    // to shorten branches, with each actual rectangle checked against fixed cards.
    for(int ring=1;ring<=4;ring++)for(int angle=0;angle<16;angle++) {
      double theta=angle*6.283185307179586/16;
      double radius=500*std::pow(1.8,ring);
      slots.push_back({center.x+radius*std::cos(theta),center.y+radius*std::sin(theta)});
    }
    std::vector<std::vector<double>> costs(ns.size(),std::vector<double>(slots.size()));
    for(int i=0;i<int(ns.size());i++)for(int j=0;j<int(slots.size());j++) {
      int n=ns[i];const auto p=slots[j];double cost=0;
      if(!s.inside(n,p)){costs[i][j]=1e9;continue;}
      bool collision=false;
      for(int other=0;other<int(s.nodes.size());other++)if(!active[other]) {
        double dx=std::abs(p.x-s.pos[other].x)-(s.nodes[n].w+s.nodes[other].w)*.5;
        double dy=std::abs(p.y-s.pos[other].y)-(s.nodes[n].h+s.nodes[other].h)*.5;
        if(dx<55.99&&dy<41.99){collision=true;break;}
      }
      if(collision){costs[i][j]=1e9;continue;}
      for(int e=0;e<int(s.edges.size());e++)if(fixedEdge[e])cost+=options.hitWeight*hits(s.routes[e],p,s.nodes[n]);
      for(int e:s.incident[n]) {
        const auto& edge=s.edges[e];int other=edge.s==n?edge.t:edge.s;
        if(active[other])continue;
        const auto route=segment(port(p,s.nodes[n],s.pos[other]),port(s.pos[other],s.nodes[other],p));
        for(int f=0;f<int(s.edges.size());f++)if(fixedEdge[f])cost+=crosses(route,s.routes[f]);
        for(int obstacle=0;obstacle<int(s.nodes.size());obstacle++)if(!active[obstacle]&&obstacle!=other)cost+=options.hitWeight*hits(route,s.pos[obstacle],s.nodes[obstacle]);
      }
      costs[i][j]=cost+distance(p,s.pos[n])*1e-7;
    }
    auto placement=assignment(costs);
    std::vector<Point> proposed;
    for(int i=0;i<int(ns.size());i++){
      if(placement[i]<0||costs[i][placement[i]]>=1e9)return;
      proposed.push_back(slots[placement[i]]);
    }
    if(repairSlots(ns,proposed)&&move(ns,proposed,0,options.destroy&&round%2))assignmentAccepted++;
  }

  // Crossings change when a moving star passes a line through a fixed
  // neighbor and an endpoint of a conflicting edge. Sample those boundaries,
  // their intersections, and obstacle tangents, then evaluate the real scene.
  // Card clipping means this is a proposal generator, not an exact arrangement.
  void reinsertStar(int n, const std::function<bool()>& expired) {
    if (s.incident[n].empty()) return;
    struct Line { Point origin, direction; };
    std::vector<Line> lines;
    const auto old=s.pos[n];
    const auto addLine=[&](Point a,Point b) {
      const double length=distance(a,b);
      if(length>1e-6)lines.push_back({a,{(b.x-a.x)/length,(b.y-a.y)/length}});
    };
    for(int e:s.incident[n])s.changedEdge[e]=true;
    s.moved[n]=true;
    const auto& es=s.incident[n];
    for(int e:es) {
      int other=s.edges[e].s==n?s.edges[e].t:s.edges[e].s;
      const auto origin=s.pos[other];
      for(int f=0;f<int(s.edges.size());f++)if(!s.changedEdge[f]&&crosses(s.routes[e],s.routes[f])) {
        addLine(origin,s.routes[f].a);addLine(origin,s.routes[f].b);
      }
      for(int obstacle=0;obstacle<int(s.nodes.size());obstacle++)if(obstacle!=n&&obstacle!=other&&hits(s.routes[e],s.pos[obstacle],s.nodes[obstacle])) {
        for(int x:{-1,1})for(int y:{-1,1})addLine(origin,{s.pos[obstacle].x+x*(s.nodes[obstacle].w*.5+10),s.pos[obstacle].y+y*(s.nodes[obstacle].h*.5+10)});
      }
      // A small background sample also permits moves outside the cells
      // represented by conflicts at the current position.
      for(int i=0;i<3;i++) {
        const auto& background=s.routes[pick(s.routes.size())];
        addLine(origin,background.a);addLine(origin,background.b);
      }
    }
    for(int e=0;e<int(s.edges.size());e++)if(!s.changedEdge[e]&&hits(s.routes[e],old,s.nodes[n])) {
      auto a=s.routes[e].a,b=s.routes[e].b;
      double length=distance(a,b);if(length<1e-6)continue;
      Point normal{-(b.y-a.y)/length,(b.x-a.x)/length};
      double clearance=(std::abs(normal.x)*s.nodes[n].w+std::abs(normal.y)*s.nodes[n].h)*.5+11;
      for(int sign:{-1,1})addLine({a.x+normal.x*clearance*sign,a.y+normal.y*clearance*sign},{b.x+normal.x*clearance*sign,b.y+normal.y*clearance*sign});
    }
    const auto before=s.local({n},es);
    auto best=current;
    auto destination=old;
    const auto evaluate=[&](Point p) {
      if(!s.inside(n,p))return;
      s.pos[n]=p;
      for(int other=0;other<int(s.nodes.size());other++)if(other!=n&&s.pair(n,other).spacing)return;
      s.update(es);starEvaluations++;
      const auto next=current+s.local({n},es)-before;
      if(better(next,best)){best=next;destination=p;}
    };
    const int count=std::max(128,int(options.starCandidates/std::sqrt(1.+es.size()*.05)));
    for(int i=0;i<count&&!lines.empty();i++) {
      if(i%64==0&&expired())break;
      const auto& a=lines[pick(lines.size())];
      Point candidate;
      if(i%3==0) {
        double along=(old.x-a.origin.x)*a.direction.x+(old.y-a.origin.y)*a.direction.y;
        candidate={a.origin.x+along*a.direction.x,a.origin.y+along*a.direction.y};
      } else {
        const auto& b=lines[pick(lines.size())];
        double det=a.direction.x*b.direction.y-a.direction.y*b.direction.x;
        if(std::abs(det)<1e-7)continue;
        double dx=b.origin.x-a.origin.x,dy=b.origin.y-a.origin.y;
        double along=(dx*b.direction.y-dy*b.direction.x)/det;
        candidate={a.origin.x+along*a.direction.x,a.origin.y+along*a.direction.y};
      }
      double epsilon=i%5==0?1.:std::pow(10.,uniform()*2.8);
      double angle=uniform()*6.283185307179586;
      evaluate({candidate.x+epsilon*std::cos(angle),candidate.y+epsilon*std::sin(angle)});
    }
    s.pos[n]=destination;s.update(es);
    s.moved[n]=false;
    for(int e:es)s.changedEdge[e]=false;
    if(better(best,current)){current=best;remember();starWins++;}
  }

  void step(const std::vector<int>& patch, double temperature) {
    int n = patch[pick(patch.size())];
    if (uniform() < .5) for (int i=0;i<2;i++) {int other=patch[pick(patch.size())];if(pressure[other]>pressure[n])n=other;}
    int mode = pick(10);
    if (mode < 2 && !crossingPairs.empty()) {
      for (int attempt=0; attempt<24; attempt++) {
        auto pair = crossingPairs[pick(crossingPairs.size())];
        if (!crosses(s.routes[pair.first], s.routes[pair.second])) continue;
        const auto& a = s.edges[pair.first]; const auto& b = s.edges[pair.second];
        int left = uniform()<.5?a.s:a.t, right = uniform()<.5?b.s:b.t;
        if (left==right || !inPatch[left] || !inPatch[right]) continue;
        move({left,right}, {s.pos[right],s.pos[left]}, temperature);
        return;
      }
    }
    if (mode == 2 && !nodeHits.empty()) {
      for (int attempt=0; attempt<24; attempt++) {
        auto [e, blocker] = nodeHits[pick(nodeHits.size())];
        if (!inPatch[blocker] || !hits(s.routes[e],s.pos[blocker],s.nodes[blocker])) continue;
        const auto& line=s.routes[e];const auto p=s.pos[blocker];
        double dx=line.b.x-line.a.x,dy=line.b.y-line.a.y,len=std::hypot(dx,dy);
        if (len < 1e-9) continue;
        Point normal{-dy/len,dx/len};
        double signedDistance=(p.x-line.a.x)*normal.x+(p.y-line.a.y)*normal.y;
        double clear=(std::abs(normal.x)*s.nodes[blocker].w+std::abs(normal.y)*s.nodes[blocker].h)*.5+15+uniform()*100;
        clear*=uniform()<.7?(signedDistance>=0?1:-1):(signedDistance>=0?-1:1);
        move({blocker},{{p.x+normal.x*(clear-signedDistance),p.y+normal.y*(clear-signedDistance)}},temperature);
        return;
      }
    }
    if (mode == 3) {
      int other=patch[pick(patch.size())];
      if(n!=other)move({n,other},{s.pos[other],s.pos[n]},temperature);
      return;
    }
    const double radius=std::pow(10.,1.15+uniform()*2.9),angle=uniform()*6.283185307179586;
    Point offset{radius*std::cos(angle),radius*std::sin(angle)},target=s.pos[n];
    if (!s.adj[n].empty() && mode <= 5) {
      target={};for(int other:s.adj[n]){target.x+=s.pos[other].x;target.y+=s.pos[other].y;}
      target.x/=s.adj[n].size();target.y/=s.adj[n].size();
    } else if (!s.adj[n].empty() && mode==6) target=s.pos[s.adj[n][pick(s.adj[n].size())]];
    else if (!s.adj[n].empty() && mode==7) {
      auto a=s.pos[s.adj[n][pick(s.adj[n].size())]], b=s.pos[s.adj[n][pick(s.adj[n].size())]];
      double mix=uniform();target={a.x*mix+b.x*(1-mix),a.y*mix+b.y*(1-mix)};
    }
    move({n},{{target.x+offset.x,target.y+offset.y}},temperature);
  }

public:
  NeighborhoodSearch(State& state, NeighborhoodOptions configuration)
    : s(state), options(configuration), rng(options.seed), current(s.full()) {}

  Score run(const std::function<bool()>& expired, const std::function<double()>& elapsed,
            const std::string& output) {
    for (int round=0; round<options.rounds && !expired(); round++) {
      refreshConflicts();
      auto [root, patch]=choosePatch(round);
      const Score before=current;
      patchBest=current;patchBestPositions=s.pos;
      if (options.reorder) reorderNeighbors(root, round);
      if (options.assignment) assignPatch(root,patch,round);
      if (options.reinsertCandidates) {
        std::vector<Point> rebuilt;
        if(reconstructPatch(s,patch,options.reinsertCandidates,rng,options.hitWeight,expired,rebuilt)
            &&move(patch,rebuilt,0,options.destroy&&round%2))reinsertionAccepted++;
      }
      if (options.starCandidates) {
        auto stars=patch;
        std::stable_sort(stars.begin(),stars.end(),[&](int a,int b){
          return pressure[a]/std::sqrt(1.+s.adj[a].size())>pressure[b]/std::sqrt(1.+s.adj[b].size());
        });
        reinsertStar(root,expired);
        for(int i=0;i<std::min(6,int(stars.size()))&&!expired();i++)if(stars[i]!=root)reinsertStar(stars[i],expired);
      }
      for (int i=0;i<options.steps;i++) {
        if(i%128==0 && expired())break;
        const double t=options.temperature*std::pow(.02/options.temperature,double(i)/options.steps);
        step(patch,t);
        if(i%2000==1999)refreshConflicts();
      }
      s.pos=patchBestPositions;s.allRoutes();current=patchBest;
      if(!equal(s.full(),current))throw std::runtime_error("neighborhood final score drift");
      if(better(current,before))patchWins++;
      std::cerr<<"patch="<<round<<" root="<<s.nodes[root].id<<" size="<<patch.size()<<" gain="<<before.visual()-current.visual()<<'\n';
      report("neighborhood",round,current,elapsed());
      s.save(output);
      if(current.visual()<=500 && current.hit==0)break;
    }
    std::cerr<<"neighborhood attempts="<<attempts<<" accepted="<<accepted<<" orderAccepted="<<orderAccepted<<" assignmentAccepted="<<assignmentAccepted<<" reinsertionAccepted="<<reinsertionAccepted<<" starEvaluations="<<starEvaluations<<" starWins="<<starWins<<" patchWins="<<patchWins<<'\n';
    return current;
  }
};
}
