#include "sourceInputLayout.h"
#include "sourceInputLayoutFeatures.h"
#include "sourceInputLayoutSpacing.h"
#include <cstring>
#include <fstream>
#include <functional>
namespace djerd::source_input {
namespace si=source_input_layout;
struct Model {
  std::array<double,64> mean{},scale{};std::array<double,8450> p{};
  explicit Model(const std::string& path){
    std::ifstream file(path,std::ios::binary);si::check(bool(file),"source-model-unavailable");char magic[8]{};std::array<std::uint32_t,4> shape{};
    file.read(magic,8);file.read(reinterpret_cast<char*>(shape.data()),sizeof(shape));si::check(std::memcmp(magic,"SRCLAY01",8)==0&&shape==std::array<std::uint32_t,4>{64,64,2,8450},"source-model-schema");
    file.read(reinterpret_cast<char*>(mean.data()),sizeof(mean));file.read(reinterpret_cast<char*>(scale.data()),sizeof(scale));file.read(reinterpret_cast<char*>(p.data()),sizeof(p));
    si::check(bool(file)&&file.peek()==std::char_traits<char>::eof(),"source-model-incomplete");
    for(std::size_t k=0;k<64;++k)si::check(std::isfinite(mean[k])&&std::isfinite(scale[k])&&scale[k]>0,"source-model-normalization");for(double value:p)si::check(std::isfinite(value),"source-model-parameter");
  }
  std::pair<double,double> forward(const si::Features& input)const{
    std::array<double,64> x,first,second;for(std::size_t k=0;k<64;++k)x[k]=std::clamp((input[k]-mean[k])/scale[k],-8.0,8.0);
    for(std::size_t j=0;j<64;++j){double value=p[4096+j];for(std::size_t k=0;k<64;++k)value+=x[k]*p[k*64+j];first[j]=std::tanh(value);}
    for(std::size_t j=0;j<64;++j){double value=p[8256+j];for(std::size_t k=0;k<64;++k)value+=first[k]*p[4160+k*64+j];second[j]=std::tanh(value);}
    std::array<double,2> result;for(std::size_t j=0;j<2;++j){double value=p[8448+j];for(std::size_t k=0;k<64;++k)value+=second[k]*p[8320+k*2+j];si::check(std::isfinite(value),"source-model-prediction");result[j]=value;}
    return {result[0],result[1]};
  }
};
static double orientation(double ax,double ay,double bx,double by,double x,double y){return (bx-ax)*(y-ay)-(by-ay)*(x-ax);}
static bool crossing(const djerd::StraightVisualRoute& a,const djerd::StraightVisualRoute& b){
  return orientation(a.sourceX,a.sourceY,a.targetX,a.targetY,b.sourceX,b.sourceY)*orientation(a.sourceX,a.sourceY,a.targetX,a.targetY,b.targetX,b.targetY)<-1e-9
    &&orientation(b.sourceX,b.sourceY,b.targetX,b.targetY,a.sourceX,a.sourceY)*orientation(b.sourceX,b.sourceY,b.targetX,b.targetY,a.targetX,a.targetY)<-1e-9;
}
Seed propose(const std::vector<djerd::StraightVisualNode>& original,const std::vector<djerd::StraightVisualEdge>& edges,
    const std::vector<std::string>& ids,const std::string& modelPath,std::chrono::steady_clock::time_point deadline){
  const auto began=std::chrono::steady_clock::now();Seed result;const auto expired=[&]{return std::chrono::steady_clock::now()>=deadline;};
  try{
    si::check(original.size()==ids.size()&&!original.empty(),"source-model-originals");si::check(!expired(),"source-model-deadline");Model model(modelPath);si::Source source;
    for(std::size_t n=0;n<original.size();++n)source.nodes.push_back({ids[n],original[n].width,original[n].height});
    for(std::size_t e=0;e<edges.size();++e)source.edges.push_back({std::to_string(e),edges[e].source,edges[e].target});
    const auto built=si::features(source);std::vector<source_input_spacing_details::Point> points;points.reserve(original.size());
    for(std::size_t n=0;n<original.size();++n){if(n%32==0)si::check(!expired(),"source-model-deadline");const auto p=model.forward(built.rows[n]);points.push_back({p.first*built.physicalUnit,p.second*built.physicalUnit});}
    const auto physical=source_input_pixel_spacing::space(original,points,ids,expired);si::check(physical.complete&&!expired(),"source-model-spacing-deadline");
    djerd::StraightVisualState state(physical.nodes,edges);const auto score=djerd::measureStraightVisualFull(physical.nodes,edges);
    si::check(!score.nodeOverlaps&&!score.invalidRoutes,"source-model-original-route-invalid");
    std::vector<std::vector<std::size_t>> incident(original.size());for(std::size_t e=0;e<edges.size();++e){incident.at(edges[e].source).push_back(e);incident.at(edges[e].target).push_back(e);}
    for(const auto& list:incident)for(std::size_t i=0;i<list.size();++i)for(std::size_t j=i+1;j<list.size();++j)
      si::check(!crossing(state.routes()[list[i]],state.routes()[list[j]]),"source-model-adjacent-route-invalid");
    si::check(!expired(),"source-model-complete-audit-deadline");result.nodes=physical.nodes;result.complete=true;result.reason="complete-original-source";
  }catch(const std::exception& error){result.nodes.clear();result.reason=error.what();}
  result.elapsedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();return result;
}
} // namespace djerd::source_input
