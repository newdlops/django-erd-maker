#include "rectangleCollisionIndex.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

using djerd::Rect;

bool linearOverlap(const Rect& query, const std::vector<Rect>& placed) {
  return std::any_of(placed.begin(), placed.end(), [&](const Rect& other) {
    return query.left < other.right && query.right > other.left
      && query.top < other.bottom && query.bottom > other.top;
  });
}

void check(const djerd::RectangleCollisionIndex& index,
           const std::vector<Rect>& placed, const Rect& query) {
  if (index.overlaps(query) != linearOverlap(query, placed)) {
    std::cerr << "Collision query differs from the original linear scan\n";
    std::exit(1);
  }
}

int main() {
  djerd::RectangleCollisionIndex index(160.0, 120.0);
  std::vector<Rect> placed;
  const auto insert = [&](const Rect& rect) {
    index.insert(rect);
    placed.push_back(rect);
  };

  check(index, placed, {120, 0, 160, 0});
  insert({120, 0, 160, 0});
  // Touching boundaries are not overlaps, including negative grid cells.
  for (const Rect& query : std::vector<Rect>{
         {120, 160, 320, 0}, {120, -160, 0, 0},
         {240, 0, 160, 120}, {0, 0, 160, -120},
         {120, 159.999, 320, 0}, {120, -160, 0.001, 0}}) {
    check(index, placed, query);
  }
  insert({-120, -320, -160, -240});
  // Large model cards and unusual coordinates must remain exact.
  insert({100000, -100000, 100000, 99999});
  insert({20, 1e20, 1e20 + 1e7, -20});
  insert({60, 20, 10, 30});
  const double infinity = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  insert({infinity, -5, 5, -infinity});
  insert({40, nan, 20, 30});
  for (const Rect& query : std::vector<Rect>{
         {100001, -100001, 100001, 99998},
         {30, 1e20, 1e20 + 1e7, -30},
         {50, -infinity, infinity, 40}, {infinity, -10, 10, -infinity},
         {40, nan, 20, 30}, {60, 20, 10, 30}}) {
    check(index, placed, query);
  }

  std::mt19937_64 random(731);
  std::uniform_real_distribution<double> coordinate(-25000, 25000);
  std::uniform_real_distribution<double> extent(1, 2000);
  for (int i = 0; i < 4000; ++i) {
    const double x = coordinate(random), y = coordinate(random);
    const Rect rect{y + extent(random), x, x + extent(random), y};
    check(index, placed, rect);
    insert(rect);
  }
  for (int i = 0; i < 10000; ++i) {
    const double x = coordinate(random), y = coordinate(random);
    check(index, placed, {y + extent(random), x, x + extent(random), y});
  }
  if (index.size() != placed.size()) return 1;
  std::cout << "Exact collision queries passed for boundaries, large cards, "
               "unusual coordinates and 14000 deterministic random queries\n";
}
