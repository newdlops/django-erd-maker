#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "rectangle.h"

namespace djerd {

// A mutable interval tree for this calculation's routed geometry. Bounding
// boxes only select candidates; the original segment predicate still decides
// crossings. There is no stored analysis or layout reused between requests.
class RouteBoundsIndex {
 public:
  explicit RouteBoundsIndex(std::size_t count);
  ~RouteBoundsIndex();
  void update(std::size_t index, const Rect& bounds);
  std::vector<std::size_t> query(const Rect& bounds) const;

 private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

}  // namespace djerd
