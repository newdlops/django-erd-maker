// Exact quadratic assignment of independent real cards to candidate positions.
// Every relation has at most one moving endpoint, so its conflicts decompose
// into unary and pairwise terms. A bijection permits simultaneous card swaps;
// pair terms are evaluated lazily instead of allocating an O(k^4) table.
namespace {
struct SlotTerm {
  Score score;
  long invalid=0;
  double energy() const {return score.cost(1)+invalid*10000.;}
  bool feasible() const {return !invalid&&!score.overlap&&!score.spacing;}
};
SlotTerm operator+(SlotTerm a,SlotTerm b){return {a.score+b.score,a.invalid+b.invalid};}
SlotTerm operator-(SlotTerm a,SlotTerm b){return {a.score-b.score,a.invalid-b.invalid};}
bool equal(SlotTerm a,SlotTerm b){return equal(a.score,b.score)&&a.invalid==b.invalid;}
Score slotCardPair(Point a,const Node& an,Point b,const Node& bn) {
  double dx=std::abs(a.x-b.x)-(an.w+bn.w)*.5,dy=std::abs(a.y-b.y)-(an.h+bn.h)*.5;
  return {0,0,dx<0&&dy<0,dx<55.99&&dy<41.99};
}

class SlotAssignmentModel {
  State& s;
  std::vector<char> active;
  std::vector<int> fixedEdges,fixedNodes;
  struct Candidate {Point position;std::vector<Segment> routes;};
  std::vector<std::vector<Candidate>> candidates;
  std::vector<std::vector<SlotTerm>> unary;
  std::vector<Point> slots;
  std::vector<int> owners;
  std::unordered_map<uint32_t,SlotTerm> pairs;
  const size_t cacheLimit=250000;
  bool adjacent(int e,int f) const {
    const auto& a=s.edges[e];const auto& b=s.edges[f];
    return a.s==b.s||a.s==b.t||a.t==b.s||a.t==b.t;
  }
  SlotTerm unaryCost(int n,const Candidate& candidate) const {
    SlotTerm value;auto p=candidate.position;
    value.invalid+=!s.inside(n,p);
    for(int other:fixedNodes)value.score=value.score+slotCardPair(p,s.nodes[n],s.pos[other],s.nodes[other]);
    for(int e:fixedEdges)value.score.hit+=hits(s.routes[e],p,s.nodes[n]);
    for(size_t i=0;i<s.incident[n].size();i++) {
      int e=s.incident[n][i];const auto& edge=s.edges[e];const auto& line=candidate.routes[i];
      int other=edge.s==n?edge.t:edge.s;
      value.invalid+=hits(line,p,s.nodes[n],-.02)||hits(line,s.pos[other],s.nodes[other],-.02);
      for(int f:fixedEdges)if(crosses(line,s.routes[f])){value.score.cross++;value.invalid+=adjacent(e,f);}
      for(int card:fixedNodes)if(card!=other)value.score.hit+=hits(line,s.pos[card],s.nodes[card]);
      for(size_t j=i+1;j<candidate.routes.size();j++)if(crosses(line,candidate.routes[j])){value.score.cross++;value.invalid++;}
    }
    return value;
  }
  SlotTerm pairCost(int i,int a,int j,int b) const {
    int n=members[i],m=members[j];const auto& first=candidates[i][a];const auto& second=candidates[j][b];
    SlotTerm value{slotCardPair(first.position,s.nodes[n],second.position,s.nodes[m]),0};
    for(size_t x=0;x<first.routes.size();x++) {
      value.score.hit+=hits(first.routes[x],second.position,s.nodes[m]);
      for(size_t y=0;y<second.routes.size();y++)if(crosses(first.routes[x],second.routes[y])) {
        value.score.cross++;value.invalid+=adjacent(s.incident[n][x],s.incident[m][y]);
      }
    }
    for(const auto& line:second.routes)value.score.hit+=hits(line,first.position,s.nodes[n]);
    return value;
  }
public:
  std::vector<int> members;
  SlotAssignmentModel(State& state,std::vector<int> moving,const std::vector<Point>& extra={}):s(state),active(s.nodes.size(),false),members(std::move(moving)) {
    if(members.size()<2||members.size()>128)throw std::runtime_error("slot assignment requires 2..128 cards");
    for(int n:members){if(active[n])throw std::runtime_error("duplicate slot card");active[n]=true;}
    for(int n:members){slots.push_back(s.pos[n]);owners.push_back(n);}
    for(auto point:extra){if(!std::isfinite(point.x)||!std::isfinite(point.y))throw std::runtime_error("invalid free slot");slots.push_back(point);owners.push_back(-1);}
    if(slots.size()>256)throw std::runtime_error("slot candidate limit exceeded");
    for(const auto& edge:s.edges)if(active[edge.s]&&active[edge.t])throw std::runtime_error("slot cards must be an independent set");
    for(int n=0;n<int(s.nodes.size());n++)if(!active[n])fixedNodes.push_back(n);
    for(int e=0;e<int(s.edges.size());e++)if(!active[s.edges[e].s]&&!active[s.edges[e].t])fixedEdges.push_back(e);
    pairs.reserve(cacheLimit);
  }
  bool build(const std::function<bool()>& expired) {
    candidates.resize(members.size());unary.resize(members.size());
    for(int i=0;i<int(members.size());i++) {
      int n=members[i];
      for(int j=0;j<int(slots.size());j++) {
        if(expired())return false;
        int owner=owners[j];Candidate candidate;candidate.position=slots[j];
        for(int e:s.incident[n]) {
          if(i==j){candidate.routes.push_back(s.routes[e]);continue;}
          const auto& edge=s.edges[e];int other=edge.s==n?edge.t:edge.s;
          auto fixed=port(s.pos[other],s.nodes[other],candidate.position);
          // A shared hub lends the original slot's endpoint. This preserves
          // its fan order while changing which real card occupies the slot.
          if(owner>=0)for(int f:s.incident[owner])if(s.edges[f].s==other||s.edges[f].t==other) {
            fixed=s.edges[f].s==other?s.routes[f].a:s.routes[f].b;break;
          }
          auto moving=port(candidate.position,s.nodes[n],fixed);
          auto line=edge.s==n?segment(moving,fixed):segment(fixed,moving);
          if(hits(line,s.pos[other],s.nodes[other],-.02)) {
            fixed=port(s.pos[other],s.nodes[other],candidate.position);
            moving=port(candidate.position,s.nodes[n],fixed);
            line=edge.s==n?segment(moving,fixed):segment(fixed,moving);
          }
          candidate.routes.push_back(line);
        }
        unary[i].push_back(unaryCost(n,candidate));candidates[i].push_back(std::move(candidate));
      }
    }
    return true;
  }
  SlotTerm pair(int i,int a,int j,int b) {
    if(i>j){std::swap(i,j);std::swap(a,b);}
    uint32_t key=uint32_t(i)|(uint32_t(a)<<8)|(uint32_t(j)<<16)|(uint32_t(b)<<24);
    const auto found=pairs.find(key);if(found!=pairs.end())return found->second;
    auto value=pairCost(i,a,j,b);if(pairs.size()<cacheLimit)pairs.emplace(key,value);return value;
  }
  SlotTerm full(const std::vector<int>& labels) {
    SlotTerm value;
    for(int i=0;i<int(labels.size());i++) {
      value=value+unary[i][labels[i]];
      for(int j=i+1;j<int(labels.size());j++)value=value+pair(i,labels[i],j,labels[j]);
    }
    return value;
  }
  SlotTerm local(const std::vector<int>& changing,const std::vector<int>& labels) {
    SlotTerm value;
    for(int i:changing) {
      value=value+unary[i][labels[i]];
      for(int j=0;j<int(labels.size());j++)if(i!=j&&(i<j||std::find(changing.begin(),changing.end(),j)==changing.end()))
        value=value+pair(i,labels[i],j,labels[j]);
    }
    return value;
  }
  void apply(const std::vector<int>& labels) {
    std::vector<char> used(slots.size(),false);
    for(int i=0;i<int(labels.size());i++) {
      int label=labels[i];if(label<0||label>=int(slots.size())||used[label])throw std::runtime_error("slot labels must be distinct");
      used[label]=true;int n=members[i];const auto& candidate=candidates[i][label];s.pos[n]=candidate.position;
      for(size_t j=0;j<s.incident[n].size();j++)s.routes[s.incident[n][j]]=candidate.routes[j];
    }
  }
  size_t cacheSize() const {return pairs.size();}
  int slotCount() const {return int(slots.size());}
};

struct SlotSolution {
  std::vector<int> labels;SlotTerm value;long completed=0,accepted=0,cycles=0;
  std::vector<int> trialLabels{};SlotTerm trialValue{};
};
SlotSolution solveSlotAssignment(SlotAssignmentModel& model,long steps,double temperature,uint64_t seed,
                                 const std::function<bool()>& expired,double polishSlack=0) {
  std::mt19937_64 rng(seed);std::uniform_real_distribution<double> unit(0,1);
  std::vector<int> labels(model.members.size());std::iota(labels.begin(),labels.end(),0);
  auto current=model.full(labels);if(!current.feasible())throw std::runtime_error("initial slot assignment is not feasible");
  const auto identity=labels;const long initialVisual=current.score.visual();
  SlotSolution best{labels,current};const long cycle=std::max(1L,steps/4);
  for(long step=0;step<steps;step++) {
    if(step%64==0&&expired())break;
    if(step&&step%cycle==0){labels=best.labels;current=best.value;}
    bool vacant=model.slotCount()>int(labels.size())&&unit(rng)<.4;
    int count=vacant?1:2+(labels.size()>2&&unit(rng)<.25)+(labels.size()>3&&unit(rng)<.1);
    std::vector<int> changing;
    while(int(changing.size())<count){int n=int(rng()%labels.size());if(std::find(changing.begin(),changing.end(),n)==changing.end())changing.push_back(n);}
    const auto before=model.local(changing,labels);auto previous=labels;
    if(vacant) {
      int target;
      do {target=int(rng()%model.slotCount());}while(std::find(labels.begin(),labels.end(),target)!=labels.end());
      labels[changing[0]]=target;
    } else for(int i=0;i<count;i++)labels[changing[i]]=previous[changing[(i+1)%count]];
    const auto next=current+model.local(changing,labels)-before;
    const double progress=double(step%cycle)/cycle,t=temperature*std::pow(.015/temperature,progress);
    double delta=next.energy()-current.energy();
    if(delta<=0||unit(rng)<std::exp(-delta/t)){current=next;best.accepted++;best.cycles+=count>2;}
    else labels=std::move(previous);
    if(current.feasible()&&better(current.score,best.value.score)){best.labels=labels;best.value=current;}
    if(polishSlack>0&&current.feasible()&&labels!=identity&&current.score.visual()<=initialVisual+polishSlack
      &&(best.trialLabels.empty()||better(current.score,best.trialValue.score))) {
      best.trialLabels=labels;best.trialValue=current;
    }
    best.completed=step+1;
    if(step%2048==0&&!equal(current,model.full(labels)))throw std::runtime_error("slot assignment energy drift");
  }
  if(!equal(best.value,model.full(best.labels)))throw std::runtime_error("slot assignment best drift");
  return best;
}

std::vector<Point> proposeFreeSlots(const State& s,const std::vector<int>& moving,int count,std::mt19937_64& rng) {
  std::vector<Point> points;std::uniform_real_distribution<double> unit(0,1);
  for(int i=0;i<count;i++) {
    int n=moving[i%moving.size()];Point p=s.pos[n];
    if(i%3!=2) {
      int start=int(rng()%s.edges.size());
      for(int j=0;j<int(s.edges.size());j++) {
        int f=(start+j)%s.edges.size();if(s.edges[f].s==n||s.edges[f].t==n)continue;
        bool conflict=hits(s.routes[f],p,s.nodes[n]);int anchor=-1;
        if(!conflict)for(int e:s.incident[n])if(crosses(s.routes[e],s.routes[f])){conflict=true;anchor=s.edges[e].s==n?s.edges[e].t:s.edges[e].s;break;}
        if(!conflict)continue;
        const auto& line=s.routes[f];double dx=line.b.x-line.a.x,dy=line.b.y-line.a.y,len=std::hypot(dx,dy);if(len<.01)continue;
        Point normal{-dy/len,dx/len};double distance=(p.x-line.a.x)*normal.x+(p.y-line.a.y)*normal.y;
        double side=anchor<0?(unit(rng)<.5?-1:1):((s.pos[anchor].x-line.a.x)*normal.x+(s.pos[anchor].y-line.a.y)*normal.y>=0?1:-1);
        double clear=(std::abs(normal.x)*s.nodes[n].w+std::abs(normal.y)*s.nodes[n].h)*.5+20+unit(rng)*120;
        p={p.x+normal.x*(clear*side-distance),p.y+normal.y*(clear*side-distance)};break;
      }
    } else {
      double angle=unit(rng)*6.283185307179586,radius=std::pow(10.,1.3+unit(rng)*2.2);
      p={p.x+radius*std::cos(angle),p.y+radius*std::sin(angle)};
    }
    points.push_back(p);
  }
  return points;
}

Score runSlotAssignments(State& s,int argc,char** argv,const std::function<bool()>& expired,
                         const std::function<double()>& elapsed,const std::string& output) {
  int rounds=int(number(argc,argv,"--slot-rounds",0)),limit=int(number(argc,argv,"--slot-size",64));
  int extra=int(number(argc,argv,"--slot-extra",0));
  double polishSlack=number(argc,argv,"--slot-polish-slack",0);
  long steps=long(number(argc,argv,"--slot-steps",50000)),portSteps=long(number(argc,argv,"--slot-port-steps",100000));
  double temperature=number(argc,argv,"--slot-temperature",8);uint64_t seed=uint64_t(number(argc,argv,"--seed",42));
  if(rounds<0||limit<2||limit>128||extra<0||extra>128||steps<0||portSteps<0||!std::isfinite(temperature)||temperature<=0
    ||!std::isfinite(polishSlack)||polishSlack<0||polishSlack>100||!s.sourceOffsets.empty())throw std::runtime_error("invalid slot assignment options");
  std::mt19937_64 rng(seed);std::vector<long> pressure;auto best=s.full(&pressure);
  for(int round=0;round<rounds&&!expired();round++) {
    auto originalPos=s.pos;auto originalRoutes=s.routes;s.full(&pressure);
    std::vector<int> roots;for(int n=0;n<int(s.nodes.size());n++)if(s.adj[n].size()>=3)roots.push_back(n);
    std::stable_sort(roots.begin(),roots.end(),[&](int a,int b){return pressure[a]>pressure[b];});
    if(roots.empty())break;int root=roots[(round/3)%std::min(20,int(roots.size()))];
    std::vector<char> eligible(s.nodes.size(),round%3==2),blocked(s.nodes.size(),false);
    if(round%3!=2)for(int n:s.adj[root]){eligible[n]=true;if(round%3==1)for(int other:s.adj[n])eligible[other]=true;}
    eligible[root]=round%3==2;
    std::vector<int> pool;for(int n=0;n<int(s.nodes.size());n++)if(eligible[n]&&!s.adj[n].empty())pool.push_back(n);
    std::shuffle(pool.begin(),pool.end(),rng);
    std::stable_sort(pool.begin(),pool.end(),[&](int a,int b){return pressure[a]>pressure[b];});
    std::vector<int> moving;for(int n:pool)if(!blocked[n]){
      moving.push_back(n);for(int other:s.adj[n])blocked[other]=true;if(int(moving.size())==limit)break;
    }
    if(moving.size()<2)continue;
    SlotAssignmentModel model(s,moving,proposeFreeSlots(s,moving,extra,rng));if(!model.build(expired))break;
    std::vector<int> identity(moving.size());std::iota(identity.begin(),identity.end(),0);const auto before=model.full(identity);
    auto solution=solveSlotAssignment(model,steps,temperature,seed+round,expired,polishSlack);
    std::vector<std::pair<std::vector<int>,SlotTerm>> trials{{solution.labels,solution.value}};
    if(!solution.trialLabels.empty()&&solution.trialLabels!=solution.labels)trials.push_back({solution.trialLabels,solution.trialValue});
    auto retained=best;auto retainedPos=originalPos;auto retainedRoutes=originalRoutes;
    int moved=0,free=0,tested=0;auto candidate=best;
    for(const auto& trial:trials) {
      if(tested&&expired())break;
      s.pos=originalPos;s.routes=originalRoutes;model.apply(trial.first);candidate=s.full();
      if(!equal(candidate,best+trial.second.score-before.score))throw std::runtime_error("slot assignment and full scene disagree");
      moved=0;free=0;for(size_t i=0;i<moving.size();i++){moved+=trial.first[i]!=int(i);free+=trial.first[i]>=int(moving.size());}
      // A slightly worse position plan may have better boundary ports. Only
      // the fully scored, feasible result can replace the retained scene.
      if(moved&&!expired())candidate=refineBoundaryPorts(s,candidate,portSteps,rng,expired,elapsed);
      if(better(candidate,retained)){retained=candidate;retainedPos=s.pos;retainedRoutes=s.routes;}
      tested++;
    }
    bool accept=better(retained,best);best=retained;s.pos=retainedPos;s.routes=retainedRoutes;
    if(accept)s.save(output);
    std::cerr<<"slot round="<<round<<" root="<<s.nodes[root].id<<" variables="<<moving.size()<<" moved="<<moved
      <<" candidate="<<candidate.visual()<<" accepted="<<accept<<" steps="<<solution.completed<<" cache="<<model.cacheSize()<<" free="<<free<<" tested="<<tested<<'\n';
    report("slot-best",round,best,elapsed());
  }
  if(!equal(best,s.full()))throw std::runtime_error("slot result drift");s.save(output);return best;
}
}
