#pragma once

#include <algorithm>
#include <cmath>

#include <ogdf/basic/GraphAttributes.h>

#include "types.h"

namespace djerd {

inline bool isFiniteCoordinate(double value) {
  return std::isfinite(value);
}

inline double sanitizeNodeWidth(const NodeRecord& node, ogdf::GraphAttributes& attributes) {
  const double width = attributes.width(node.handle);
  return std::max(1.0, isFiniteCoordinate(width) ? width : node.width);
}

inline double sanitizeNodeHeight(const NodeRecord& node, ogdf::GraphAttributes& attributes) {
  const double height = attributes.height(node.handle);
  return std::max(1.0, isFiniteCoordinate(height) ? height : node.height);
}

inline double sanitizeNodeCenterX(const NodeRecord& node, ogdf::GraphAttributes& attributes) {
  const double width = sanitizeNodeWidth(node, attributes);
  const double fallback = node.x + width / 2.0;
  const double center = attributes.x(node.handle);
  return isFiniteCoordinate(center) ? center : fallback;
}

inline double sanitizeNodeCenterY(const NodeRecord& node, ogdf::GraphAttributes& attributes) {
  const double height = sanitizeNodeHeight(node, attributes);
  const double fallback = node.y + height / 2.0;
  const double center = attributes.y(node.handle);
  return isFiniteCoordinate(center) ? center : fallback;
}

inline Rect nodeRect(
  const NodeRecord& node,
  ogdf::GraphAttributes& attributes,
  double margin = 0.0) {
  const double width = sanitizeNodeWidth(node, attributes);
  const double height = sanitizeNodeHeight(node, attributes);
  const double centerX = sanitizeNodeCenterX(node, attributes);
  const double centerY = sanitizeNodeCenterY(node, attributes);
  return {
    centerY + height / 2.0 + margin,
    centerX - width / 2.0 - margin,
    centerX + width / 2.0 + margin,
    centerY - height / 2.0 - margin,
  };
}

inline Rect handleRect(
  ogdf::node handle,
  ogdf::GraphAttributes& attributes,
  double margin = 0.0) {
  const double width = std::max(1.0, attributes.width(handle));
  const double height = std::max(1.0, attributes.height(handle));
  const double centerX = attributes.x(handle);
  const double centerY = attributes.y(handle);
  return {
    centerY + height / 2.0 + margin,
    centerX - width / 2.0 - margin,
    centerX + width / 2.0 + margin,
    centerY - height / 2.0 - margin,
  };
}

inline double rectWidth(const Rect& rect) {
  return rect.right - rect.left;
}

inline double rectHeight(const Rect& rect) {
  return rect.bottom - rect.top;
}

inline double rectCenterX(const Rect& rect) {
  return (rect.left + rect.right) / 2.0;
}

inline double rectCenterY(const Rect& rect) {
  return (rect.top + rect.bottom) / 2.0;
}

inline std::string nodeThresholdReason(std::size_t threshold, const std::string& detail) {
  return "nodes>=" + std::to_string(threshold) + "; " + detail;
}

}  // namespace djerd
