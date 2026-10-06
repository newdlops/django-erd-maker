#pragma once
// Acyclic coordinate-rank constraints preserve every real card's X/Y gap.
// Neural positions are already pixels; no occupancy or old-bbox rescaling.
#include "sourceInputLayout.h"
#include "sourceInputLayoutFeatures.h"
#include <numeric>
#include <set>
#include <tuple>
namespace source_input_spacing_details {
using Point = std::pair<double, double>;
using source_input_layout::check;
struct SpacingResult {
  std::vector<djerd::StraightVisualNode> nodes;
  std::size_t passes=0,constraints=0,pairChecks=0;
  bool complete=false,deadlineHit=false;
  double scale=0,maximumDisplacement=0,minimumClearance=0,bboxArea=0;
};
inline double bboxArea(const std::vector<djerd::StraightVisualNode>& nodes){
  double left=std::numeric_limits<double>::infinity(),top=left,right=-left,bottom=-left;
  for(auto n:nodes){left=std::min(left,n.x-n.width/2);right=std::max(right,n.x+n.width/2);
    top=std::min(top,n.y-n.height/2);bottom=std::max(bottom,n.y+n.height/2);}
  return (right-left)*(bottom-top);
}
}
namespace source_input_pixel_spacing {
inline bool productSeparated(const djerd::StraightVisualNode& a,
    const djerd::StraightVisualNode& b, double gapX, double gapY) {
  return std::abs(a.x-b.x) >= (a.width+b.width)/2 + gapX
    || std::abs(a.y-b.y) >= (a.height+b.height)/2 + gapY;
}
template<class Expired> source_input_spacing_details::SpacingResult space(
    const std::vector<djerd::StraightVisualNode>& original,const std::vector<source_input_spacing_details::Point>& points,
    const std::vector<std::string>& ids,Expired expired){
  namespace sn=source_input_spacing_details;
  sn::check(!points.empty()&&original.size()==points.size()&&ids.size()==points.size(),"source NN spacing source rows missing");
  sn::SpacingResult result;result.nodes=original;result.scale=1;
  for(std::size_t n=0;n<points.size();++n){sn::check(std::isfinite(points[n].first)&&std::isfinite(points[n].second),"source NN point nonfinite");
    result.nodes[n].x=points[n].first+.137*std::sin(double(n)*1.23);result.nodes[n].y=points[n].second+.129*std::cos(double(n)*1.07);}
  const auto desired=result.nodes;std::vector<std::size_t> xo(points.size()),yo(points.size()),xr(points.size()),yr(points.size());std::iota(xo.begin(),xo.end(),0);std::iota(yo.begin(),yo.end(),0);
  std::sort(xo.begin(),xo.end(),[&](auto a,auto b){return desired[a].x!=desired[b].x?desired[a].x<desired[b].x:ids[a]<ids[b];});
  std::sort(yo.begin(),yo.end(),[&](auto a,auto b){return desired[a].y!=desired[b].y?desired[a].y<desired[b].y:ids[a]<ids[b];});
  for(std::size_t k=0;k<points.size();++k){xr[xo[k]]=k;yr[yo[k]]=k;}
  std::vector<std::vector<std::pair<std::size_t,double>>> xa(points.size()),ya(points.size());std::set<std::tuple<std::size_t,std::size_t,int>> constraints;
  for(;result.passes<64;++result.passes){bool added=false;
    for(std::size_t a=0;a<points.size();++a){if(a%16==0&&expired()){result.nodes.clear();result.deadlineHit=true;return result;}
      for(std::size_t b=a+1;b<points.size();++b){++result.pairChecks;if(productSeparated(result.nodes[a],result.nodes[b],56.04,42.04))continue;
        const auto xf=xr[a]<xr[b]?a:b,xs=xf==a?b:a,yf=yr[a]<yr[b]?a:b,ys=yf==a?b:a;
        const auto xgap=(original[a].width+original[b].width)/2+56.04,ygap=(original[a].height+original[b].height)/2+42.04;
        const auto xdebt=result.nodes[xf].x+xgap-result.nodes[xs].x,ydebt=result.nodes[yf].y+ygap-result.nodes[ys].y;
        if(xdebt<=ydebt){if(constraints.emplace(xf,xs,0).second){xa[xf].push_back({xs,xgap});added=true;}}
        else if(constraints.emplace(yf,ys,1).second){ya[yf].push_back({ys,ygap});added=true;}
      }
    }
    if(!added){result.complete=true;break;}result.nodes=desired;
    for(auto n:xo)for(auto [other,gap]:xa[n])result.nodes[other].x=std::max(result.nodes[other].x,result.nodes[n].x+gap);
    for(auto n:yo)for(auto [other,gap]:ya[n])result.nodes[other].y=std::max(result.nodes[other].y,result.nodes[n].y+gap);
  }
  result.constraints=constraints.size();
  if(result.complete){result.minimumClearance=std::numeric_limits<double>::infinity();
    for(std::size_t a=0;a<points.size();++a){result.maximumDisplacement=std::max(result.maximumDisplacement,std::hypot(result.nodes[a].x-desired[a].x,result.nodes[a].y-desired[a].y));
      for(std::size_t b=a+1;b<points.size();++b){sn::check(productSeparated(result.nodes[a],result.nodes[b],56.03999999,42.03999999),"source NN original pair clearance missing");
        const double dx=std::max(0.,std::abs(result.nodes[a].x-result.nodes[b].x)-(original[a].width+original[b].width)/2),dy=std::max(0.,std::abs(result.nodes[a].y-result.nodes[b].y)-(original[a].height+original[b].height)/2);
        result.minimumClearance=std::min(result.minimumClearance,std::hypot(dx,dy));}
    }result.bboxArea=sn::bboxArea(result.nodes);
  }else result.nodes.clear();return result;
}
} // namespace source_input_pixel_spacing
