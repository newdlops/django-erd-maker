#pragma once

#include <cstddef>
#include <memory>

#include "rectangle.h"

namespace djerd {

// Insert-only collision queries for already placed model cards. The grid
// only narrows candidates; the original strict rectangle predicate decides
// every collision, so placement order and selected positions are unchanged.
class RectangleCollisionIndex {
 public:
  RectangleCollisionIndex(double cellWidth, double cellHeight,
                          std::size_t expectedSize = 0);
  ~RectangleCollisionIndex();
  void insert(const Rect& rect);
  bool overlaps(const Rect& rect) const;
  std::size_t size() const;

 private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

}  // namespace djerd
