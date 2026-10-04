#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace djerd {

// Keep the original candidate order without storing a two-point vector for
// every boundary-port pair on every relationship. Visitor references are
// valid only during that call; a selected candidate must be copied.
template <typename Point>
class StraightRouteCandidates {
 public:
  void push_back(std::vector<Point> route) {
    explicitRoutes_.push_back(std::move(route));
  }

  void setPortProduct(std::vector<Point> sources, std::vector<Point> targets) {
    sources_ = std::move(sources);
    targets_ = std::move(targets);
  }

  std::size_t size() const {
    return explicitRoutes_.size() + sources_.size() * targets_.size();
  }

  bool empty() const { return size() == 0; }

  std::size_t storedPointCount() const {
    std::size_t count = sources_.size() + targets_.size();
    for (const auto& route : explicitRoutes_) count += route.size();
    return count;
  }

  template <typename Visitor>
  void forEach(Visitor&& visit) const {
    for (const auto& route : explicitRoutes_) visit(route);
    if (sources_.empty() || targets_.empty()) return;
    std::vector<Point> route(2);
    for (const Point& source : sources_) {
      route[0] = source;
      for (const Point& target : targets_) {
        route[1] = target;
        visit(route);
      }
    }
  }

 private:
  std::vector<std::vector<Point>> explicitRoutes_;
  std::vector<Point> sources_, targets_;
};

}  // namespace djerd
