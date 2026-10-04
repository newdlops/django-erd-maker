// A coordinated move otherwise fails whenever any one follower hits a card.
// Repair only moved cards, within a bounded radius; fixed cards never shift.
namespace {
bool repairGroupProposal(State& s,const std::vector<int>& moving,std::vector<Point>& proposed) {
  const auto original=s.pos;
  std::vector<char> pending(s.nodes.size(),false);
  for(int n:moving)pending[n]=true;
  std::vector<int> order(moving.size());std::iota(order.begin(),order.end(),0);
  std::stable_sort(order.begin(),order.end(),[&](int a,int b){
    int n=moving[a],m=moving[b];
    return s.adj[n].size()>s.adj[m].size();
  });
  bool valid=true;
  for(int i:order) {
    int n=moving[i];const auto wanted=proposed[i];
    const auto free=[&](Point p) {
      if(!s.inside(n,p))return false;
      s.pos[n]=p;
      for(int other=0;other<int(s.nodes.size());other++)
        if(other!=n&&!pending[other]&&s.pair(n,other).spacing)return false;
      return true;
    };
    bool found=free(wanted);
    for(int ring=1;!found&&ring<=8;ring++)for(int angle=0;!found&&angle<16;angle++) {
      double theta=angle*6.283185307179586/16;
      found=free({wanted.x+ring*65*std::cos(theta),wanted.y+ring*65*std::sin(theta)});
    }
    if(!found){valid=false;break;}
    proposed[i]=s.pos[n];pending[n]=false;
  }
  s.pos=original;
  return valid;
}
}
