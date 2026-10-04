#include "routeBoundsIndex.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace djerd {

struct RouteBoundsIndex::Storage {
  static constexpr auto none = std::numeric_limits<std::size_t>::max();
  struct Entry {
    Rect bounds;
    double maxRight = 0;
    std::size_t left = none, right = none;
    std::uint64_t priority = 0;
    bool indexed = false, present = false;
  };
  std::vector<Entry> entries;
  std::vector<std::size_t> unindexed;
  std::size_t root = none;

  static bool finite(const Rect& rect) {
    return std::isfinite(rect.left) && std::isfinite(rect.right)
      && std::isfinite(rect.top) && std::isfinite(rect.bottom)
      && rect.left <= rect.right && rect.top <= rect.bottom;
  }
  bool less(std::size_t a, std::size_t b) const {
    return entries[a].bounds.left < entries[b].bounds.left
      || (entries[a].bounds.left == entries[b].bounds.left && a < b);
  }
  void refresh(std::size_t index) {
    auto& entry = entries[index];
    entry.maxRight = entry.bounds.right;
    if (entry.left != none) entry.maxRight = std::max(entry.maxRight, entries[entry.left].maxRight);
    if (entry.right != none) entry.maxRight = std::max(entry.maxRight, entries[entry.right].maxRight);
  }
  std::size_t merge(std::size_t left, std::size_t right) {
    if (left == none) return right;
    if (right == none) return left;
    if (entries[left].priority < entries[right].priority) {
      entries[left].right = merge(entries[left].right, right);
      refresh(left);
      return left;
    }
    entries[right].left = merge(left, entries[right].left);
    refresh(right);
    return right;
  }
  std::size_t erase(std::size_t rootIndex, std::size_t index) {
    if (rootIndex == index) return merge(entries[rootIndex].left, entries[rootIndex].right);
    if (less(index, rootIndex)) entries[rootIndex].left = erase(entries[rootIndex].left, index);
    else entries[rootIndex].right = erase(entries[rootIndex].right, index);
    refresh(rootIndex);
    return rootIndex;
  }
  std::size_t insert(std::size_t rootIndex, std::size_t index) {
    if (rootIndex == none) return index;
    if (less(index, rootIndex)) {
      entries[rootIndex].left = insert(entries[rootIndex].left, index);
      const auto child = entries[rootIndex].left;
      if (entries[child].priority < entries[rootIndex].priority) {
        entries[rootIndex].left = entries[child].right;
        entries[child].right = rootIndex;
        refresh(rootIndex);
        refresh(child);
        return child;
      }
    } else {
      entries[rootIndex].right = insert(entries[rootIndex].right, index);
      const auto child = entries[rootIndex].right;
      if (entries[child].priority < entries[rootIndex].priority) {
        entries[rootIndex].right = entries[child].left;
        entries[child].left = rootIndex;
        refresh(rootIndex);
        refresh(child);
        return child;
      }
    }
    refresh(rootIndex);
    return rootIndex;
  }
  void collect(std::size_t index, const Rect& bounds, std::vector<std::size_t>& result) const {
    if (index == none || entries[index].maxRight <= bounds.left) return;
    const auto& entry = entries[index];
    collect(entry.left, bounds, result);
    if (entry.bounds.left >= bounds.right) return;
    if (rectsOverlap(entry.bounds, bounds)) result.push_back(index);
    collect(entry.right, bounds, result);
  }
};

RouteBoundsIndex::RouteBoundsIndex(std::size_t count) : storage_(std::make_unique<Storage>()) {
  storage_->entries.resize(count);
  for (std::size_t index = 0; index < count; ++index) {
    std::uint64_t hash = index + 0x9e3779b97f4a7c15ULL;
    hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ULL;
    hash = (hash ^ (hash >> 27)) * 0x94d049bb133111ebULL;
    storage_->entries[index].priority = hash ^ (hash >> 31);
  }
}

RouteBoundsIndex::~RouteBoundsIndex() = default;

void RouteBoundsIndex::update(std::size_t index, const Rect& bounds) {
  auto& entry = storage_->entries.at(index);
  if (entry.indexed) storage_->root = storage_->erase(storage_->root, index);
  else if (entry.present) {
    auto& unindexed = storage_->unindexed;
    unindexed.erase(std::remove(unindexed.begin(), unindexed.end(), index), unindexed.end());
  }
  entry.bounds = bounds;
  entry.maxRight = bounds.right;
  entry.left = entry.right = Storage::none;
  entry.present = true;
  entry.indexed = Storage::finite(bounds);
  if (entry.indexed) storage_->root = storage_->insert(storage_->root, index);
  else storage_->unindexed.push_back(index);
}

std::vector<std::size_t> RouteBoundsIndex::query(const Rect& bounds) const {
  std::vector<std::size_t> result;
  if (!Storage::finite(bounds)) {
    for (std::size_t i = 0; i < storage_->entries.size(); ++i) {
      if (storage_->entries[i].present) result.push_back(i);
    }
    return result;
  }
  result = storage_->unindexed;
  storage_->collect(storage_->root, bounds, result);
  return result;
}

}  // namespace djerd
