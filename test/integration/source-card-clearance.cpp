#include "straightVisualOptimization.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <unistd.h>
using namespace djerd;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static bool independent(const std::vector<StraightVisualNode>& cards,double minimumX,double minimumY){
  for(std::size_t a=0;a<cards.size();++a)for(std::size_t b=a+1;b<cards.size();++b){
    const double dx=std::abs(cards[a].x-cards[b].x)-(cards[a].width+cards[b].width)/2;
    const double dy=std::abs(cards[a].y-cards[b].y)-(cards[a].height+cards[b].height)/2;
    if(dx<minimumX-1e-7&&dy<minimumY-1e-7)return false;
  }return true;
}
int main(){try{
  alarm(15);const double gapX=56.04,gapY=42.04;
  std::vector<StraightVisualNode> cards;
  for(std::size_t n=0;n<40;++n)cards.push_back({60+double(n%4)*17,70+double(n%6)*23,double(n%8)*400,double(n/8)*400});
  StraightVisualState state(cards,{});require(state.allCardsHaveClearance(gapX,gapY),"synthetic initial gap missing");
  std::mt19937_64 random(42);std::size_t checks=0,rejected[3]{};
  for(std::size_t step=0;step<3000;++step){
    const auto first=random()%cards.size(),second=(first+1+random()%(cards.size()-1))%cards.size();
    auto proposed=state.nodes();bool actual=false;const auto kind=step%3;
    const double x=double(int(random()%450001)-100000)/100,y=double(int(random()%350001)-100000)/100;
    std::vector<StraightVisualMove> group;
    if(kind==0){proposed[first].x=x;proposed[first].y=y;actual=state.canMoveWithCardClearance(first,x,y,gapX,gapY);}
    else if(kind==1){std::swap(proposed[first].x,proposed[second].x);std::swap(proposed[first].y,proposed[second].y);actual=state.canSwapWithCardClearance(first,second,gapX,gapY);}
    else{
      const double dx=x-state.nodes()[first].x,dy=y-state.nodes()[first].y;
      group={{first,x,y},{second,state.nodes()[second].x+dx,state.nodes()[second].y+dy}};
      for(const auto& move:group){proposed[move.node].x=move.x;proposed[move.node].y=move.y;}
      actual=state.canMoveManyWithCardClearance(group,gapX,gapY);
    }
    require(actual==independent(proposed,gapX,gapY),"indexed simultaneous card guard differs from independent pair gap");++checks;
    if(!actual){++rejected[kind];continue;}
    if(kind==0)state.move(first,x,y);else if(kind==1)state.swap(first,second);else state.moveMany(group);
    require(independent(state.nodes(),gapX,gapY)&&state.allCardsHaveClearance(gapX,gapY),"accepted card move or index update loses required gap");
  }
  require(rejected[0]>0&&rejected[1]>0&&rejected[2]>0,"guard rejection cases missing");
  const double nan=std::numeric_limits<double>::quiet_NaN();
  require(!state.canMoveWithCardClearance(0,nan,0,gapX,gapY)&&!state.canMoveWithCardClearance(40,0,0,gapX,gapY)
    &&!state.canSwapWithCardClearance(0,0,gapX,gapY)&&!state.canSwapWithCardClearance(0,1,nan,gapY)
    &&!state.canMoveManyWithCardClearance({{0,0,0},{0,1,1}},gapX,gapY)&&!state.allCardsHaveClearance(-1,gapY),"invalid card proposals do not fail closed");
  StraightVisualState boundary({{20,20,0,0},{20,20,76.04,0}},{});
  require(boundary.allCardsHaveClearance(gapX,gapY)&&!boundary.canMoveWithCardClearance(1,76.02,0,gapX,gapY)
    &&boundary.canMoveWithCardClearance(1,76.08,0,gapX,gapY)
    &&boundary.canMoveWithCardClearance(1,0,62.04,gapX,gapY)
    &&!boundary.canMoveWithCardClearance(1,0,62.02,gapX,gapY),"exact card clearance boundary differs");
  std::cout<<"{\"independentIndexedCardClearanceChecks\":"<<checks<<",\"singleSwapAndSimultaneousGroupsVerified\":true,\"acceptedMovesAndIndexUpdatesKeepGap\":true,\"roundoffBoundaryAndInvalidProposalsVerified\":true,\"singleRejected\":"<<rejected[0]<<",\"swapsRejected\":"<<rejected[1]<<",\"groupsRejected\":"<<rejected[2]<<"}\n";
  alarm(0);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
