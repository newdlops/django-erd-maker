#include "straightRouteCandidates.h"

#include <cassert>
#include <iostream>
#include <random>

struct Point {
  double x, y;
  bool operator==(const Point& other) const {
    return x == other.x && y == other.y;
  }
};

int main() {
  std::mt19937 random(4201071);
  for (int trial = 0; trial < 1000; ++trial) {
    djerd::StraightRouteCandidates<Point> candidates;
    std::vector<std::vector<Point>> eager;
    for (unsigned i = 0, count = random() % 8; i < count; ++i) {
      std::vector<Point> route{{double(random()), double(random())},
                               {double(random()), double(random())}};
      eager.push_back(route);
      candidates.push_back(route);
    }
    std::vector<Point> sources, targets;
    for (unsigned i = 0, count = random() % 49; i < count; ++i)
      sources.push_back({double(i), double(i % 5)});
    for (unsigned i = 0, count = random() % 49; i < count; ++i)
      targets.push_back({double(i % 3), double(i)});
    for (const Point& source : sources)
      for (const Point& target : targets) eager.push_back({source, target});
    candidates.setPortProduct(sources, targets);
    assert(candidates.size() == eager.size());
    assert(candidates.empty() == eager.empty());
    assert(candidates.storedPointCount() <= 16 + sources.size() + targets.size());
    // Repeated passes must enumerate the same coordinates in the original
    // order: equal-cost candidates keep the first winner in the layout.
    for (int pass = 0; pass < 2; ++pass) {
      std::size_t index = 0;
      candidates.forEach([&](const std::vector<Point>& route) {
        assert(route == eager[index++]);
      });
      assert(index == eager.size());
    }
  }
  djerd::StraightRouteCandidates<Point> maximum;
  std::vector<Point> ports(97, Point{1, 2});
  maximum.setPortProduct(ports, ports);
  assert(maximum.size() == 9409);
  assert(maximum.storedPointCount() == 194);
  std::cout << "1000 ordered candidate sets; 9409 routes store 194 points\n";
}
