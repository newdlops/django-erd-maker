#pragma once
#include "straightVisualOptimization.h"
#include <chrono>
#include <string>
#include <vector>
namespace djerd::source_input {
// Positions supplied by a previous layout are never inputs to this model.
constexpr double kCardGapX = 56.04, kCardGapY = 42.04;
struct Seed {
  std::vector<djerd::StraightVisualNode> nodes;
  bool complete=false;double elapsedMs=0;std::string reason;
};
Seed propose(const std::vector<djerd::StraightVisualNode>& original,
    const std::vector<djerd::StraightVisualEdge>& edges,const std::vector<std::string>& ids,
    const std::string& modelPath,std::chrono::steady_clock::time_point deadline);
}
