#include "constrained_scene.h"
#include "constrained_neighborhood_search.h"
#include "constrained_global_search.h"
#include "constrained_joint_search.h"
#include "constrained_port_search.h"
#include "constrained_slot_assignment.h"
#include "constrained_adaptive_port_search.h"
#include "constrained_crossover_search.h"
#include "constrained_detour_search.h"
#include "constrained_hub_relocation.h"
#include "constrained_group_repair.h"
#ifdef ERD_ENABLE_LINEAR_SEARCH
#include "constrained_linear_search.h"
#endif
int main(int argc,char**argv) {
  try {
    State s;
    s.width=number(argc,argv,"--width",31600);s.height=number(argc,argv,"--height",31600);
    const double maxArea=number(argc,argv,"--max-area",1e9);
    if(!std::isfinite(maxArea)||maxArea<=0)throw std::runtime_error("max area must be finite and positive");
    if(!std::isfinite(s.width)||!std::isfinite(s.height)||s.width*s.height>maxArea||s.width<=0||s.height<=0)
      throw std::runtime_error(maxArea==1e9?"canvas must be finite, positive and <= 1B":"canvas must be finite, positive and <= configured max area");
    const std::string output=arg(argc,argv,"--out");
    std::unordered_map<std::string,int> ids;
    std::ifstream nodeIn(arg(argc,argv,"--nodes"));std::string line;
    if(!nodeIn)throw std::runtime_error("cannot read nodes");
    while(std::getline(nodeIn,line)){auto f=split(line);if(f.size()<3)throw std::runtime_error("invalid node row");int id=s.nodes.size();if(ids.count(f[0]))throw std::runtime_error("duplicate node");ids[f[0]]=id;s.nodes.push_back({f[0],std::stod(f[1]),std::stod(f[2])});}
    if(s.nodes.empty())throw std::runtime_error("empty nodes");
    for(const auto& n:s.nodes)if(!std::isfinite(n.w)||!std::isfinite(n.h)||n.w<=0||n.h<=0||n.w+2>s.width||n.h+2>s.height)throw std::runtime_error("invalid node dimensions: "+n.id);
    s.pos.resize(s.nodes.size());s.adj.resize(s.nodes.size());s.incident.resize(s.nodes.size());s.moved.resize(s.nodes.size());
    std::ifstream edgeIn(arg(argc,argv,"--edges"));std::unordered_map<std::string,bool> edgeIds;
    if(!edgeIn)throw std::runtime_error("cannot read edges");
    while(std::getline(edgeIn,line)){auto f=split(line);if(f.size()<3||!ids.count(f[1])||!ids.count(f[2]))throw std::runtime_error("invalid edge row");if(edgeIds.count(f[0]))throw std::runtime_error("duplicate edge");edgeIds[f[0]]=true;int a=ids.at(f[1]),b=ids.at(f[2]);if(a==b)continue;int e=s.edges.size();s.edges.push_back({f[0],a,b});s.adj[a].push_back(b);s.adj[b].push_back(a);s.incident[a].push_back(e);s.incident[b].push_back(e);}
    if(s.edges.empty())throw std::runtime_error("empty edges");
    s.changedEdge.resize(s.edges.size());
    std::ifstream posIn(arg(argc,argv,"--positions"));std::vector<bool> present(s.nodes.size());
    if(!posIn)throw std::runtime_error("cannot read positions");
    while(std::getline(posIn,line)){auto f=split(line);if(f.size()<3||!ids.count(f[0]))continue;int n=ids.at(f[0]);s.pos[n]={std::stod(f[1]),std::stod(f[2])};present[n]=true;}
    if(std::find(present.begin(),present.end(),false)!=present.end())throw std::runtime_error("missing positions");
    for(const auto& p:s.pos)if(!std::isfinite(p.x)||!std::isfinite(p.y))throw std::runtime_error("non-finite position");
    const std::string groupPath=arg(argc,argv,"--groups","none");
    if(groupPath!="none") {
      std::ifstream in(groupPath);std::unordered_map<std::string,int> groupIds;
      if(!in)throw std::runtime_error("cannot read groups");
      while(std::getline(in,line)){auto f=split(line);if(f.size()<2||!ids.count(f[1]))throw std::runtime_error("invalid group row");if(!groupIds.count(f[0])){groupIds[f[0]]=s.groups.size();s.groups.emplace_back();}s.groups[groupIds.at(f[0])].push_back(ids.at(f[1]));}
      for(auto&g:s.groups){std::sort(g.begin(),g.end());g.erase(std::unique(g.begin(),g.end()),g.end());}
    }
    const std::string elasticPath=arg(argc,argv,"--elastic-groups","none");
    if(elasticPath!="none"){
      std::ifstream in(elasticPath);std::unordered_map<std::string,int> groupIds;
      if(!in)throw std::runtime_error("cannot read elastic groups");
      while(std::getline(in,line)){
        auto f=split(line);if(f.size()<3||!ids.count(f[1]))throw std::runtime_error("invalid elastic group row");
        double weight=std::stod(f[2]);if(!std::isfinite(weight)||weight<=0||weight>1)throw std::runtime_error("invalid elastic weight");
        if(!groupIds.count(f[0])){groupIds[f[0]]=s.elasticGroups.size();s.elasticGroups.emplace_back();}
        s.elasticGroups[groupIds.at(f[0])].push_back({ids.at(f[1]),weight});
      }
    }
    initialize(s,int(number(argc,argv,"--fit",1)));
    std::vector<long> pressure;
    Score current=s.full(&pressure),best=current;std::vector<Point> bestPos=s.pos;
    const auto attachedPorts=arg(argc,argv,"--attached-ports","none");
    if(attachedPorts!="none") {
      if(number(argc,argv,"--fit",1)!=0||number(argc,argv,"--linear-rounds",0)||number(argc,argv,"--hub-rounds",0)||number(argc,argv,"--joint-rounds",0)||number(argc,argv,"--global-projections",0)||number(argc,argv,"--shell-rounds",0)||number(argc,argv,"--neighborhood-rounds",0)||arg(argc,argv,"--initial-ports","none")!="none")
        throw std::runtime_error("attached ports require fit=0 and annealing-only position search");
      current=attachBoundaryPorts(s,attachedPorts,current);best=current;s.full(&pressure);
    }
    const auto start=std::chrono::steady_clock::now();
    const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
    const long steps=long(number(argc,argv,"--steps",200000));
    const int cycles=int(number(argc,argv,"--cycles",4));
    const long cycleLength=std::max(1L,steps/std::max(1,cycles));
    const double seconds=number(argc,argv,"--seconds",45),hitWeight=number(argc,argv,"--hit-weight",1.0);
    const bool bestWeighted=number(argc,argv,"--best-weighted",0)!=0;
    const long crossingCap=long(number(argc,argv,"--crossing-cap",-1));
    if(crossingCap>=0&&current.cross>crossingCap)throw std::runtime_error("initial crossing count exceeds cap");
    if(crossingCap>=0&&(number(argc,argv,"--port-steps",0)||number(argc,argv,"--adaptive-steps",0)||arg(argc,argv,"--initial-ports","none")!="none"||arg(argc,argv,"--crossover-positions","none")!="none"||number(argc,argv,"--linear-rounds",0)||number(argc,argv,"--hub-rounds",0)||number(argc,argv,"--joint-rounds",0)||number(argc,argv,"--global-projections",0)||number(argc,argv,"--shell-rounds",0)||number(argc,argv,"--neighborhood-rounds",0)))
      throw std::runtime_error("crossing cap currently applies to annealing-only runs");
    const bool temporarySpacing=number(argc,argv,"--temporary-spacing",0)!=0;
    if(temporarySpacing&&bestWeighted)throw std::runtime_error("temporary spacing requires feasible visual selection");
    const double t0=number(argc,argv,"--temperature",4),t1=number(argc,argv,"--end-temperature",.015);
    const double degreeTemperature=number(argc,argv,"--degree-temperature",0);
    if(!std::isfinite(degreeTemperature)||degreeTemperature<0||degreeTemperature>1)throw std::runtime_error("invalid degree temperature exponent");
    const double groupRate=number(argc,argv,"--group-rate",.20);
    const bool geometric=number(argc,argv,"--geometry",0)!=0;
    const bool pressureSampling=number(argc,argv,"--pressure",0)!=0;
    const bool obstacleMoves=number(argc,argv,"--obstacle-moves",0)!=0;
    const bool anchorMoves=number(argc,argv,"--anchor-moves",0)!=0;
    const bool groupRepair=number(argc,argv,"--group-repair",0)!=0;
    const double elasticRate=number(argc,argv,"--elastic-rate",.12);
    const int verifyEvery=int(number(argc,argv,"--verify-every",5000));
    if(steps<0||cycles<=0||seconds<=0||!std::isfinite(seconds)||verifyEvery<=0||t0<=0||t1<=0||!std::isfinite(t0)||!std::isfinite(t1)||hitWeight<=0||!std::isfinite(hitWeight))throw std::runtime_error("invalid search limits or weights");
    if(!std::isfinite(groupRate)||groupRate<0||groupRate>1||!std::isfinite(elasticRate)||elasticRate<0||elasticRate>1)throw std::runtime_error("invalid group rate");
    std::mt19937_64 rng(uint64_t(number(argc,argv,"--seed",42)));
    std::uniform_real_distribution<double> unit(0,1);
    const auto randomIndex=[&](int count){return int(rng()%uint64_t(count));};
    std::vector<int> selectable;
    for(int n=0;n<int(s.nodes.size());n++)if(!s.adj[n].empty())selectable.push_back(n);
    report("start",0,current,elapsed());s.save(output);
    long accepted=0,uphill=0,completed=0,repairAttempts=0,repairSuccess=0;
    for(long step=1;step<=steps;step++) {
      completed=step;
      if(step%256==0&&elapsed()>=seconds)break;
      const double progress=double((step-1)%cycleLength)/double(cycleLength);
      const double temperature=t0*std::pow(t1/t0,progress);
      // Temporary spacing debt can unlock cyclic card exchanges. It is only
      // an exploration cost; the retained best must still have zero debt.
      const auto energy=[&](Score score) {
        return temporarySpacing?score.cross+score.hit*hitWeight+score.overlap
          +score.spacing*2*std::pow(1000.,progress):score.cost(hitWeight);
      };
      std::vector<int> moving;
      std::vector<double> weights;
      int mode=randomIndex(obstacleMoves?18:geometric?15:10);
      bool group=!s.groups.empty()&&unit(rng)<groupRate;
      if(group){
        int selected=randomIndex(s.groups.size());
        if(pressureSampling&&unit(rng)<.65){
          const auto groupPressure=[&](int g){double sum=0;for(int n:s.groups[g])sum+=pressure[n];return sum/std::sqrt(double(s.groups[g].size()));};
          for(int i=0;i<4;i++){int other=randomIndex(s.groups.size());if(groupPressure(other)>groupPressure(selected))selected=other;}
        }
        moving=s.groups[selected];
      }
      else {
        int n=selectable[randomIndex(selectable.size())];
        if(pressureSampling&&unit(rng)<.65)for(int i=0;i<4;i++){int other=selectable[randomIndex(selectable.size())];if(pressure[other]>pressure[n])n=other;}
        moving={n};
      }
      const bool elastic=!s.elasticGroups.empty()&&unit(rng)<elasticRate;
      if(elastic){
        int selected=randomIndex(s.elasticGroups.size());
        if(pressureSampling&&unit(rng)<.65)for(int i=0;i<4;i++){int other=randomIndex(s.elasticGroups.size());if(pressure[s.elasticGroups[other][0].first]>pressure[s.elasticGroups[selected][0].first])selected=other;}
        moving.clear();for(auto entry:s.elasticGroups[selected]){moving.push_back(entry.first);weights.push_back(entry.second);}group=true;
      }
      if(moving.empty())continue;
      if(!group&&mode==9){int other=selectable[randomIndex(selectable.size())];if(other==moving[0])continue;moving.push_back(other);}
      std::vector<int> affected;
      for(int n:moving){s.moved[n]=true;for(int e:s.incident[n])if(!s.changedEdge[e]){s.changedEdge[e]=true;affected.push_back(e);}}
      std::vector<Point> old;for(int n:moving)old.push_back(s.pos[n]);
      Point center,anchor;int boundary=0;std::vector<int> anchors;
      for(int n:moving){center.x+=s.pos[n].x;center.y+=s.pos[n].y;for(int other:s.adj[n])if(!s.moved[other]){anchor.x+=s.pos[other].x;anchor.y+=s.pos[other].y;boundary++;}}
      center.x/=moving.size();center.y/=moving.size();
      if(boundary){anchor.x/=boundary;anchor.y/=boundary;}else anchor=center;
      if(group&&anchorMoves){for(int n:moving)for(int other:s.adj[n])if(!s.moved[other])anchors.push_back(other);std::sort(anchors.begin(),anchors.end());anchors.erase(std::unique(anchors.begin(),anchors.end()),anchors.end());}
      std::vector<Point> proposed=old;
      const double radius=std::pow(10.,1.3+unit(rng)*2.9);
      const double angle=unit(rng)*6.283185307179586;
      const Point offset{radius*std::cos(angle),radius*std::sin(angle)};
      if(elastic){
        Point displacement=offset;
        if(mode%3==0){double mix=unit(rng);Point origin=s.pos[moving[0]];displacement={(anchor.x-origin.x)*mix+offset.x*.1,(anchor.y-origin.y)*mix+offset.y*.1};}
        for(size_t i=0;i<moving.size();i++)proposed[i]={old[i].x+displacement.x*weights[i],old[i].y+displacement.y*weights[i]};
      }
      else if(!group&&mode==9)std::swap(proposed[0],proposed[1]);
      else if(!group&&mode>=15) {
        int n=moving[0];Point candidate=center;
        if(mode==15||mode==16){
          int begin=randomIndex(s.edges.size());
          for(int offsetIndex=0;offsetIndex<int(s.edges.size());offsetIndex++){
            int e=(begin+offsetIndex)%s.edges.size();
            if(s.edges[e].s==n||s.edges[e].t==n||!hits(s.routes[e],center,s.nodes[n]))continue;
            const auto& line=s.routes[e];double dx=line.b.x-line.a.x,dy=line.b.y-line.a.y,len=std::hypot(dx,dy);if(len<1e-9)continue;
            Point normal{-dy/len,dx/len};double distance=(center.x-line.a.x)*normal.x+(center.y-line.a.y)*normal.y;
            double clear=(std::abs(normal.x)*s.nodes[n].w+std::abs(normal.y)*s.nodes[n].h)*.5+15+unit(rng)*80;
            clear*=(mode==15?(distance>=0?1:-1):(distance>=0?-1:1));
            double along=(unit(rng)-.5)*100;
            candidate={center.x+normal.x*(clear-distance)+dx/len*along,center.y+normal.y*(clear-distance)+dy/len*along};break;
          }
        }else{
          int incident=s.incident[n][randomIndex(s.incident[n].size())];
          int neighbor=s.edges[incident].s==n?s.edges[incident].t:s.edges[incident].s;
          int begin=randomIndex(s.nodes.size());
          for(int offsetIndex=0;offsetIndex<int(s.nodes.size());offsetIndex++){
            int block=(begin+offsetIndex)%s.nodes.size();if(block==n||block==neighbor||!hits(s.routes[incident],s.pos[block],s.nodes[block]))continue;
            Point a=s.pos[neighbor],b={s.pos[block].x+(unit(rng)<.5?-1:1)*(s.nodes[block].w*.5+15),s.pos[block].y+(unit(rng)<.5?-1:1)*(s.nodes[block].h*.5+15)};
            double dx=b.x-a.x,dy=b.y-a.y,len=std::hypot(dx,dy);if(len<1e-9)continue;
            Point normal{-dy/len,dx/len};double distance=(center.x-a.x)*normal.x+(center.y-a.y)*normal.y;
            double clear=(unit(rng)-.5)*150;
            candidate={center.x+normal.x*(clear-distance),center.y+normal.y*(clear-distance)};break;
          }
        }
        proposed[0]=candidate;
      }
      else if(!group&&mode>=10) {
        int n=moving[0];Point candidate=center;
        int neighbor=s.adj[n][randomIndex(s.adj[n].size())];
        if(mode==10) {
          int incident=s.incident[n][randomIndex(s.incident[n].size())];
          neighbor=s.edges[incident].s==n?s.edges[incident].t:s.edges[incident].s;
          for(int attempt=0;attempt<96;attempt++) {
            int e=randomIndex(s.edges.size());
            if(!crosses(s.routes[incident],s.routes[e]))continue;
            Point a=s.routes[e].a,b=s.routes[e].b;
            double dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);if(length<1e-6)continue;
            Point normal{-dy/length,dx/length};
            double signedDistance=(center.x-a.x)*normal.x+(center.y-a.y)*normal.y;
            double neighborSide=(s.pos[neighbor].x-a.x)*normal.x+(s.pos[neighbor].y-a.y)*normal.y;
            double offset=(std::abs(normal.x)*s.nodes[n].w+std::abs(normal.y)*s.nodes[n].h)*.5+20+unit(rng)*200;
            offset*=neighborSide>=0?1:-1;
            candidate={center.x+normal.x*(offset-signedDistance),center.y+normal.y*(offset-signedDistance)};
            break;
          }
        }else if(mode==11||s.adj[n].size()<2){
          int other=s.adj[n][randomIndex(s.adj[n].size())];
          double mix=unit(rng);
          candidate={s.pos[neighbor].x*mix+s.pos[other].x*(1-mix)+offset.x*.15,s.pos[neighbor].y*mix+s.pos[other].y*(1-mix)+offset.y*.15};
        }else{
          int other=s.adj[n][randomIndex(s.adj[n].size())];
          Point a=s.pos[neighbor],c=s.pos[other];
          Point b=s.pos[randomIndex(s.nodes.size())],d=s.pos[randomIndex(s.nodes.size())];
          Point r{b.x-a.x,b.y-a.y},t{d.x-c.x,d.y-c.y};
          double determinant=r.x*t.y-r.y*t.x;
          if(std::abs(determinant)>1e-6){
            double f=((c.x-a.x)*t.y-(c.y-a.y)*t.x)/determinant;
            double jitter=30+unit(rng)*300;
            candidate={a.x+r.x*f+jitter*std::cos(angle),a.y+r.y*f+jitter*std::sin(angle)};
          }
        }
        proposed[0]=candidate;
      }else if(group&&anchorMoves&&mode>=10&&anchors.size()<=2&&!anchors.empty()) {
        Point origin=s.pos[anchors[0]];
        if(anchors.size()==1){
          double turn=(unit(rng)-.5)*6.283185307179586;
          double c=std::cos(turn),sin=std::sin(turn);
          for(size_t i=0;i<moving.size();i++){double x=old[i].x-origin.x,y=old[i].y-origin.y;proposed[i]={origin.x+x*c-y*sin,origin.y+x*sin+y*c};}
        }else{
          Point other=s.pos[anchors[1]];double dx=other.x-origin.x,dy=other.y-origin.y,len=std::hypot(dx,dy);
          if(len>1e-9){
            Point axis{dx/len,dy/len},normal{-dy/len,dx/len};
            double shift=(unit(rng)-.5)*std::min(5000.,len*.4);
            double scale=std::exp((unit(rng)-.5)*1.4);
            for(size_t i=0;i<moving.size();i++){
              double x=old[i].x-origin.x,y=old[i].y-origin.y;
              double along=x*axis.x+y*axis.y,perp=x*normal.x+y*normal.y;
              if(mode%3==0)perp=-perp;else if(mode%3==1)perp+=shift;else{along=len*.5+(along-len*.5)*scale;perp*=scale;}
              proposed[i]={origin.x+axis.x*along+normal.x*perp,origin.y+axis.y*along+normal.y*perp};
            }
          }
        }
      }else if(group&&mode<4) {
        const double cosines[]={-1,1,0,0};const double sines[]={0,0,1,-1};
        for(size_t i=0;i<moving.size();i++){double x=old[i].x-center.x,y=old[i].y-center.y;if(mode==1)x=-x;proposed[i]={center.x+x*cosines[mode]-y*sines[mode],center.y+x*sines[mode]+y*cosines[mode]};}
      } else {
        Point destination=center;
        if(mode<3)destination={anchor.x+offset.x,anchor.y+offset.y};
        else if(mode==3)destination={s.width*unit(rng),s.height*unit(rng)};
        else if(mode==4&&boundary){int n=moving[0],neighbor=s.adj[n][randomIndex(s.adj[n].size())];destination={s.pos[neighbor].x+offset.x,s.pos[neighbor].y+offset.y};}
        else if(mode==5&&boundary){double mix=unit(rng);destination={center.x+(anchor.x-center.x)*mix,center.y+(anchor.y-center.y)*mix};}
        else destination={center.x+offset.x,center.y+offset.y};
        for(size_t i=0;i<moving.size();i++)proposed[i]={old[i].x+destination.x-center.x,old[i].y+destination.y-center.y};
      }
      bool valid=true;
      for(size_t i=0;i<moving.size();i++)if(!s.inside(moving[i],proposed[i]))valid=false;
      if(valid&&group&&groupRepair){repairAttempts++;valid=repairGroupProposal(s,moving,proposed);repairSuccess+=valid;}
      if(valid&&!temporarySpacing){
        // Card spacing is a hard constraint once initialization repairs it.
        for(size_t i=0;i<moving.size();i++)s.pos[moving[i]]=proposed[i];
        for(int a:moving){for(int b=0;b<int(s.nodes.size());b++)if(a!=b&&(!s.moved[b]||a<b)&&s.pair(a,b).spacing){valid=false;break;}if(!valid)break;}
        for(size_t i=0;i<moving.size();i++)s.pos[moving[i]]=old[i];
      }
      if(valid){
        Score before=s.local(moving,affected);
        for(size_t i=0;i<moving.size();i++)s.pos[moving[i]]=proposed[i];s.update(affected);
        Score after=s.local(moving,affected),next=current+after-before;
        const double delta=energy(next)-energy(current);
        const double moveTemperature=temperature*std::pow(std::max(1.,affected.size()/std::sqrt(double(moving.size()))),degreeTemperature);
        if((attachedPorts=="none"||validAttachedRoutes(s,affected))&&(crossingCap<0||next.cross<=crossingCap)&&(delta<=0||unit(rng)<std::exp(-delta/moveTemperature))) {
          current=next;accepted++;if(delta>0)uphill++;
          const bool bestImproved=bestWeighted
            ? current.cost(hitWeight)<best.cost(hitWeight)||(current.cost(hitWeight)==best.cost(hitWeight)&&better(current,best))
            : better(current,best);
          if(bestImproved){best=current;bestPos=s.pos;}
        }else{for(size_t i=0;i<moving.size();i++)s.pos[moving[i]]=old[i];s.update(affected);}
      }
      for(int n:moving)s.moved[n]=false;for(int e:affected)s.changedEdge[e]=false;
      if(step%verifyEvery==0||step%cycleLength==0){
        Score exact=s.full(&pressure);if(!equal(exact,current))throw std::runtime_error("incremental score drift");
        report("progress",step,best,elapsed());
        if(temporarySpacing)report("exploration",step,current,elapsed());
        auto active=s.pos;s.pos=bestPos;s.allRoutes();s.save(output);
        if(step%cycleLength==0)current=best;else{s.pos=std::move(active);s.allRoutes();}
      }
      if(best.visual()<=500&&best.hit==0&&best.spacing==0)break;
    }
    s.pos=bestPos;s.allRoutes();Score exact=s.full();if(!equal(exact,best))throw std::runtime_error("final score drift");
    if(number(argc,argv,"--hub-rounds",0)!=0)exact=runHubRelocation(s,argc,argv,
      [&]{return elapsed()>=seconds-std::min(5.,seconds*.1);},elapsed,output);
    if(number(argc,argv,"--joint-rounds",0)!=0)exact=runJointPositionSearch(s,argc,argv,
      [&]{return elapsed()>=seconds-std::min(5.,seconds*.1);},elapsed,output);
    const int projections=int(number(argc,argv,"--global-projections",0));
    if(projections<0)throw std::runtime_error("invalid global projection limit");
    if(projections)exact=searchGlobalProjections(s,projections,uint64_t(number(argc,argv,"--seed",42)),
      [&]{return elapsed()>=seconds-std::min(5.,seconds*.1);},elapsed,output);
    const int shellRounds=int(number(argc,argv,"--shell-rounds",0));
    const int shellSamples=int(number(argc,argv,"--shell-candidates",700));
    const double shellLookahead=number(argc,argv,"--shell-lookahead",0);
    if(shellRounds<0||shellSamples<1||!std::isfinite(shellLookahead)||shellLookahead<0||shellLookahead>1)throw std::runtime_error("invalid shell search options");
    if(shellRounds)exact=reconstructPeeledShell(s,shellRounds,shellSamples,uint64_t(number(argc,argv,"--seed",42)),
      shellLookahead,[&]{return elapsed()>=seconds-std::min(5.,seconds*.1);},elapsed,output);
    NeighborhoodOptions neighborhood;
    neighborhood.rounds=int(number(argc,argv,"--neighborhood-rounds",0));
    neighborhood.steps=int(number(argc,argv,"--patch-steps",10000));
    neighborhood.size=int(number(argc,argv,"--patch-size",60));
    neighborhood.temperature=number(argc,argv,"--patch-temperature",8);
    neighborhood.hitWeight=hitWeight;
    neighborhood.reorder=number(argc,argv,"--patch-reorder",1)!=0;
    neighborhood.destroy=number(argc,argv,"--patch-destroy",1)!=0;
    neighborhood.assignment=number(argc,argv,"--patch-assignment",0)!=0;
    neighborhood.starCandidates=int(number(argc,argv,"--patch-star-candidates",0));
    neighborhood.reinsertCandidates=int(number(argc,argv,"--patch-reinsert-candidates",0));
    neighborhood.seed=uint64_t(number(argc,argv,"--seed",42));
    if(neighborhood.rounds<0||neighborhood.steps<0||neighborhood.starCandidates<0||neighborhood.reinsertCandidates<0||neighborhood.size<2||neighborhood.size>240||!std::isfinite(neighborhood.temperature)||neighborhood.temperature<=0)throw std::runtime_error("invalid neighborhood search options");
    if(neighborhood.rounds){
      NeighborhoodSearch search(s,neighborhood);
      exact=search.run([&]{return elapsed()>=seconds-std::min(5.,seconds*.1);},elapsed,output);
    }
    const long portSteps=long(number(argc,argv,"--port-steps",0));
    if(portSteps<0)throw std::runtime_error("port steps must be nonnegative");
    const auto initialPorts=arg(argc,argv,"--initial-ports","none");
    if(initialPorts!="none"&&(steps||projections||shellRounds||neighborhood.rounds||number(argc,argv,"--joint-rounds",0)||number(argc,argv,"--hub-rounds",0)))
      throw std::runtime_error("initial ports require a port-only or linear-separation run");
    exact=loadBoundaryPorts(s,initialPorts,exact);
    if(number(argc,argv,"--slot-rounds",0)!=0) {
      if(crossingCap>=0)throw std::runtime_error("crossing cap does not apply to slot assignments");
      exact=runSlotAssignments(s,argc,argv,[&]{return elapsed()>=seconds-std::min(5.,seconds*.1);},elapsed,output);
    }
    if(arg(argc,argv,"--crossover-positions","none")!="none")exact=runSceneCrossover(s,argc,argv,
      [&]{return elapsed()>=seconds-std::min(5.,seconds*.1);},elapsed,output);
    if(number(argc,argv,"--adaptive-steps",0)!=0)exact=runAdaptivePortSearch(s,argc,argv,
      [&]{return elapsed()>=seconds-std::min(5.,seconds*.1);},elapsed,output);
    if(number(argc,argv,"--linear-rounds",0)!=0) {
#ifdef ERD_ENABLE_LINEAR_SEARCH
      exact=searchLinearSeparations(s,argc,argv,elapsed,output);
#else
      throw std::runtime_error("linear separation requires a COIN-enabled research build");
#endif
    }
    exact=refineBoundaryPorts(s,exact,portSteps,rng,[&]{return elapsed()>=seconds;},elapsed);
    if(number(argc,argv,"--detour-rounds",0)!=0||arg(argc,argv,"--detour-initial-routes","none")!="none") {
      if(crossingCap>=0)throw std::runtime_error("crossing cap does not apply to detour research");
      exact=runBoundedDetours(s,argc,argv,[&]{return elapsed()>=seconds;},elapsed,output);
    } else s.save(output);
    report("done",completed,exact,elapsed());
    if(groupRepair)std::cerr<<"group-repair attempts="<<repairAttempts<<" feasible="<<repairSuccess<<'\n';
    std::cerr<<"accepted="<<accepted<<" uphill="<<uphill<<" nodes="<<s.nodes.size()<<" edges="<<s.edges.size()<<" groups="<<s.groups.size()<<" canvas="<<s.width*s.height<<'\n';
    return 0;
  }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
