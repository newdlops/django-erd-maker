#include "pairwise_label_optimizer.h"
#include "constrained_factor_graph.h"

namespace {
struct JointSearchOptions {int rounds=0,size=16,labels=32,samples=500,steps=20000,leaves=0;bool connected=false,relaxSpacing=false;uint64_t seed=61;};
struct PositionLabel {Point point;int unary=0,conditional=0;std::vector<Segment> star;std::vector<Point> members;};

class JointPositionSearch {
  State& s;
  JointSearchOptions options;
  std::mt19937_64 rng;
  std::uniform_real_distribution<double> uniform{0,1};
  std::vector<int> selected;
  std::vector<char> variable,variableEdge;
  std::vector<std::vector<int>> members;
  std::vector<int> rootByMember;
  static constexpr int forbidden=1000000000;
  int pick(int size){return int(rng()%uint64_t(size));}
  double unit(){return uniform(rng);}
  double distance(Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y);}

  std::vector<Segment> star(int n,Point p) {
    const auto old=s.pos[n];s.pos[n]=p;
    std::vector<Segment> result;
    for(int e:s.incident[n])result.push_back(s.route(e));
    s.pos[n]=old;return result;
  }
  bool separated(int a,Point p,int b,Point q) const {
    double dx=std::abs(p.x-q.x)-(s.nodes[a].w+s.nodes[b].w)*.5;
    double dy=std::abs(p.y-q.y)-(s.nodes[a].h+s.nodes[b].h)*.5;
    return dx>=55.99||dy>=41.99;
  }
  bool makeLabel(int n,Point p,PositionLabel& result) {
    std::vector<Point> original;for(int member:members[n])original.push_back(s.pos[member]);
    double rotation=0;
    if(options.leaves==2&&members[n].size()>1&&distance(p,original[0])>.01) {
      Point leafCenter{},peerCenter{};int peers=0;
      for(size_t i=1;i<members[n].size();i++){leafCenter.x+=original[i].x;leafCenter.y+=original[i].y;}
      leafCenter.x/=members[n].size()-1;leafCenter.y/=members[n].size()-1;
      for(int other:s.adj[n])if(rootByMember[other]!=n){peerCenter.x+=s.pos[other].x;peerCenter.y+=s.pos[other].y;peers++;}
      if(peers){peerCenter.x/=peers;peerCenter.y/=peers;rotation=std::atan2(p.y-peerCenter.y,p.x-peerCenter.x)-std::atan2(leafCenter.y-original[0].y,leafCenter.x-original[0].x);}
    }
    bool valid=true;std::vector<int> placed;
    for(size_t i=0;i<members[n].size();i++) {
      int member=members[n][i];
      double dx=original[i].x-original[0].x,dy=original[i].y-original[0].y;
      Point wanted{p.x+dx*std::cos(rotation)-dy*std::sin(rotation),p.y+dx*std::sin(rotation)+dy*std::cos(rotation)};
      if(p.x==original[0].x&&p.y==original[0].y)wanted=original[i];
      const auto free=[&](Point q) {
        if(!s.inside(member,q))return false;
        for(int other=0;other<int(s.nodes.size());other++)if(!variable[other]&&!separated(member,q,other,s.pos[other]))return false;
        for(int other:placed)if(!separated(member,q,other,s.pos[other]))return false;
        s.pos[member]=q;return true;
      };
      bool found=free(wanted);
      for(int ring=1;i>0&&!found&&ring<=12;ring++)for(int angle=0;!found&&angle<16;angle++) {
        double theta=angle*6.283185307179586/16;
        found=free({wanted.x+ring*60*std::cos(theta),wanted.y+ring*60*std::sin(theta)});
      }
      if(!found){valid=false;break;}placed.push_back(member);
    }
    if(valid) {
      result.point=p;
      for(int member:members[n])result.members.push_back(s.pos[member]);
      for(int e:s.incident[n])result.star.push_back(s.route(e));
    }
    for(size_t i=0;i<members[n].size();i++)s.pos[members[n][i]]=original[i];
    return valid;
  }
  int unary(int n,const PositionLabel& label) const {
    const auto& routes=label.star;
    int cost=0;
    for(size_t i=0;i<members[n].size();i++)for(int f=0;f<int(s.edges.size());f++)if(!variableEdge[f])cost+=hits(s.routes[f],label.members[i],s.nodes[members[n][i]]);
    for(int i=0;i<int(routes.size());i++) {
      int e=s.incident[n][i],other=s.edges[e].s==n?s.edges[e].t:s.edges[e].s;
      if(options.connected&&variable[other]&&rootByMember[other]!=n)continue;
      for(int f=0;f<int(s.edges.size());f++)if(!variableEdge[f])cost+=crosses(routes[i],s.routes[f]);
      for(int obstacle=0;obstacle<int(s.nodes.size());obstacle++)if(!variable[obstacle]&&obstacle!=other)cost+=hits(routes[i],s.pos[obstacle],s.nodes[obstacle]);
      for(size_t j=0;j<members[n].size();j++)if(members[n][j]!=s.edges[e].s&&members[n][j]!=s.edges[e].t)cost+=hits(routes[i],label.members[j],s.nodes[members[n][j]]);
      for(int j=i+1;j<int(routes.size());j++)cost+=crosses(routes[i],routes[j]);
    }
    return cost;
  }
  int coupling(int a,const PositionLabel& p,int b,const PositionLabel& q) const {
    if(!separated(a,p.point,b,q.point))return forbidden;
    int cost=0;
    for(const auto& e:p.star) {
      cost+=hits(e,q.point,s.nodes[b]);
      for(const auto& f:q.star)cost+=crosses(e,f);
    }
    for(const auto& f:q.star)cost+=hits(f,p.point,s.nodes[a]);
    return cost;
  }
  void choose(int round) {
    std::vector<long> pressure;s.full(&pressure);
    std::vector<int> order(s.nodes.size());std::iota(order.begin(),order.end(),0);
    std::vector<double> priority(s.nodes.size());
    for(int n:order)priority[n]=pressure[n]/std::pow(1.+s.adj[n].size(),.2)*(round? .3+unit()*1.7:1.);
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){return priority[a]>priority[b];});
    selected.clear();variable.assign(s.nodes.size(),false);variableEdge.assign(s.edges.size(),false);
    for(int n:order) {
      if(pressure[n]==0)continue;
      bool adjacent=false;for(int other:s.adj[n])if(variable[other])adjacent=true;
      if(adjacent&&!options.connected)continue;
      selected.push_back(n);variable[n]=true;
      for(int e:s.incident[n])variableEdge[e]=true;
      if(int(selected.size())==options.size)break;
    }
    members.assign(s.nodes.size(),{});rootByMember.assign(s.nodes.size(),-1);
    for(int n:selected){members[n]={n};rootByMember[n]=n;}
    if(options.leaves)for(int n:selected)for(int other:s.adj[n])if(s.adj[other].size()==1&&!variable[other]) {
      members[n].push_back(other);rootByMember[other]=n;variable[other]=true;
    }
  }
  std::vector<PositionLabel> candidates(int n,const std::function<bool()>& expired) {
    std::vector<PositionLabel> pool;
    struct Line {Point a,d;};std::vector<Line> lines;
    const auto old=s.pos[n];
    const auto addLine=[&](Point a,Point b){double length=distance(a,b);if(length>1e-6)lines.push_back({a,{(b.x-a.x)/length,(b.y-a.y)/length}});};
    std::vector<PositionLabel> fixedStars;
    if(!options.connected)for(int other:selected)fixedStars.push_back({s.pos[other],0,0,star(other,s.pos[other]),{s.pos[other]}});
    const auto evaluate=[&](Point p) {
      if(!s.inside(n,p))return;
      for(int other=0;other<int(s.nodes.size());other++)if(!variable[other]&&!separated(n,p,other,s.pos[other]))return;
      PositionLabel label;
      if(!makeLabel(n,p,label))return;
      label.unary=unary(n,label);
      if(label.unary>=forbidden)return;
      long conditional=label.unary;
      if(options.connected) {
        std::vector<Point> originals;
        for(size_t i=0;i<members[n].size();i++){int member=members[n][i];originals.push_back(s.pos[member]);s.moved[member]=true;s.pos[member]=label.members[i];}
        for(int e:s.incident[n])s.changedEdge[e]=true;
        s.update(s.incident[n]);conditional=long(s.local(members[n],s.incident[n]).cost(1));
        for(size_t i=0;i<members[n].size();i++){s.pos[members[n][i]]=originals[i];s.moved[members[n][i]]=false;}
        s.update(s.incident[n]);for(int e:s.incident[n])s.changedEdge[e]=false;
      } else for(int i=0;i<int(selected.size());i++)if(selected[i]!=n)conditional+=coupling(n,label,selected[i],fixedStars[i]);
      label.conditional=int(std::min(long(forbidden),conditional));
      pool.push_back(std::move(label));
    };
    evaluate(old);
    if(pool.empty())throw std::runtime_error("joint original position is not feasible");
    for(int other:selected)if(other!=n)evaluate(s.pos[other]);
    const int mandatory=std::min(int(pool.size()),options.connected?3:options.labels/2);
    Point mean=old;
    if(!s.adj[n].empty()) {
      mean={};for(int other:s.adj[n]){mean.x+=s.pos[other].x;mean.y+=s.pos[other].y;}
      mean.x/=s.adj[n].size();mean.y/=s.adj[n].size();
    }
    for(int e:s.incident[n]) {
      int other=s.edges[e].s==n?s.edges[e].t:s.edges[e].s;
      for(int f=0;f<int(s.edges.size());f++)if(s.edges[f].s!=n&&s.edges[f].t!=n&&crosses(s.routes[e],s.routes[f])) {
        addLine(s.pos[other],s.routes[f].a);addLine(s.pos[other],s.routes[f].b);
      }
      for(int obstacle=0;obstacle<int(s.nodes.size());obstacle++)if(obstacle!=n&&obstacle!=other&&hits(s.routes[e],s.pos[obstacle],s.nodes[obstacle]))
        for(int x:{-1,1})for(int y:{-1,1})addLine(s.pos[other],{s.pos[obstacle].x+x*(s.nodes[obstacle].w*.5+10),s.pos[obstacle].y+y*(s.nodes[obstacle].h*.5+10)});
    }
    evaluate(mean);
    for(int i=0;i<options.samples&&!expired();i++) {
      Point p=old;int mode=pick(10);
      if(mode<4&&!lines.empty()) {
        const auto& a=lines[pick(lines.size())];const auto& b=lines[pick(lines.size())];
        double det=a.d.x*b.d.y-a.d.y*b.d.x;
        if(std::abs(det)<1e-7)continue;
        double along=((b.a.x-a.a.x)*b.d.y-(b.a.y-a.a.y)*b.d.x)/det;
        p={a.a.x+a.d.x*along,a.a.y+a.d.y*along};
      } else if(mode==4)p=mean;
      else if(mode==5&&!s.adj[n].empty())p=s.pos[s.adj[n][pick(s.adj[n].size())]];
      else if(mode==6)p=s.pos[selected[pick(selected.size())]];
      else if(mode==7)p={s.width*unit(),s.height*unit()};
      double radius=std::pow(10.,(mode<4?0.:1.2)+unit()*(mode<4?2.8:3.)),angle=unit()*6.283185307179586;
      evaluate({p.x+radius*std::cos(angle),p.y+radius*std::sin(angle)});
    }
    std::vector<PositionLabel> result;
    std::vector<char> used(pool.size(),false);
    for(int i=0;i<mandatory;i++){result.push_back(pool[i]);used[i]=true;}
    std::vector<int> order(pool.size());std::iota(order.begin(),order.end(),0);
    for(int pass=0;pass<3;pass++) {
      auto value=[&](int i){return pass==0?double(pool[i].unary):pass==1?double(pool[i].conditional):pool[i].unary*.8+pool[i].conditional*.2;};
      std::stable_sort(order.begin(),order.end(),[&](int a,int b){return value(a)<value(b);});
      int quota=pass==2?options.labels:mandatory+(options.labels-mandatory)*(pass+1)/3;
      for(int i:order) {
        if(int(result.size())>=quota)break;
        if(used[i])continue;
        bool near=false;
        for(const auto& label:result)if(distance(label.point,pool[i].point)<std::min(180.,s.nodes[n].w*.5))near=true;
        if(near)continue;
        result.push_back(pool[i]);used[i]=true;
      }
    }
    return result;
  }

public:
  JointPositionSearch(State& state,JointSearchOptions configuration):s(state),options(configuration),rng(options.seed){}
  Score run(const std::function<bool()>& expired,const std::function<double()>& elapsed,const std::string& output) {
    auto current=s.full();
    for(int round=0;round<options.rounds&&!expired();round++) {
      choose(round);if(selected.size()<2)break;
      std::vector<std::vector<PositionLabel>> positions;
      for(int n:selected){positions.push_back(candidates(n,expired));if(expired())break;}
      if(expired())break;
      if(options.connected) {
        std::vector<int> owner(s.nodes.size(),-1),domains;
        std::vector<std::vector<Point>> points(s.nodes.size());
        for(int i=0;i<int(selected.size());i++) {
          domains.push_back(positions[i].size());
          for(size_t j=0;j<members[selected[i]].size();j++) {
            int member=members[selected[i]][j];owner[member]=i;
            for(const auto& label:positions[i])points[member].push_back(label.members[j]);
          }
        }
        FiniteLabelFactors factors;
        if(!buildGeometryFactors(s,owner,points,domains,expired,factors))break;
        const auto original=s.pos;
        const long baseline=factors.energy(std::vector<int>(selected.size(),0));
        for(int trial=0;trial<12&&!expired();trial++) {
          std::vector<int> labels;
          for(int i=0;i<int(selected.size());i++){labels.push_back(pick(domains[i]));for(int member:members[selected[i]])s.pos[member]=points[member][labels[i]];}
          s.allRoutes();const auto actual=s.full();
          if(actual.visual()-current.visual()!=factors.energy(labels,0)-baseline||actual.spacing!=factors.spacingCount(labels))
            throw std::runtime_error("four-variable factor decomposition differs from complete scene");
        }
        s.pos=original;s.allRoutes();
        auto solution=solveFiniteFactors(factors,options.steps,rng(),expired,options.relaxSpacing);
        for(int i=0;i<int(selected.size());i++)for(int member:members[selected[i]])s.pos[member]=points[member][solution.labels[i]];
        s.allRoutes();auto candidate=s.full();
        if(candidate.spacing||candidate.overlap||candidate.visual()-current.visual()!=solution.energy-baseline)
          throw std::runtime_error("four-variable solution differs from complete scene");
        int movedNodes=0;for(int n:selected)movedNodes+=members[n].size();
        std::cerr<<"connected joint round="<<round<<" variables="<<selected.size()<<" nodes="<<movedNodes<<" predictedGain="<<baseline-solution.energy<<" blockWins="<<solution.blockWins<<'\n';
        if(better(candidate,current)){current=candidate;s.save(output);}else{s.pos=original;s.allRoutes();}
        report("connected-joint",round,current,elapsed());continue;
      }
      PairwiseLabels model;
      for(const auto& choices:positions){std::vector<int> costs;for(const auto& label:choices)costs.push_back(label.unary);model.unary.push_back(std::move(costs));}
      model.pairs.resize(selected.size()*selected.size());
      for(int i=0;i<int(selected.size())&&!expired();i++)for(int j=i+1;j<int(selected.size())&&!expired();j++) {
        auto& matrix=model.pairs[i*selected.size()+j];
        for(const auto& a:positions[i])for(const auto& b:positions[j])matrix.push_back(coupling(selected[i],a,selected[j],b));
      }
      if(expired())break;
      const long baseline=model.energy(std::vector<int>(selected.size(),0));
      const auto old=s.pos;
      // Audit arbitrary valid combinations, not only the winning combination.
      for(int trial=0;trial<12;trial++) {
        std::vector<int> labels;
        for(int i=0;i<int(selected.size());i++){labels.push_back(pick(positions[i].size()));s.pos[selected[i]]=positions[i][labels[i]].point;}
        s.allRoutes();auto actual=s.full();
        if(actual.spacing==0&&actual.overlap==0&&actual.visual()-current.visual()!=model.energy(labels)-baseline)
          throw std::runtime_error("joint pairwise decomposition differs from complete scene");
      }
      s.pos=old;s.allRoutes();
      const auto solution=solvePairwiseLabels(model,options.steps,rng(),expired);
      for(int i=0;i<int(selected.size());i++)s.pos[selected[i]]=positions[i][solution.labels[i]].point;
      s.allRoutes();auto candidate=s.full();
      if(candidate.spacing||candidate.overlap||candidate.visual()-current.visual()!=solution.energy-baseline)
        throw std::runtime_error("joint solution differs from complete scene");
      std::cerr<<"joint round="<<round<<" variables="<<selected.size()<<" predictedGain="<<baseline-solution.energy<<" pairWins="<<solution.pairWins<<" tripleWins="<<solution.tripleWins<<'\n';
      if(better(candidate,current)){current=candidate;s.save(output);}
      else{s.pos=old;s.allRoutes();}
      report("joint",round,current,elapsed());
    }
    return current;
  }
};

Score runJointPositionSearch(State& s,int argc,char**argv,const std::function<bool()>& expired,
                             const std::function<double()>& elapsed,const std::string& output) {
  JointSearchOptions options;
  options.rounds=int(number(argc,argv,"--joint-rounds",0));
  options.size=int(number(argc,argv,"--joint-size",16));
  options.labels=int(number(argc,argv,"--joint-labels",32));
  options.samples=int(number(argc,argv,"--joint-samples",500));
  options.steps=int(number(argc,argv,"--joint-steps",20000));
  options.connected=number(argc,argv,"--joint-connected",0)!=0;
  options.leaves=int(number(argc,argv,"--joint-leaves",0));
  options.relaxSpacing=number(argc,argv,"--joint-relax-spacing",0)!=0;
  options.seed=uint64_t(number(argc,argv,"--seed",61));
  if(options.rounds<0||options.size<2||options.size>48||options.labels<2||options.labels>64||options.samples<0||options.samples>4000||options.steps<0)
    throw std::runtime_error("invalid joint position options");
  if(options.connected&&(options.size>24||options.labels>16))throw std::runtime_error("connected joint search requires <=24 variables and <=16 labels");
  if(options.leaves<0||options.leaves>2||(options.leaves&&!options.connected))throw std::runtime_error("joint leaf groups require connected joint search");
  JointPositionSearch search(s,options);
  return search.run(expired,elapsed,output);
}
}
