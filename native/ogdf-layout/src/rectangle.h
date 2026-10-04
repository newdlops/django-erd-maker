#pragma once

namespace djerd {

struct Rect {
  double bottom = 0.0;
  double left = 0.0;
  double right = 0.0;
  double top = 0.0;
};

inline bool rectsOverlap(const Rect& left, const Rect& right) {
  return left.left < right.right
    && left.right > right.left
    && left.top < right.bottom
    && left.bottom > right.top;
}

}  // namespace djerd
