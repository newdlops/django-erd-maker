#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace djerd {

// Obstacle labels and background face labels are mutually exclusive. Store
// faces below the edge-label range instead of allocating a second full grid.
class FaceRasterGrid {
public:
  FaceRasterGrid(std::size_t count, std::size_t edgeCount) {
    if (edgeCount > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
      throw std::length_error("Too many raster edge labels");
    }
    edgeCount_ = static_cast<std::int32_t>(edgeCount);
    wide_ = edgeCount > static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max());
    if (wide_) cells32_.resize(count, 0);
    else cells16_.resize(count, 0);
  }

  void setObstacle(std::size_t index, std::int32_t label) { set(index, label); }
  std::int32_t obstacle(std::size_t index) const {
    const auto value = get(index);
    return value < -edgeCount_ ? 0 : value;
  }
  bool unassignedBackground(std::size_t index) const { return get(index) == 0; }
  void setFace(std::size_t index, std::int32_t label) {
    if (label > std::numeric_limits<std::int32_t>::max() - edgeCount_) {
      throw std::length_error("Too many raster face labels");
    }
    set(index, -edgeCount_ - label);
  }
  std::int32_t face(std::size_t index) const {
    const auto value = get(index);
    return value < -edgeCount_ ? -value - edgeCount_ : 0;
  }
  std::size_t storageBytes() const {
    return cells16_.size() * sizeof(std::int16_t) + cells32_.size() * sizeof(std::int32_t);
  }

private:
  std::int32_t get(std::size_t index) const {
    return wide_ ? cells32_[index] : cells16_[index];
  }
  void set(std::size_t index, std::int32_t label) {
    if (!wide_ && (label < std::numeric_limits<std::int16_t>::min()
                  || label > std::numeric_limits<std::int16_t>::max())) {
      cells32_.assign(cells16_.begin(), cells16_.end());
      std::vector<std::int16_t>().swap(cells16_);
      wide_ = true;
    }
    if (wide_) cells32_[index] = label;
    else cells16_[index] = static_cast<std::int16_t>(label);
  }
  std::vector<std::int16_t> cells16_;
  std::vector<std::int32_t> cells32_;
  std::int32_t edgeCount_ = 0;
  bool wide_ = false;
};

}  // namespace djerd
