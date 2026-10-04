// Exact environment for a learned continuous policy. Runtime actions come
// exclusively from stdin; there is no native proposal generator or repair.
#include "constrained_dual_node_geometry.h"

namespace {
struct Trial {bool legal=false,accepted=false;long gain=0,individualGain=0;};
struct Environment {
  State a,b;std::vector<int> mapping,eligible;std::vector<Point> initial;
  Score baseline,baselineIndividual;long initialHard=0,initialIndividualHard=0,visual=0,individualVisual=0;
  long attempts=0,accepted=0;double left=1e100,right=-1e100,top=1e100,bottom=-1e100,radius=384;
  explicit Environment(State x,State y,const std::set<std::string>& singleton):a(std::move(x)),b(std::move(y)) {
    initial=a.pos;mapping.assign(a.nodes.size(),-1);std::unordered_map<std::string,int> nodes,edges;
    for(int n=0;n<int(b.nodes.size());n++)nodes[b.nodes[n].id]=n;
    for(int e=0;e<int(b.edges.size());e++)edges[b.edges[e].id]=e;
    for(int n=0;n<int(a.nodes.size());n++) {
      left=std::min(left,a.pos[n].x-a.nodes[n].w/2);right=std::max(right,a.pos[n].x+a.nodes[n].w/2);
      top=std::min(top,a.pos[n].y-a.nodes[n].h/2);bottom=std::max(bottom,a.pos[n].y+a.nodes[n].h/2);
      if(!nodes.count(a.nodes[n].id))continue;const int m=nodes.at(a.nodes[n].id);
      bool valid=boundarySame(a.pos[n],b.pos[m])&&a.nodes[n].w==b.nodes[m].w&&a.nodes[n].h==b.nodes[m].h
        &&a.incident[n].size()==b.incident[m].size();
      for(int e:a.incident[n]) {
        if(!singleton.count(a.edges[e].id)||!edges.count(a.edges[e].id)){valid=false;break;}
        const int f=edges.at(a.edges[e].id);
        valid=valid&&a.nodes[a.edges[e].s].id==b.nodes[b.edges[f].s].id&&a.nodes[a.edges[e].t].id==b.nodes[b.edges[f].t].id
          &&boundarySame(a.routes[e].a,b.routes[f].a)&&boundarySame(a.routes[e].b,b.routes[f].b);
      }
      if(valid){mapping[n]=m;eligible.push_back(n);}
    }
    baseline=a.full();baselineIndividual=b.full();initialHard=hardScore(a);initialIndividualHard=hardScore(b);
    visual=baseline.visual();individualVisual=baselineIndividual.visual();
  }
  NodeCost cost(int n) {moving(a,n,true);auto c=localNode(a,n);moving(a,n,false);return c;}
  std::vector<double> features(int n) {
    // Relative geometry only: no model names, indices, absolute coordinates,
    // graph identity, candidate actions, labels, or future measured outcomes.
    const auto p=a.pos[n];const auto c=cost(n);std::vector<double> v;
    const auto add=[&](double x){v.push_back(std::clamp(x,-8.,8.));};
    add(a.nodes[n].w/256);add(a.nodes[n].h/256);add(std::log1p(a.incident[n].size()));
    add(std::log1p(c.score.cross));add(std::log1p(c.score.hit));add(std::log1p(c.hard));
    double mx=0,my=0,xx=0,yy=0,minLength=1e100,maxLength=0;
    for(int e:a.incident[n]) {
      const Point q=a.edges[e].s==n?a.routes[e].b:a.routes[e].a;
      const double dx=q.x-p.x,dy=q.y-p.y,len=std::max(.01,std::hypot(dx,dy));
      mx+=dx/len;my+=dy/len;xx+=dx*dx/(len*len);yy+=dy*dy/(len*len);minLength=std::min(minLength,len);maxLength=std::max(maxLength,len);
    }
    const double count=std::max(size_t(1),a.incident[n].size());
    add(mx/count);add(my/count);add(xx/count);add(yy/count);add(minLength==1e100?0:minLength/1024);add(maxLength/1024);
    std::vector<std::pair<double,int>> nearby;
    for(int m=0;m<int(a.nodes.size());m++)if(m!=n)nearby.push_back({std::hypot(a.pos[m].x-p.x,a.pos[m].y-p.y),m});
    std::sort(nearby.begin(),nearby.end());
    for(int k=0;k<4;k++) {
      if(k>=int(nearby.size())){for(int j=0;j<4;j++)add(0);continue;}
      const int m=nearby[k].second;add((a.pos[m].x-p.x)/512);add((a.pos[m].y-p.y)/512);add(a.nodes[m].w/256);add(a.nodes[m].h/256);
    }
    struct Conflict {int edge;bool hit;long cross;double distance;};std::vector<Conflict> conflicts;
    for(int f=0;f<int(a.edges.size());f++)if(a.edges[f].s!=n&&a.edges[f].t!=n) {
      const auto line=a.routes[f];const bool hit=hits(line,p,a.nodes[n]);long crossing=0;
      for(int e:a.incident[n])crossing+=crosses(a.routes[e],line);
      if(!hit&&!crossing)continue;
      const double dx=line.b.x-line.a.x,dy=line.b.y-line.a.y;
      const double t=std::clamp(((p.x-line.a.x)*dx+(p.y-line.a.y)*dy)/std::max(.01,dx*dx+dy*dy),0.,1.);
      conflicts.push_back({f,hit,crossing,std::hypot(p.x-line.a.x-t*dx,p.y-line.a.y-t*dy)});
    }
    std::stable_sort(conflicts.begin(),conflicts.end(),[](auto x,auto y){return x.distance<y.distance;});
    for(int k=0;k<4;k++) {
      if(k>=int(conflicts.size())){for(int j=0;j<6;j++)add(0);continue;}
      const auto c=conflicts[k];auto line=a.routes[c.edge];
      if(line.a.x>line.b.x||(line.a.x==line.b.x&&line.a.y>line.b.y))std::swap(line.a,line.b);
      add((line.a.x-p.x)/1024);add((line.a.y-p.y)/1024);add((line.b.x-p.x)/1024);add((line.b.y-p.y)/1024);add(c.hit);add(std::log1p(c.cross));
    }
    add((p.x-a.nodes[n].w/2-left)/512);add((right-p.x-a.nodes[n].w/2)/512);
    add((p.y-a.nodes[n].h/2-top)/512);add((bottom-p.y-a.nodes[n].h/2)/512);
    add((p.x-initial[n].x)/radius);add((p.y-initial[n].y)/radius);
    return v; // schema v1: 58 features
  }
  Trial trial(int n,Point delta,bool commit) {
    if(n<0||n>=int(mapping.size())||mapping[n]<0||!std::isfinite(delta.x)||!std::isfinite(delta.y))
      throw std::runtime_error("invalid external policy action");
    attempts++;const int m=mapping[n];const auto origin=a.pos[n];const Point p{rounded(origin.x+delta.x),rounded(origin.y+delta.y)};
    if(std::hypot(p.x-initial[n].x,p.y-initial[n].y)>radius||p.x-a.nodes[n].w/2<left||p.x+a.nodes[n].w/2>right
      ||p.y-a.nodes[n].h/2<top||p.y+a.nodes[n].h/2>bottom)return {};
    moving(a,n,true);moving(b,m,true);const auto beforeA=localNode(a,n),beforeB=localNode(b,m);
    const auto ra=incidentRoutes(a,n),rb=incidentRoutes(b,m);translate(a,n,origin,p,ra);translate(b,m,origin,p,rb);Trial result;
    if(spaced(a,n)&&spaced(b,m)) {
      const auto afterA=localNode(a,n),afterB=localNode(b,m);result.gain=beforeA.score.visual()-afterA.score.visual();result.individualGain=beforeB.score.visual()-afterB.score.visual();
      result.legal=afterA.hard<=beforeA.hard&&afterB.hard<=beforeB.hard;
      result.accepted=result.legal&&result.gain>=0&&result.individualGain>=0&&(result.gain||result.individualGain);
    }
    if(commit&&result.accepted){visual-=result.gain;individualVisual-=result.individualGain;accepted++;}
    else {translate(a,n,origin,origin,ra);translate(b,m,origin,origin,rb);}
    moving(a,n,false);moving(b,m,false);return result;
  }
  void save(const std::string& output) {
    const auto final=a.full(),full=b.full();const long h=hardScore(a),ih=hardScore(b);
    if(final.visual()!=visual||full.visual()!=individualVisual||visual>baseline.visual()||individualVisual>baselineIndividual.visual()
      ||h>initialHard||ih>initialIndividualHard||final.overlap||final.spacing||full.overlap||full.spacing)throw std::runtime_error("policy rollout audit failed");
    a.save(output);b.save(output+".individual");
    std::ofstream out(output+".stats.json");out<<"{\"initialVisual\":"<<baseline.visual()<<",\"visual\":"<<visual<<",\"cross\":"<<final.cross<<",\"hit\":"<<final.hit
      <<",\"initialIndividualVisual\":"<<baselineIndividual.visual()<<",\"individualVisual\":"<<individualVisual<<",\"initialHardConditions\":"<<initialHard
      <<",\"hardConditions\":"<<h<<",\"initialIndividualHardConditions\":"<<initialIndividualHard<<",\"individualHardConditions\":"<<ih
      <<",\"overlap\":0,\"spacing\":0,\"maxDisplacement\":"<<radius<<",\"policyActionsEvaluated\":"<<attempts<<",\"acceptedActions\":"<<accepted
      <<",\"heuristicSearchCalls\":0,\"proposalAuthority\":\"external-policy\"}\n";
  }
};
void array(std::ostream& out,const std::vector<double>& v) {out<<'[';for(size_t i=0;i<v.size();i++){if(i)out<<',';out<<v[i];}out<<']';}
State synthetic(uint64_t seed) {
  std::mt19937_64 rng(seed);State s;const int count=24+rng()%17,columns=6;
  s.nodes.resize(count);s.pos.resize(count);s.incident.resize(count);s.adj.resize(count);s.moved.resize(count);
  for(int n=0;n<count;n++) {
    s.nodes[n]={std::to_string(n),double(100+rng()%181),double(50+rng()%121)};
    s.pos[n]={double((n%columns)*480+240+int(rng()%97)-48),double((n/columns)*320+160+int(rng()%49)-24)};
  }
  std::set<std::pair<int,int>> pairs;
  while(pairs.size()<size_t(count*1.4)) {
    int n=rng()%count,m=rng()%count;if(n==m)continue;if(n>m)std::swap(n,m);
    if(rng()%5&&std::hypot(s.pos[n].x-s.pos[m].x,s.pos[n].y-s.pos[m].y)>1500)continue;
    pairs.insert({n,m});
  }
  for(auto [n,m]:pairs) {
    const int e=s.edges.size();s.edges.push_back({std::to_string(e),n,m});s.incident[n].push_back(e);s.incident[m].push_back(e);
  }
  s.changedEdge.resize(s.edges.size());s.allRoutes();return s;
}
void dataset(int argc,char**argv) {
  const auto file=arg(argc,argv,"--dataset");const int graphs=number(argc,argv,"--graphs",64),samples=number(argc,argv,"--samples",32);
  const uint64_t seed=number(argc,argv,"--seed",27182);const double seconds=number(argc,argv,"--seconds",20);
  if(graphs<8||graphs>256||samples<4||samples>128||seconds<=0||seconds>60)throw std::runtime_error("invalid dataset budget");
  const auto start=std::chrono::steady_clock::now();std::mt19937_64 rng(seed);std::uniform_real_distribution<double> unit(0,1);
  std::ofstream out(file);if(!out)throw std::runtime_error("cannot write dataset");out<<std::setprecision(8);
  long rows=0,actions=0,positives=0;int completed=0;
  for(int g=0;g<graphs;g++) {
    if(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=seconds)break;
    State a=synthetic(seed+g);std::set<std::string> ids;for(auto e:a.edges)ids.insert(e.id);Environment env(a,a,ids);
    for(int n:env.eligible)if(env.cost(n).score.visual()) {
      out<<"{\"graph\":"<<g<<",\"features\":";array(out,env.features(n));out<<",\"actions\":[";
      for(int k=0;k<samples;k++) {
        const double angle=unit(rng)*6.283185307179586,radius=std::exp(std::log(8.)+unit(rng)*std::log(384./8));
        const Point delta{std::cos(angle)*radius,std::sin(angle)*radius};const auto result=env.trial(n,delta,false);
        const bool good=result.legal&&result.gain>0&&result.individualGain>=0;
        if(k)out<<',';out<<'['<<delta.x/384<<','<<delta.y/384<<','<<(good?result.gain:0)<<']';actions++;positives+=good;
      }
      out<<"]}\n";rows++;
    }
    completed++;
  }
  std::cout<<"{\"graphs\":"<<completed<<",\"states\":"<<rows<<",\"randomActions\":"<<actions<<",\"positiveActions\":"<<positives
    <<",\"seed\":"<<seed<<",\"seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<"}\n";
}
void featureTest() {
  State a=synthetic(88123);std::set<std::string> ids;for(auto e:a.edges)ids.insert(e.id);Environment original(a,a,ids);
  auto shifted=a;for(auto& p:shifted.pos){p.x+=5000;p.y-=3000;}for(auto& line:shifted.routes)line=segment({line.a.x+5000,line.a.y-3000},{line.b.x+5000,line.b.y-3000});
  Environment translated(shifted,shifted,ids);
  for(int n:original.eligible) {const auto x=original.features(n),y=translated.features(n);if(x.size()!=58)throw std::runtime_error("wrong observation schema");
    for(size_t i=0;i<x.size();i++)if(std::abs(x[i]-y[i])>1e-7)throw std::runtime_error("absolute coordinates leaked into policy features");}
  std::cout<<"{\"featureTranslationInvariance\":\"pass\",\"featureCount\":58}\n";
}
}
int main(int argc,char**argv) {
  try {
    if(argc==2&&std::string(argv[1])=="--self-test"){selfTest();featureTest();return 0;}
    if(arg(argc,argv,"--dataset","none")!="none"){dataset(argc,argv);return 0;}
    const auto output=arg(argc,argv,"--out"),syntheticSeed=arg(argc,argv,"--synthetic-seed","none");
    std::set<std::string> ids;std::string line;State first,second;
    if(syntheticSeed!="none") {first=synthetic(std::stoull(syntheticSeed));second=first;for(auto e:first.edges)ids.insert(e.id);}
    else {
      const auto directory=arg(argc,argv,"--directory");
      std::ifstream allowed(directory+"/eligible-singletons.txt");if(!allowed)throw std::runtime_error("missing singleton mask");while(std::getline(allowed,line))if(!line.empty())ids.insert(line);
      first=loadView(directory,"");second=loadView(directory,"individual.");
    }
    Environment env(std::move(first),std::move(second),ids);
    std::cout<<std::setprecision(10)<<"{\"ready\":true,\"eligible\":"<<env.eligible.size()<<",\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<"}"<<std::endl;
    while(std::getline(std::cin,line)) {
      std::istringstream command(line);std::string op;command>>op;
      if(op=="OBS") {
        std::cout<<"{\"nodes\":[";bool first=true;
        for(int n:env.eligible)if(env.cost(n).score.visual()) {
          if(!first)std::cout<<',';first=false;std::cout<<"{\"id\":"<<n<<",\"features\":";array(std::cout,env.features(n));std::cout<<'}';
        }
        std::cout<<"],\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<"}"<<std::endl;
      }else if(op=="TRY") {
        int n;Point delta;if(!(command>>n>>delta.x>>delta.y))throw std::runtime_error("invalid policy command");const auto r=env.trial(n,delta,true);
        std::cout<<"{\"accepted\":"<<(r.accepted?"true":"false")<<",\"legal\":"<<(r.legal?"true":"false")<<",\"gain\":"<<r.gain
          <<",\"individualGain\":"<<r.individualGain<<",\"visual\":"<<env.visual<<",\"individualVisual\":"<<env.individualVisual<<"}"<<std::endl;
      }else if(op=="SAVE") {env.save(output);std::cout<<"{\"saved\":true}"<<std::endl;}
      else if(op=="QUIT")return 0;else throw std::runtime_error("unknown policy command");
    }
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
