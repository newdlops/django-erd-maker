#include "rectangleCollisionIndex.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace djerd {

struct RectangleCollisionIndex::Storage {
  struct Cell {
    std::int64_t x, y;
    bool operator==(const Cell& other) const {
      return x == other.x && y == other.y;
    }
  };
  struct CellHash {
    std::size_t operator()(const Cell& cell) const {
      const auto x = static_cast<std::uint64_t>(cell.x);
      const auto y = static_cast<std::uint64_t>(cell.y);
      return static_cast<std::size_t>(
        x ^ (y + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2)));
    }
  };
  struct CellRange {
    std::int64_t minX, maxX, minY, maxY;
  };
  bool cellRange(const Rect& rect, CellRange& range) const;
  double cellWidth, cellHeight;
  std::vector<Rect> rectangles;
  std::vector<std::size_t> unindexed;
  std::unordered_map<Cell, std::vector<std::size_t>, CellHash> cells;
};

RectangleCollisionIndex::RectangleCollisionIndex(
    double cellWidth, double cellHeight, std::size_t expectedSize)
    : storage_(std::make_unique<Storage>()) {
  storage_->cellWidth = std::isfinite(cellWidth) && cellWidth > 0 ? cellWidth : 1;
  storage_->cellHeight = std::isfinite(cellHeight) && cellHeight > 0 ? cellHeight : 1;
  storage_->rectangles.reserve(expectedSize);
  storage_->cells.reserve(expectedSize);
}

RectangleCollisionIndex::~RectangleCollisionIndex() = default;

std::size_t RectangleCollisionIndex::size() const {
  return storage_->rectangles.size();
}

bool RectangleCollisionIndex::Storage::cellRange(const Rect& rect, CellRange& range) const {
  if (rect.right < rect.left || rect.bottom < rect.top) return false;
  const double bounds[] = {
    std::floor(rect.left / cellWidth), std::floor(rect.right / cellWidth),
    std::floor(rect.top / cellHeight), std::floor(rect.bottom / cellHeight),
  };
  // Stay within exactly represented integers, avoiding floating-to-integer
  // overflow for malformed or exceptionally distant source coordinates.
  for (double bound : bounds) {
    if (!std::isfinite(bound) || std::abs(bound) > 4503599627370496.0) return false;
  }
  range = {static_cast<std::int64_t>(bounds[0]), static_cast<std::int64_t>(bounds[1]),
           static_cast<std::int64_t>(bounds[2]), static_cast<std::int64_t>(bounds[3])};
  const auto width = range.maxX - range.minX + 1;
  const auto height = range.maxY - range.minY + 1;
  // Huge cards use the original linear predicate instead of allocating or
  // traversing an unbounded number of grid cells.
  return width <= 64 && height <= 64 / width;
}

void RectangleCollisionIndex::insert(const Rect& rect) {
  const auto index = storage_->rectangles.size();
  storage_->rectangles.push_back(rect);
  Storage::CellRange range;
  if (!storage_->cellRange(rect, range)) {
    storage_->unindexed.push_back(index);
    return;
  }
  for (auto y = range.minY; y <= range.maxY; ++y) {
    for (auto x = range.minX; x <= range.maxX; ++x) {
      storage_->cells[{x, y}].push_back(index);
    }
  }
}

bool RectangleCollisionIndex::overlaps(const Rect& rect) const {
  Storage::CellRange range;
  if (!storage_->cellRange(rect, range)) {
    return std::any_of(storage_->rectangles.begin(), storage_->rectangles.end(),
                       [&](const Rect& other) { return rectsOverlap(rect, other); });
  }
  for (auto index : storage_->unindexed) {
    if (rectsOverlap(rect, storage_->rectangles[index])) return true;
  }
  for (auto y = range.minY; y <= range.maxY; ++y) {
    for (auto x = range.minX; x <= range.maxX; ++x) {
      const auto cell = storage_->cells.find({x, y});
      if (cell == storage_->cells.end()) continue;
      for (auto index : cell->second) {
        if (rectsOverlap(rect, storage_->rectangles[index])) return true;
      }
    }
  }
  return false;
}

}  // namespace djerd
