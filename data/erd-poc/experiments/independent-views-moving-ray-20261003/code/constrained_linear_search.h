// Alternating-axis linear separation proposals. Fixing one coordinate makes
// edge ordering, card clearance, and displacement constraints linear. The LP
// is only a proposal model: clipped straight routes are independently scored.
#include <coin/ClpSimplex.hpp>
#include <coin/CoinPackedMatrix.hpp>
#include <coin/CoinPackedVector.hpp>
#include <array>

namespace {
struct SeparationRow {
  std::vector<std::pair<int,double>> terms;
  double lower;
  int slack=-1;
};
struct SeparationCandidate {
  std::vector<SeparationRow> rows;
  double weight=0,priority=0;
};

Score searchLinearSeparations(State& s,int argc,char** argv,
                             const std::function<double()>& elapsed,const std::string& output) {
  const int rounds=int(number(argc,argv,"--linear-rounds",0));
  const int limit=int(number(argc,argv,"--linear-limit",16000));
  const int backtracks=int(number(argc,argv,"--linear-backtracks",0));
  const double radius=number(argc,argv,"--linear-radius",1500);
  const double solveSeconds=number(argc,argv,"--linear-solve-seconds",12);
  const long portSteps=long(number(argc,argv,"--linear-port-steps",100000));
  const bool preserveClear=number(argc,argv,"--linear-preserve-clear",1)!=0;
  const double seconds=number(argc,argv,"--seconds",45);
  if(rounds<0||backtracks<0||backtracks>8||limit<1||limit>32000||portSteps<0||!std::isfinite(radius)||radius<=0||!std::isfinite(solveSeconds)||solveSeconds<=0)
    throw std::runtime_error("invalid linear separation limits");
  const auto expired=[&]{return elapsed()>=seconds-std::min(5.,seconds*.1);};
  auto best=s.full();auto bestPos=s.pos;auto bestRoutes=s.routes;const int count=s.nodes.size();
  std::mt19937_64 rng(uint64_t(number(argc,argv,"--seed",42)));
  bool cycleImproved=false;
  for(int round=0;round<rounds&&!expired();round++) {
    if(round%4==0){s.pos=bestPos;s.routes=bestRoutes;cycleImproved=false;}
    const int axis=round%2;
    const auto x=[&](int n){return axis?s.pos[n].y:s.pos[n].x;};
    const auto y=[&](int n){return axis?s.pos[n].x:s.pos[n].y;};
    const auto width=[&](int n){return axis?s.nodes[n].h:s.nodes[n].w;};
    const auto height=[&](int n){return axis?s.nodes[n].w:s.nodes[n].h;};
    const double canvas=axis?s.width:s.height;
    const double xgap=axis?42.:56.,ygap=axis?56.:42.;
    std::vector<double> lower(count*2,0),upper(count*2,1e100),objective(count*2,0);
    std::vector<SeparationRow> hard;
    for(int n=0;n<count;n++) {
      lower[n]=std::max(height(n)*.5+1,y(n)-radius);
      upper[n]=std::min(canvas-height(n)*.5-1,y(n)+radius);
      objective[count+n]=.00008;
      hard.push_back({{{n,1},{count+n,1}},y(n)});
      hard.push_back({{{n,-1},{count+n,1}},-y(n)});
      for(int other=n+1;other<count;other++)if(std::abs(x(n)-x(other))<(width(n)+width(other))*.5+xgap) {
        const double sign=y(n)>=y(other)?1:-1;
        hard.push_back({{{n,sign},{other,-sign}},(height(n)+height(other))*.5+ygap+.02});
      }
    }
    const auto snapped=[&](Point p,int n) {
      double dx=p.x-s.pos[n].x,dy=p.y-s.pos[n].y;
      if(std::abs(std::abs(dx)-s.nodes[n].w*.5)<.011)dx=std::copysign(s.nodes[n].w*.5,dx);
      if(std::abs(std::abs(dy)-s.nodes[n].h*.5)<.011)dy=std::copysign(s.nodes[n].h*.5,dy);
      return Point{s.pos[n].x+std::clamp(dx,-s.nodes[n].w*.5,s.nodes[n].w*.5),
        s.pos[n].y+std::clamp(dy,-s.nodes[n].h*.5,s.nodes[n].h*.5)};
    };
    struct OrderedEdge {int a,b;double left,right,offa,offb;};
    std::vector<OrderedEdge> ordered;
    std::vector<Segment> unrounded;
    for(int e=0;e<int(s.edges.size());e++) {
      int a=s.edges[e].s,b=s.edges[e].t;
      Point p=snapped(s.routes[e].a,a),q=snapped(s.routes[e].b,b);unrounded.push_back(segment(p,q));
      for(int end=0;end<2;end++) {
        int n=end?b:a,other=end?a:b;Point own=end?q:p,to=end?p:q;
        double bestOut=-1e100,sign=0;bool variable=false;
        for(int coordinate=0;coordinate<2;coordinate++) {
          double offset=coordinate?own.y-s.pos[n].y:own.x-s.pos[n].x;
          double half=(coordinate?s.nodes[n].h:s.nodes[n].w)*.5;
          if(std::abs(std::abs(offset)-half)>.001)continue;
          double direction=offset>=0?1:-1;
          double outward=direction*(coordinate?to.y-own.y:to.x-own.x);
          if(outward>bestOut){bestOut=outward;sign=direction;variable=coordinate==(axis?0:1);}
        }
        if(variable) {
          double offset=(axis?to.x-own.x:to.y-own.y)-(y(other)-y(n));
          hard.push_back({{{other,sign},{n,-sign}},.02-offset*sign});
        }
      }
      double left=axis?p.y:p.x,right=axis?q.y:q.x;
      double offa=(axis?p.x:p.y)-y(a),offb=(axis?q.x:q.y)-y(b);
      if(left>right){std::swap(a,b);std::swap(left,right);std::swap(offa,offb);}
      ordered.push_back({a,b,left,right,offa,offb});
    }
    const auto coefficients=[&](int e,double at,int end=0) {
      const auto edge=ordered[e];double t=edge.right-edge.left<1e-4?end:(at-edge.left)/(edge.right-edge.left);
      return std::array<std::pair<int,double>,2>{{{edge.a,1-t},{edge.b,t}}};
    };
    const auto bias=[&](int e,double at,int end=0) {
      const auto edge=ordered[e];double t=edge.right-edge.left<1e-4?end:(at-edge.left)/(edge.right-edge.left);
      return edge.offa*(1-t)+edge.offb*t;
    };
    const auto value=[&](int e,double at,int end=0) {
      double result=bias(e,at,end);for(auto [n,w]:coefficients(e,at,end))result+=y(n)*w;return result;
    };
    std::vector<SeparationCandidate> candidates;
    for(int e=0;e<int(s.edges.size())&&!expired();e++) {
      const auto edge=ordered[e];const bool vertical=edge.right-edge.left<1e-4;
      for(int f=e+1;f<int(s.edges.size());f++) {
        const auto other=ordered[f];
        const bool otherVertical=other.right-other.left<1e-4;
        if(vertical&&otherVertical)continue;
        const bool adjacent=edge.a==other.a||edge.a==other.b||edge.b==other.a||edge.b==other.b;
        double left=std::max(edge.left,other.left),right=std::min(edge.right,other.right);
        if(right-left<(vertical||otherVertical?-1e-4:1e-4))continue;
        double dl=value(e,left,0)-value(f,left,0),dr=value(e,right,1)-value(f,right,1);
        double margin=std::min(std::abs(dl),std::abs(dr));bool crossing=dl*dr<0&&(!adjacent||margin>1e-5);
        bool protect=preserveClear&&!crossing&&(margin>=1||adjacent);
        if(!crossing&&margin>(preserveClear?2*radius+8:250))continue;
        double sign=dl+dr>=0?1:-1;
        SeparationCandidate candidate;
        candidate.priority=crossing?1e6-margin:250-margin;
        candidate.weight=crossing?1./std::max(40.,margin):.025;
        for(int end=0;end<2;end++) {
          double at=end?right:left;
          SeparationRow row;row.lower=protect?std::min(8.,margin*.5):8;
          row.lower-=sign*(bias(e,at,end)-bias(f,at,end));
          for(auto [n,w]:coefficients(e,at,end))row.terms.emplace_back(n,w*sign);
          for(auto [n,w]:coefficients(f,at,end))row.terms.emplace_back(n,-w*sign);
          if(protect)hard.push_back(std::move(row));
          else candidate.rows.push_back(std::move(row));
        }
        if(!candidate.rows.empty())candidates.push_back(std::move(candidate));
      }
      for(int n=0;n<count;n++)if(n!=edge.a&&n!=edge.b) {
        double left=std::max(edge.left,x(n)-width(n)*.5-10),right=std::min(edge.right,x(n)+width(n)*.5+10);
        if(right-left<(vertical?-1e-4:1e-4))continue;
        double dl=value(e,left,0)-y(n),dr=value(e,right,1)-y(n),clear=height(n)*.5+11;
        double range=preserveClear?2*radius:150;
        if(std::min(dl,dr)>clear+range||std::max(dl,dr)<-clear-range)continue;
        double sign=dl+dr>=0?1:-1;
        double debt=clear-std::min(dl*sign,dr*sign);
        SeparationCandidate candidate;candidate.priority=debt>0?1e6+debt:150+debt;
        candidate.weight=1./std::max(40.,clear);
        for(int end=0;end<2;end++) {
          double at=end?right:left;
          SeparationRow row;row.lower=clear-sign*bias(e,at,end);row.terms.emplace_back(n,-sign);
          for(auto [node,w]:coefficients(e,at,end))row.terms.emplace_back(node,w*sign);
          if(preserveClear&&debt<=0)hard.push_back(std::move(row));
          else candidate.rows.push_back(std::move(row));
        }
        if(!candidate.rows.empty())candidates.push_back(std::move(candidate));
      }
    }
    if(expired())break;
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.priority>b.priority;});
    if(int(candidates.size())>limit)candidates.resize(limit);
    for(auto& candidate:candidates) {
      int slack=objective.size();lower.push_back(0);upper.push_back(1e100);objective.push_back(candidate.weight);
      for(auto& row:candidate.rows){row.slack=slack;hard.push_back(std::move(row));}
    }
    CoinPackedMatrix matrix(false,0,0);matrix.setDimensions(0,objective.size());
    std::vector<double> rowLower,rowUpper;
    for(const auto& row:hard) {
      CoinPackedVector packed;
      std::vector<std::pair<int,double>> combined;
      for(const auto& term:row.terms) {
        int n=term.first;double w=term.second;
        auto found=std::find_if(combined.begin(),combined.end(),[&](const auto& v){return v.first==n;});
        if(found==combined.end())combined.emplace_back(n,w);else found->second+=w;
      }
      for(auto [n,w]:combined)if(std::abs(w)>1e-12)packed.insert(n,w);
      if(row.slack>=0)packed.insert(row.slack,1);
      matrix.appendRow(packed);rowLower.push_back(row.lower);rowUpper.push_back(1e100);
    }
    ClpSimplex model;model.setLogLevel(0);
    model.loadProblem(matrix,lower.data(),upper.data(),objective.data(),rowLower.data(),rowUpper.data());
    const auto rowCount=hard.size(),columnCount=objective.size();
    // Clp owns its loaded matrix. Release construction copies before simplex
    // factorization, which is the largest part of the bounded memory budget.
    std::vector<SeparationRow>().swap(hard);std::vector<SeparationCandidate>().swap(candidates);
    matrix=CoinPackedMatrix();std::vector<double>().swap(rowLower);std::vector<double>().swap(rowUpper);
    std::vector<double>().swap(lower);std::vector<double>().swap(upper);std::vector<double>().swap(objective);
    model.setMaximumSeconds(std::max(.01,std::min(solveSeconds,seconds-elapsed()-2)));
    model.setMaximumIterations(60000);model.dual();
    if(model.status()!=0) {
      std::cerr<<"linear round="<<round<<" status="<<model.status()<<" rows="<<rowCount<<" columns="<<columnCount<<'\n';
      continue;
    }
    const auto prior=s.pos;const auto priorRoutes=s.routes;const auto solution=model.primalColumnSolution();
    auto lineBest=s.full();auto linePos=prior;auto lineRoutes=priorRoutes;
    for(int backtrack=0;backtrack<=backtracks;backtrack++) {
    if(backtrack&&expired())break;
    const double fraction=std::pow(.5,backtrack);
    s.pos=prior;s.routes=priorRoutes;
    bool valid=true;int outside=0,endpointEntries=0,adjacentCrossings=0;
    for(int n=0;n<count;n++) {
      double start=axis?prior[n].x:prior[n].y;
      double destination=backtrack?start+(solution[n]-start)*fraction:solution[n];
      if(axis)s.pos[n].x=destination;else s.pos[n].y=destination;
      outside+=!s.inside(n,s.pos[n]);valid=valid&&s.inside(n,s.pos[n]);
    }
    for(int e=0;e<int(s.edges.size());e++) {
      int a=s.edges[e].s,b=s.edges[e].t;
      Point p{rounded(unrounded[e].a.x+s.pos[a].x-prior[a].x),rounded(unrounded[e].a.y+s.pos[a].y-prior[a].y)};
      Point q{rounded(unrounded[e].b.x+s.pos[b].x-prior[b].x),rounded(unrounded[e].b.y+s.pos[b].y-prior[b].y)};
      s.routes[e]=segment(p,q);
      endpointEntries+=hits(s.routes[e],s.pos[a],s.nodes[a],-.02)||hits(s.routes[e],s.pos[b],s.nodes[b],-.02);
    }
    int portRepairs=repairAdjacentBoundaryPorts(s,expired);
    for(const auto& incident:s.incident)for(size_t i=0;i<incident.size();i++)for(size_t j=i+1;j<incident.size();j++)
      adjacentCrossings+=crosses(s.routes[incident[i]],s.routes[incident[j]]);
    valid=valid&&!endpointEntries&&!adjacentCrossings;
    auto candidate=s.full();valid=valid&&!candidate.overlap&&!candidate.spacing;
    const auto rawVisual=candidate.visual();
    if(valid&&portSteps)candidate=refineBoundaryPorts(s,candidate,portSteps,rng,expired,elapsed);
    bool improve=valid&&better(candidate,best);
    std::cerr<<"linear round="<<round<<" status=0 rows="<<rowCount<<" columns="<<columnCount
      <<" candidate="<<candidate.visual()<<" raw="<<rawVisual<<" cross="<<candidate.cross<<" hit="<<candidate.hit
      <<" spacing="<<candidate.spacing<<" outside="<<outside<<" endpoint="<<endpointEntries<<" adjacent="<<adjacentCrossings<<" portRepairs="<<portRepairs
      <<" fraction="<<fraction<<" valid="<<valid<<" accepted="<<improve<<'\n';
    if(improve){best=candidate;bestPos=s.pos;bestRoutes=s.routes;cycleImproved=true;s.save(output);report("linear-best",round,best,elapsed());}
    if(valid&&better(candidate,lineBest)){lineBest=candidate;linePos=s.pos;lineRoutes=s.routes;}
    if(!valid){s.pos=prior;s.routes=priorRoutes;}
    }
    if(backtracks){s.pos=linePos;s.routes=lineRoutes;}
    if(!cycleImproved&&round%4==3)break;
  }
  s.pos=bestPos;s.routes=bestRoutes;return best;
}
}
