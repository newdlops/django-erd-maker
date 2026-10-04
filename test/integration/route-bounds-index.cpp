#include "routeBoundsIndex.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

int main() {
  constexpr std::size_t count = 1732;
  djerd::RouteBoundsIndex index(count);
  std::vector<djerd::Rect> rectangles(count);
  std::mt19937_64 random(773);
  std::uniform_real_distribution<double> coordinate(-25000, 25000);
  std::uniform_real_distribution<double> extent(0, 12000);
  auto rectangle = [&]() {
    const double x = coordinate(random), y = coordinate(random);
    return djerd::Rect{y + extent(random), x, x + extent(random), y};
  };
  for (std::size_t i = 0; i < count; ++i) {
    rectangles[i] = rectangle();
    index.update(i, rectangles[i]);
  }
  for (int trial = 0; trial < 10000; ++trial) {
    const std::size_t moved = random() % count;
    rectangles[moved] = rectangle();
    if (trial % 31 == 0) rectangles[moved] = {120, -160, -160, 0};
    if (trial % 37 == 0) rectangles[moved] = {0, -160, 160, 0};
    index.update(moved, rectangles[moved]);
    const auto query = trial % 11 == 0 ? rectangles[moved] : rectangle();
    std::vector<std::size_t> expected;
    for (std::size_t i = 0; i < count; ++i) {
      if (query.left < rectangles[i].right && query.right > rectangles[i].left
          && query.top < rectangles[i].bottom && query.bottom > rectangles[i].top) {
        expected.push_back(i);
      }
    }
    auto actual = index.query(query);
    std::sort(actual.begin(), actual.end());
    if (actual != expected) {
      std::cerr << "Moved route candidates differ from the original all-route scan at " << trial << '\n';
      return 1;
    }
  }
  std::cout << "10000 mutable route queries match the original full scan\n";
}
