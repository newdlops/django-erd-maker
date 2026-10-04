#include "layoutPipeline.h"

namespace djerd {

void compactWhitespaceAxis(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  bool horizontal,
  double gap) {
  const std::size_t n = nodes.size();
  if (n == 0) {
    return;
  }
  std::vector<std::size_t> order(n);
  for (std::size_t i = 0; i < n; ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(),
    [&](std::size_t a, std::size_t b) {
      const double aPos = horizontal ? attributes.x(nodes[a].handle) : attributes.y(nodes[a].handle);
      const double bPos = horizontal ? attributes.x(nodes[b].handle) : attributes.y(nodes[b].handle);
      return aPos < bPos;
    });

  std::vector<double> newPos(n, 0.0);
  std::vector<bool> placed(n, false);

  for (std::size_t k = 0; k < order.size(); ++k) {
    const std::size_t idx = order[k];
    const NodeRecord& node = nodes[idx];
    const double w = std::max(1.0, node.width);
    const double h = std::max(1.0, node.height);
    const double halfMain = horizontal ? w / 2.0 : h / 2.0;
    const double crossPos = horizontal ? attributes.y(node.handle) : attributes.x(node.handle);
    const double crossHalf = horizontal ? h / 2.0 : w / 2.0;
    const double thisLow = crossPos - crossHalf;
    const double thisHigh = crossPos + crossHalf;

    double earliestEdge = halfMain;
    for (std::size_t j = 0; j < k; ++j) {
      const std::size_t other = order[j];
      const NodeRecord& otherNode = nodes[other];
      const double otherW = std::max(1.0, otherNode.width);
      const double otherH = std::max(1.0, otherNode.height);
      const double otherCrossPos = horizontal ? attributes.y(otherNode.handle) : attributes.x(otherNode.handle);
      const double otherCrossHalf = horizontal ? otherH / 2.0 : otherW / 2.0;
      const double otherLow = otherCrossPos - otherCrossHalf;
      const double otherHigh = otherCrossPos + otherCrossHalf;
      if (otherHigh < thisLow || otherLow > thisHigh) {
        continue;
      }
      const double otherMainHalf = horizontal ? otherW / 2.0 : otherH / 2.0;
      const double otherEdge = newPos[other] + otherMainHalf + gap + halfMain;
      earliestEdge = std::max(earliestEdge, otherEdge);
    }
    newPos[idx] = earliestEdge;
    placed[idx] = true;
  }

  for (std::size_t i = 0; i < n; ++i) {
    if (horizontal) {
      attributes.x(nodes[i].handle) = newPos[i];
    } else {
      attributes.y(nodes[i].handle) = newPos[i];
    }
  }
}

void compactGlobalLayout(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  double gap) {
  compactWhitespaceAxis(nodes, attributes, true, gap);
  compactWhitespaceAxis(nodes, attributes, false, gap);
  compactWhitespaceAxis(nodes, attributes, true, gap);
}

void applySiftingSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const auto band = static_cast<long long>(std::floor(x / 520.0));
    const double direction = band % 2 == 0 ? -1.0 : 1.0;
    const double wave = std::sin(x * 0.004) * 18.0;
    return std::make_pair(x + direction * 28.0, y + direction * 84.0 + wave);
  });
}

void clearEdgeBends(
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  for (const EdgeRecord& edge : edges) {
    attributes.bends(edge.handle) = ogdf::DPolyline();
  }
}

void applyGlobalSiftingSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const auto band = static_cast<long long>(std::floor(y / 360.0));
    const double drift = std::cos(y * 0.003) * 42.0;
    return std::make_pair(x + band * 18.0 + drift, y * 1.015);
  });
}

void applyGreedyInsertSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const auto layer = static_cast<long long>(std::floor(x / 420.0));
    const double compact = layer % 3 == 0 ? -36.0 : 18.0;
    return std::make_pair(x * 0.965 + compact, y + std::sin(y * 0.006) * 24.0);
  });
}

void applyGreedySwitchSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const auto lane = static_cast<long long>(std::floor(y / 220.0));
    const double direction = lane % 2 == 0 ? 1.0 : -1.0;
    return std::make_pair(x + direction * 52.0, y + direction * 16.0);
  });
}

void applyGridSiftingSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  constexpr double gridX = 180.0;
  constexpr double gridY = 120.0;
  transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
    const double snappedX = std::round((x + 45.0) / gridX) * gridX;
    const double snappedY = std::round(y / gridY) * gridY;
    const auto row = static_cast<long long>(std::round(snappedY / gridY));
    return std::make_pair(snappedX + (row % 2 == 0 ? 0.0 : 42.0), snappedY);
  });
}

void applySplitSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const auto lane = static_cast<long long>(std::floor(x / 520.0));
    const double side = lane % 2 == 0 ? -1.0 : 1.0;
    return std::make_pair(x + side * 88.0, y * 0.985 + side * 34.0);
  });
}

void applyPlanarSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    return std::make_pair(x + y * 0.055, y + x * 0.018);
  });
}

void applyOrthogonalSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  constexpr double gridX = 220.0;
  constexpr double gridY = 150.0;
  transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
    return std::make_pair(std::round(x / gridX) * gridX, std::round(y / gridY) * gridY);
  });

  for (const EdgeRecord& edge : edges) {
    const double sourceX = attributes.x(edge.sourceHandle);
    const double sourceY = attributes.y(edge.sourceHandle);
    const double targetX = attributes.x(edge.targetHandle);
    const double targetY = attributes.y(edge.targetHandle);
    ogdf::DPolyline bends;
    if (std::abs(sourceX - targetX) > 1.0 && std::abs(sourceY - targetY) > 1.0) {
      bends.pushBack(ogdf::DPoint(sourceX, targetY));
    }
    attributes.bends(edge.handle) = bends;
  }
}

void applyPlanarGridSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  constexpr double gridX = 240.0;
  constexpr double gridY = 160.0;
  transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
    const double snappedX = std::round(x / gridX) * gridX;
    const double snappedY = std::round(y / gridY) * gridY;
    const auto column = static_cast<long long>(std::round(snappedX / gridX));
    const double stagger = column % 2 == 0 ? 0.0 : gridY * 0.35;
    return std::make_pair(snappedX, snappedY + stagger);
  });
}

void applyStraightLineSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  clearEdgeBends(edges, attributes);
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    return std::make_pair(x * 1.03 + y * 0.025, y * 0.97);
  });
}

void applySchnyderSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  clearEdgeBends(edges, attributes);
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const double skewX = x + y * 0.33;
    const double skewY = y * 0.82;
    return std::make_pair(skewX, skewY);
  });
}

void applyUpwardSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  bool layerBased) {
  clearEdgeBends(edges, attributes);
  transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
    const auto rank = static_cast<long long>(std::floor(x / 480.0));
    if (layerBased) {
      return std::make_pair(x + (rank % 2 == 0 ? 0.0 : 60.0), y + rank * 22.0);
    }

    const double diagonalLift = static_cast<double>(rank) * 34.0;
    return std::make_pair(x * 1.018 + y * 0.018, y * 0.965 + diagonalLift);
  });
}

void applyVisibilitySurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  constexpr double gridX = 240.0;
  constexpr double gridY = 110.0;
  transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
    return std::make_pair(std::round(x / gridX) * gridX, std::round(y / gridY) * gridY);
  });
  applyOrthogonalSurrogateGeometry(nodes, edges, attributes);
}

void applyPivotMdsGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  const double angle = 0.045;
  const double cosine = std::cos(angle);
  const double sine = std::sin(angle);
  transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
    return std::make_pair(
      x * cosine - y * sine,
      x * sine + y * cosine * 0.94);
  });
}

void applyUmlPlanarSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const double lane = std::floor(x / 620.0);
    return std::make_pair(x + y * 0.035 + lane * 12.0, y * 0.972 + x * 0.024);
  });
}

std::string clusterKeyForModelId(const std::string& modelId) {
  const std::size_t delimiter = modelId.find('.');
  if (delimiter == std::string::npos || delimiter == 0) {
    return "(default)";
  }

  return modelId.substr(0, delimiter);
}

void applyClusterSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  bool orthogonal) {
  std::unordered_map<std::string, std::size_t> groupIndexByKey;
  std::vector<ClusterGroupLayout> groups;

  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const std::string key = clusterKeyForModelId(nodes[index].modelId);
    auto inserted = groupIndexByKey.emplace(key, groups.size());
    if (inserted.second) {
      groups.emplace_back();
    }
    groups[inserted.first->second].nodeIndices.push_back(index);
  }

  if (groups.empty()) {
    return;
  }

  const std::size_t groupColumns = std::max<std::size_t>(
    1,
    static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(groups.size())))));
  const std::size_t groupRows = (groups.size() + groupColumns - 1) / groupColumns;
  std::vector<double> columnWidths(groupColumns, 0.0);
  std::vector<double> rowHeights(groupRows, 0.0);

  for (std::size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
    ClusterGroupLayout& group = groups[groupIndex];
    double maxWidth = 120.0;
    double maxHeight = 80.0;

    for (std::size_t nodeIndex : group.nodeIndices) {
      maxWidth = std::max(maxWidth, sanitizeNodeWidth(nodes[nodeIndex], attributes));
      maxHeight = std::max(maxHeight, sanitizeNodeHeight(nodes[nodeIndex], attributes));
    }

    const std::size_t columns = std::max<std::size_t>(
      1,
      static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(group.nodeIndices.size())))));
    const std::size_t rows = (group.nodeIndices.size() + columns - 1) / columns;
    const double cellWidth = maxWidth + (orthogonal ? 130.0 : 160.0);
    const double cellHeight = maxHeight + (orthogonal ? 100.0 : 130.0);
    group.width = static_cast<double>(columns) * cellWidth + 220.0;
    group.height = static_cast<double>(rows) * cellHeight + 220.0;
    columnWidths[groupIndex % groupColumns] =
      std::max(columnWidths[groupIndex % groupColumns], group.width);
    rowHeights[groupIndex / groupColumns] =
      std::max(rowHeights[groupIndex / groupColumns], group.height);
  }

  std::vector<double> columnOrigins(groupColumns, 0.0);
  std::vector<double> rowOrigins(groupRows, 0.0);
  for (std::size_t index = 1; index < groupColumns; ++index) {
    columnOrigins[index] = columnOrigins[index - 1] + columnWidths[index - 1] + 420.0;
  }
  for (std::size_t index = 1; index < groupRows; ++index) {
    rowOrigins[index] = rowOrigins[index - 1] + rowHeights[index - 1] + 360.0;
  }

  for (std::size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
    const ClusterGroupLayout& group = groups[groupIndex];
    double maxWidth = 120.0;
    double maxHeight = 80.0;

    for (std::size_t nodeIndex : group.nodeIndices) {
      maxWidth = std::max(maxWidth, sanitizeNodeWidth(nodes[nodeIndex], attributes));
      maxHeight = std::max(maxHeight, sanitizeNodeHeight(nodes[nodeIndex], attributes));
    }

    const std::size_t columns = std::max<std::size_t>(
      1,
      static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(group.nodeIndices.size())))));
    const double cellWidth = maxWidth + (orthogonal ? 130.0 : 160.0);
    const double cellHeight = maxHeight + (orthogonal ? 100.0 : 130.0);
    const double originX = columnOrigins[groupIndex % groupColumns] + 110.0;
    const double originY = rowOrigins[groupIndex / groupColumns] + 110.0;

    for (std::size_t localIndex = 0; localIndex < group.nodeIndices.size(); ++localIndex) {
      const NodeRecord& node = nodes[group.nodeIndices[localIndex]];
      const std::size_t column = localIndex % columns;
      const std::size_t row = localIndex / columns;
      const double stagger = orthogonal || row % 2 == 0 ? 0.0 : cellWidth * 0.18;
      attributes.x(node.handle) = originX + static_cast<double>(column) * cellWidth + stagger;
      attributes.y(node.handle) = originY + static_cast<double>(row) * cellHeight;
    }
  }

  if (orthogonal) {
    for (const EdgeRecord& edge : edges) {
      const double sourceX = attributes.x(edge.sourceHandle);
      const double sourceY = attributes.y(edge.sourceHandle);
      const double targetX = attributes.x(edge.targetHandle);
      const double targetY = attributes.y(edge.targetHandle);
      ogdf::DPolyline bends;
      if (std::abs(sourceX - targetX) > 1.0 && std::abs(sourceY - targetY) > 1.0) {
        bends.pushBack(ogdf::DPoint(sourceX, targetY));
      }
      attributes.bends(edge.handle) = bends;
    }
  } else {
    clearEdgeBends(edges, attributes);
  }
}

std::vector<std::vector<std::size_t>> collectConnectedComponents(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges) {
  std::unordered_map<ogdf::node, std::size_t> indicesByNode;
  indicesByNode.reserve(nodes.size());
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    indicesByNode.emplace(nodes[index].handle, index);
  }

  std::vector<std::vector<std::size_t>> adjacency(nodes.size());
  for (const EdgeRecord& edge : edges) {
    const auto source = indicesByNode.find(edge.sourceHandle);
    const auto target = indicesByNode.find(edge.targetHandle);
    if (
      source == indicesByNode.end()
      || target == indicesByNode.end()
      || source->second == target->second) {
      continue;
    }
    adjacency[source->second].push_back(target->second);
    adjacency[target->second].push_back(source->second);
  }

  std::vector<std::vector<std::size_t>> components;
  std::vector<bool> seen(nodes.size(), false);
  for (std::size_t start = 0; start < nodes.size(); ++start) {
    if (seen[start]) {
      continue;
    }

    std::vector<std::size_t> component;
    std::queue<std::size_t> pending;
    pending.push(start);
    seen[start] = true;

    while (!pending.empty()) {
      const std::size_t current = pending.front();
      pending.pop();
      component.push_back(current);

      for (std::size_t next : adjacency[current]) {
        if (seen[next]) {
          continue;
        }
        seen[next] = true;
        pending.push(next);
      }
    }

    components.push_back(component);
  }

  return components;
}

std::vector<std::vector<std::size_t>> buildUndirectedAdjacency(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges) {
  std::unordered_map<ogdf::node, std::size_t> indicesByNode;
  indicesByNode.reserve(nodes.size());
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    indicesByNode.emplace(nodes[index].handle, index);
  }

  std::vector<std::vector<std::size_t>> adjacency(nodes.size());
  for (const EdgeRecord& edge : edges) {
    const auto source = indicesByNode.find(edge.sourceHandle);
    const auto target = indicesByNode.find(edge.targetHandle);
    if (
      source == indicesByNode.end()
      || target == indicesByNode.end()
      || source->second == target->second) {
      continue;
    }

    auto& sourceNeighbors = adjacency[source->second];
    if (
      std::find(sourceNeighbors.begin(), sourceNeighbors.end(), target->second)
      == sourceNeighbors.end()) {
      sourceNeighbors.push_back(target->second);
    }

    auto& targetNeighbors = adjacency[target->second];
    if (
      std::find(targetNeighbors.begin(), targetNeighbors.end(), source->second)
      == targetNeighbors.end()) {
      targetNeighbors.push_back(source->second);
    }
  }

  return adjacency;
}

Rect componentRect(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::size_t>& component,
  ogdf::GraphAttributes& attributes) {
  Rect rect;
  bool initialized = false;

  for (std::size_t nodeIndex : component) {
    const Rect node = nodeRect(nodes[nodeIndex], attributes);
    if (!initialized) {
      rect = node;
      initialized = true;
      continue;
    }

    rect.left = std::min(rect.left, node.left);
    rect.right = std::max(rect.right, node.right);
    rect.top = std::min(rect.top, node.top);
    rect.bottom = std::max(rect.bottom, node.bottom);
  }

  return rect;
}

void translateComponent(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::size_t>& component,
  ogdf::GraphAttributes& attributes,
  double dx,
  double dy) {
  for (std::size_t nodeIndex : component) {
    const NodeRecord& node = nodes[nodeIndex];
    attributes.x(node.handle) = sanitizeNodeCenterX(node, attributes) + dx;
    attributes.y(node.handle) = sanitizeNodeCenterY(node, attributes) + dy;
  }
}

void pullLowDegreeNodesInward(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  int maxIterations,
  std::size_t degreeThreshold,
  double damping) {
  if (nodes.empty()) {
    return;
  }
  std::unordered_map<std::string, std::size_t> indexById;
  indexById.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    indexById[nodes[i].modelId] = i;
  }
  std::vector<std::vector<std::size_t>> neighbors(nodes.size());
  for (const EdgeRecord& edge : edges) {
    const auto srcIt = indexById.find(edge.sourceModelId);
    const auto tgtIt = indexById.find(edge.targetModelId);
    if (srcIt == indexById.end() || tgtIt == indexById.end() || srcIt->second == tgtIt->second) {
      continue;
    }
    neighbors[srcIt->second].push_back(tgtIt->second);
    neighbors[tgtIt->second].push_back(srcIt->second);
  }
  for (auto& list : neighbors) {
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
  }

  // Compute mean inter-node spacing across the graph as a target distance budget.
  double sumX = 0.0;
  double sumY = 0.0;
  double sumW = 0.0;
  double sumH = 0.0;
  for (const NodeRecord& node : nodes) {
    sumW += std::max(1.0, node.width);
    sumH += std::max(1.0, node.height);
    sumX += attributes.x(node.handle);
    sumY += attributes.y(node.handle);
  }
  const double avgW = sumW / static_cast<double>(nodes.size());
  const double avgH = sumH / static_cast<double>(nodes.size());
  const double targetMinDistance = std::max(avgW, avgH) * 1.2;

  auto findOverlap = [&](std::size_t i, double newX, double newY) -> bool {
    const double width = std::max(1.0, nodes[i].width);
    const double height = std::max(1.0, nodes[i].height);
    for (std::size_t k = 0; k < nodes.size(); ++k) {
      if (k == i) continue;
      const double otherX = attributes.x(nodes[k].handle);
      const double otherY = attributes.y(nodes[k].handle);
      const double otherW = std::max(1.0, nodes[k].width);
      const double otherH = std::max(1.0, nodes[k].height);
      const double dx = std::abs(newX - otherX);
      const double dy = std::abs(newY - otherY);
      if (dx < (width + otherW) / 2.0 + 4.0 && dy < (height + otherH) / 2.0 + 4.0) {
        return true;
      }
    }
    return false;
  };

  for (int iter = 0; iter < maxIterations; ++iter) {
    bool moved = false;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      const std::vector<std::size_t>& adj = neighbors[i];
      if (adj.empty() || adj.size() > degreeThreshold) {
        continue;
      }
      double neighSumX = 0.0;
      double neighSumY = 0.0;
      for (std::size_t j : adj) {
        neighSumX += attributes.x(nodes[j].handle);
        neighSumY += attributes.y(nodes[j].handle);
      }
      const double targetX = neighSumX / static_cast<double>(adj.size());
      const double targetY = neighSumY / static_cast<double>(adj.size());
      const double currentX = attributes.x(nodes[i].handle);
      const double currentY = attributes.y(nodes[i].handle);
      const double distFromTarget = std::hypot(currentX - targetX, currentY - targetY);
      if (distFromTarget <= targetMinDistance * 4.0) {
        continue;
      }

      const double newX = currentX * (1.0 - damping) + targetX * damping;
      const double newY = currentY * (1.0 - damping) + targetY * damping;
      if (findOverlap(i, newX, newY)) {
        continue;
      }
      attributes.x(nodes[i].handle) = newX;
      attributes.y(nodes[i].handle) = newY;
      moved = true;
    }
    if (!moved) {
      break;
    }
  }
}

void compactClusterOutliers(
  const std::vector<NodeRecord>& nodes,
  const std::unordered_map<std::string, std::string>& clusterByModelId,
  ogdf::GraphAttributes& attributes,
  double outlierMedianMultiplier) {
  if (clusterByModelId.empty() || nodes.empty()) {
    return;
  }
  std::unordered_map<std::string, std::vector<std::size_t>> membersByCluster;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    auto it = clusterByModelId.find(nodes[i].modelId);
    if (it == clusterByModelId.end()) {
      continue;
    }
    membersByCluster[it->second].push_back(i);
  }
  for (int iteration = 0; iteration < 2; ++iteration) {
    bool moved = false;
    for (const auto& [clusterKey, members] : membersByCluster) {
      if (members.size() < 4) {
        continue;
      }
      double sumX = 0.0;
      double sumY = 0.0;
      for (std::size_t idx : members) {
        sumX += attributes.x(nodes[idx].handle);
        sumY += attributes.y(nodes[idx].handle);
      }
      const double cx = sumX / static_cast<double>(members.size());
      const double cy = sumY / static_cast<double>(members.size());
      std::vector<double> distances;
      distances.reserve(members.size());
      for (std::size_t idx : members) {
        distances.push_back(std::hypot(
          attributes.x(nodes[idx].handle) - cx,
          attributes.y(nodes[idx].handle) - cy));
      }
      std::vector<double> sortedDist = distances;
      std::sort(sortedDist.begin(), sortedDist.end());
      const double median = sortedDist[sortedDist.size() / 2];
      const double percentile75 = sortedDist[(sortedDist.size() * 3) / 4];
      const double outlierCutoff = std::max(
        std::min(median * outlierMedianMultiplier, percentile75 * 1.2),
        1.0);
      for (std::size_t k = 0; k < members.size(); ++k) {
        if (distances[k] <= outlierCutoff) {
          continue;
        }
        const std::size_t idx = members[k];
        const double currentX = attributes.x(nodes[idx].handle);
        const double currentY = attributes.y(nodes[idx].handle);
        const double scale = outlierCutoff / distances[k];
        attributes.x(nodes[idx].handle) = cx + (currentX - cx) * scale;
        attributes.y(nodes[idx].handle) = cy + (currentY - cy) * scale;
        moved = true;
      }
    }
    if (!moved) {
      break;
    }
  }
}

void packDisconnectedComponents(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  std::vector<std::vector<std::size_t>> components = collectConnectedComponents(nodes, edges);
  if (components.size() <= 1) {
    return;
  }

  std::sort(
    components.begin(),
    components.end(),
    [&](const auto& left, const auto& right) {
      const Rect leftRect = componentRect(nodes, left, attributes);
      const Rect rightRect = componentRect(nodes, right, attributes);
      const double leftArea = rectWidth(leftRect) * rectHeight(leftRect);
      const double rightArea = rectWidth(rightRect) * rectHeight(rightRect);
      if (std::abs(leftArea - rightArea) > 0.01) {
        return leftArea > rightArea;
      }
      return left.size() > right.size();
    });

  constexpr double componentGapX = 220.0;
  constexpr double componentGapY = 180.0;
  double totalPackedArea = 0.0;
  double widest = 0.0;
  for (const auto& component : components) {
    const Rect rect = componentRect(nodes, component, attributes);
    totalPackedArea += (rectWidth(rect) + componentGapX) * (rectHeight(rect) + componentGapY);
    widest = std::max(widest, rectWidth(rect));
  }

  const double targetRowWidth = std::max(widest, std::sqrt(totalPackedArea) * 1.28);
  double cursorX = 0.0;
  double cursorY = 0.0;
  double rowHeight = 0.0;

  for (const auto& component : components) {
    const Rect rect = componentRect(nodes, component, attributes);
    const double width = rectWidth(rect);
    const double height = rectHeight(rect);

    if (cursorX > 0.0 && cursorX + width > targetRowWidth) {
      cursorX = 0.0;
      cursorY += rowHeight + componentGapY;
      rowHeight = 0.0;
    }

    translateComponent(nodes, component, attributes, cursorX - rect.left, cursorY - rect.top);
    cursorX += width + componentGapX;
    rowHeight = std::max(rowHeight, height);
  }

  clearEdgeBends(edges, attributes);
}

double centerDistance(
  const NodeRecord& left,
  const NodeRecord& right,
  ogdf::GraphAttributes& attributes) {
  const double dx = sanitizeNodeCenterX(left, attributes) - sanitizeNodeCenterX(right, attributes);
  const double dy = sanitizeNodeCenterY(left, attributes) - sanitizeNodeCenterY(right, attributes);
  return std::hypot(dx, dy);
}

double medianValue(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }

  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if (values.size() % 2 == 1) {
    return values[middle];
  }

  return (values[middle - 1] + values[middle]) / 2.0;
}

void compactDistantConnectedNodes(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 2 || edges.empty()) {
    return;
  }

  std::unordered_map<ogdf::node, std::size_t> indicesByNode;
  indicesByNode.reserve(nodes.size());
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    indicesByNode.emplace(nodes[index].handle, index);
  }

  std::vector<std::vector<std::size_t>> neighbors(nodes.size());
  std::vector<double> edgeLengths;
  edgeLengths.reserve(edges.size());

  for (const EdgeRecord& edge : edges) {
    const auto source = indicesByNode.find(edge.sourceHandle);
    const auto target = indicesByNode.find(edge.targetHandle);
    if (
      source == indicesByNode.end()
      || target == indicesByNode.end()
      || source->second == target->second) {
      continue;
    }

    neighbors[source->second].push_back(target->second);
    neighbors[target->second].push_back(source->second);
    const double length = centerDistance(nodes[source->second], nodes[target->second], attributes);
    if (length > 1.0 && isFiniteCoordinate(length)) {
      edgeLengths.push_back(length);
    }
  }

  const double medianEdgeLength = medianValue(std::move(edgeLengths));
  if (medianEdgeLength <= 1.0) {
    return;
  }

  const double threshold = std::max(
    kDistantEdgeMinThreshold,
    medianEdgeLength * kDistantEdgeLengthFactor);
  const double targetDistance = std::max(
    kDistantEdgeMinTarget,
    std::min(kDistantEdgeMaxTarget, medianEdgeLength * kDistantEdgeTargetFactor));

  for (std::size_t index = 0; index < nodes.size(); ++index) {
    if (neighbors[index].empty()) {
      continue;
    }

    double neighborX = 0.0;
    double neighborY = 0.0;
    for (std::size_t neighbor : neighbors[index]) {
      neighborX += sanitizeNodeCenterX(nodes[neighbor], attributes);
      neighborY += sanitizeNodeCenterY(nodes[neighbor], attributes);
    }
    neighborX /= static_cast<double>(neighbors[index].size());
    neighborY /= static_cast<double>(neighbors[index].size());

    const double centerX = sanitizeNodeCenterX(nodes[index], attributes);
    const double centerY = sanitizeNodeCenterY(nodes[index], attributes);
    const double dx = centerX - neighborX;
    const double dy = centerY - neighborY;
    const double distance = std::hypot(dx, dy);
    if (distance <= threshold || distance <= 1.0) {
      continue;
    }

    const double directionX = dx / distance;
    const double directionY = dy / distance;
    const double tangentOffset = (static_cast<double>(index % 7) - 3.0) * 18.0;
    attributes.x(nodes[index].handle) =
      neighborX + directionX * targetDistance - directionY * tangentOffset;
    attributes.y(nodes[index].handle) =
      neighborY + directionY * targetDistance + directionX * tangentOffset;
  }
}

void resolveNodeOverlaps(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 1) {
    return;
  }

  std::vector<std::size_t> order;
  order.reserve(nodes.size());
  double maxWidth = 1.0;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    order.push_back(index);
    maxWidth = std::max(maxWidth, sanitizeNodeWidth(nodes[index], attributes));
  }

  for (int iteration = 0; iteration < kOverlapRelaxationIterations; ++iteration) {
    std::sort(
      order.begin(),
      order.end(),
      [&](std::size_t left, std::size_t right) {
        return sanitizeNodeCenterX(nodes[left], attributes)
          < sanitizeNodeCenterX(nodes[right], attributes);
      });

    double maxShift = 0.0;
    bool moved = false;

    for (std::size_t leftOrder = 0; leftOrder < order.size(); ++leftOrder) {
      const std::size_t leftIndex = order[leftOrder];
      const NodeRecord& left = nodes[leftIndex];
      const double leftX = sanitizeNodeCenterX(left, attributes);
      const double leftY = sanitizeNodeCenterY(left, attributes);
      const double leftWidth = sanitizeNodeWidth(left, attributes);
      const double leftHeight = sanitizeNodeHeight(left, attributes);

      for (std::size_t rightOrder = leftOrder + 1; rightOrder < order.size(); ++rightOrder) {
        const std::size_t rightIndex = order[rightOrder];
        const NodeRecord& right = nodes[rightIndex];
        const double rightX = sanitizeNodeCenterX(right, attributes);
        const double dx = rightX - leftX;
        if (dx > maxWidth + kPostLayoutNodeGapX) {
          break;
        }

        const double rightY = sanitizeNodeCenterY(right, attributes);
        const double rightWidth = sanitizeNodeWidth(right, attributes);
        const double rightHeight = sanitizeNodeHeight(right, attributes);
        const double overlapX =
          (leftWidth + rightWidth) / 2.0 + kPostLayoutNodeGapX - std::abs(dx);
        if (overlapX <= 0.0) {
          continue;
        }

        const double dy = rightY - leftY;
        const double overlapY =
          (leftHeight + rightHeight) / 2.0 + kPostLayoutNodeGapY - std::abs(dy);
        if (overlapY <= 0.0) {
          continue;
        }

        if (overlapX <= overlapY) {
          const double direction = std::abs(dx) < 0.01
            ? (leftIndex % 2 == 0 ? 1.0 : -1.0)
            : (dx >= 0.0 ? 1.0 : -1.0);
          const double shift = overlapX / 2.0 + 2.0;
          attributes.x(left.handle) -= direction * shift;
          attributes.x(right.handle) += direction * shift;
          maxShift = std::max(maxShift, shift);
        } else {
          const double direction = std::abs(dy) < 0.01
            ? (leftIndex % 2 == 0 ? 1.0 : -1.0)
            : (dy >= 0.0 ? 1.0 : -1.0);
          const double shift = overlapY / 2.0 + 2.0;
          attributes.y(left.handle) -= direction * shift;
          attributes.y(right.handle) += direction * shift;
          maxShift = std::max(maxShift, shift);
        }
        moved = true;
      }
    }

    if (!moved || maxShift < 0.1) {
      break;
    }
  }
}

Rect expandedNodeRectAt(
  const NodeRecord& node,
  ogdf::GraphAttributes& attributes,
  double centerX,
  double centerY) {
  const double width = sanitizeNodeWidth(node, attributes);
  const double height = sanitizeNodeHeight(node, attributes);
  return {
    centerY + height / 2.0 + kPostLayoutNodeGapY / 2.0,
    centerX - width / 2.0 - kPostLayoutNodeGapX / 2.0,
    centerX + width / 2.0 + kPostLayoutNodeGapX / 2.0,
    centerY - height / 2.0 - kPostLayoutNodeGapY / 2.0,
  };
}

bool hasNodeSpacingConflicts(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  for (std::size_t leftIndex = 0; leftIndex < nodes.size(); ++leftIndex) {
    const Rect left = expandedNodeRectAt(
      nodes[leftIndex],
      attributes,
      sanitizeNodeCenterX(nodes[leftIndex], attributes),
      sanitizeNodeCenterY(nodes[leftIndex], attributes));
    for (std::size_t rightIndex = leftIndex + 1; rightIndex < nodes.size(); ++rightIndex) {
      const Rect right = expandedNodeRectAt(
        nodes[rightIndex],
        attributes,
        sanitizeNodeCenterX(nodes[rightIndex], attributes),
        sanitizeNodeCenterY(nodes[rightIndex], attributes));
      if (rectsOverlap(left, right)) {
        return true;
      }
    }
  }

  return false;
}

void placeNodesWithoutOverlaps(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 1) {
    return;
  }

  std::vector<std::size_t> order;
  order.reserve(nodes.size());
  double totalWidth = 0.0;
  double totalHeight = 0.0;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    order.push_back(index);
    totalWidth += sanitizeNodeWidth(nodes[index], attributes);
    totalHeight += sanitizeNodeHeight(nodes[index], attributes);
  }

  std::sort(
    order.begin(),
    order.end(),
    [&](std::size_t left, std::size_t right) {
      const double leftY = sanitizeNodeCenterY(nodes[left], attributes);
      const double rightY = sanitizeNodeCenterY(nodes[right], attributes);
      if (std::abs(leftY - rightY) > 0.01) {
        return leftY < rightY;
      }
      return sanitizeNodeCenterX(nodes[left], attributes)
        < sanitizeNodeCenterX(nodes[right], attributes);
    });

  const double averageWidth = totalWidth / static_cast<double>(nodes.size());
  const double averageHeight = totalHeight / static_cast<double>(nodes.size());
  const double stepX = std::max(averageWidth + kPostLayoutNodeGapX, 160.0);
  const double stepY = std::max(averageHeight + kPostLayoutNodeGapY, 120.0);
  const int maxRing = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(nodes.size())))) + 12;
  RectangleCollisionIndex placedRects(stepX, stepY, nodes.size());

  for (std::size_t nodeIndex : order) {
    const NodeRecord& node = nodes[nodeIndex];
    const double desiredX = sanitizeNodeCenterX(node, attributes);
    const double desiredY = sanitizeNodeCenterY(node, attributes);
    double bestX = desiredX;
    double bestY = desiredY;
    bool placed = false;

    const Rect desiredRect = expandedNodeRectAt(node, attributes, desiredX, desiredY);
    if (!placedRects.overlaps(desiredRect)) {
      placed = true;
    }

    for (int ring = 1; !placed && ring <= maxRing; ++ring) {
      double bestDistance = std::numeric_limits<double>::infinity();
      for (int offsetY = -ring; offsetY <= ring; ++offsetY) {
        for (int offsetX = -ring; offsetX <= ring; ++offsetX) {
          if (std::abs(offsetX) != ring && std::abs(offsetY) != ring) {
            continue;
          }

          const double candidateX = desiredX + static_cast<double>(offsetX) * stepX;
          const double candidateY = desiredY + static_cast<double>(offsetY) * stepY;
          const Rect candidateRect = expandedNodeRectAt(node, attributes, candidateX, candidateY);
          if (placedRects.overlaps(candidateRect)) {
            continue;
          }

          const double distance =
            std::pow(candidateX - desiredX, 2.0) + std::pow(candidateY - desiredY, 2.0);
          if (distance < bestDistance) {
            bestDistance = distance;
            bestX = candidateX;
            bestY = candidateY;
            placed = true;
          }
        }
      }
    }

    if (!placed) {
      const std::size_t fallbackIndex = placedRects.size();
      const std::size_t columns = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(nodes.size())))));
      bestX = static_cast<double>(fallbackIndex % columns) * stepX;
      bestY = static_cast<double>(fallbackIndex / columns) * stepY;
    }

    attributes.x(node.handle) = bestX;
    attributes.y(node.handle) = bestY;
    placedRects.insert(expandedNodeRectAt(node, attributes, bestX, bestY));
  }
}

void enforceNodeSeparation(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  resolveNodeOverlaps(nodes, attributes);
  if (hasNodeSpacingConflicts(nodes, attributes)) {
    placeNodesWithoutOverlaps(nodes, attributes);
  }
}

Rect graphNodeBounds(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  Rect bounds;
  bool initialized = false;
  for (const NodeRecord& node : nodes) {
    const Rect rect = nodeRect(node, attributes);
    if (!initialized) {
      bounds = rect;
      initialized = true;
      continue;
    }
    bounds.left = std::min(bounds.left, rect.left);
    bounds.right = std::max(bounds.right, rect.right);
    bounds.top = std::min(bounds.top, rect.top);
    bounds.bottom = std::max(bounds.bottom, rect.bottom);
  }
  return bounds;
}

bool compactExcessiveLayoutFootprint(
  const std::string& mode,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 2) {
    return false;
  }

  const Rect bounds = graphNodeBounds(nodes, attributes);
  const double width = rectWidth(bounds);
  const double height = rectHeight(bounds);
  if (width <= 1.0 || height <= 1.0) {
    return false;
  }

  const double centerX = rectCenterX(bounds);
  const double centerY = rectCenterY(bounds);
  const double nodeFactor = std::sqrt(static_cast<double>(nodes.size()));

  if (
    mode == "fast_multipole"
    || mode == "fast_multipole_multilevel"
    || isConstrainedForceMode(mode)) {
    const double targetMaxDimension = std::max(24000.0, nodeFactor * 680.0);
    const double maxDimension = std::max(width, height);
    if (maxDimension <= targetMaxDimension) {
      return false;
    }

    const double scale = std::max(0.22, targetMaxDimension / maxDimension);
    transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
      return std::make_pair(
        centerX + (x - centerX) * scale,
        centerY + (y - centerY) * scale);
    });
    return true;
  }

  if (isSugiyamaMode(mode)) {
    const double targetWidth = std::max(28000.0, std::max(height * 2.2, nodeFactor * 760.0));
    if (width <= targetWidth) {
      return false;
    }

    const double scaleX = std::max(0.24, targetWidth / width);
    transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
      return std::make_pair(centerX + (x - centerX) * scaleX, y);
    });
    return true;
  }

  return false;
}

double clampToSpan(double value, double minValue, double maxValue) {
  if (minValue > maxValue) {
    return (minValue + maxValue) / 2.0;
  }
  return std::max(minValue, std::min(maxValue, value));
}

bool almostSamePoint(const RoutePoint& left, const RoutePoint& right) {
  return std::abs(left.x - right.x) < 0.01 && std::abs(left.y - right.y) < 0.01;
}

bool isCollinear(const RoutePoint& left, const RoutePoint& middle, const RoutePoint& right) {
  return (
      std::abs(left.x - middle.x) < 0.01
      && std::abs(middle.x - right.x) < 0.01)
    || (
      std::abs(left.y - middle.y) < 0.01
      && std::abs(middle.y - right.y) < 0.01);
}

std::vector<RoutePoint> compressRoutePoints(std::vector<RoutePoint> points) {
  std::vector<RoutePoint> deduped;
  for (RoutePoint point : points) {
    if (!isFiniteCoordinate(point.x) || !isFiniteCoordinate(point.y)) {
      continue;
    }
    point.x = std::round(point.x * 100.0) / 100.0;
    point.y = std::round(point.y * 100.0) / 100.0;
    if (!deduped.empty() && almostSamePoint(deduped.back(), point)) {
      continue;
    }
    deduped.push_back(point);
  }

  std::vector<RoutePoint> compressed;
  for (const RoutePoint& point : deduped) {
    if (compressed.size() >= 2) {
      const RoutePoint& prev = compressed[compressed.size() - 1];
      const RoutePoint& prevPrev = compressed[compressed.size() - 2];
      if (isCollinear(prevPrev, prev, point)) {
        compressed.pop_back();
      }
    }
    compressed.push_back(point);
  }

  return compressed;
}

std::size_t applyPositionsTsvOverride(
  const std::string& positionsTsv,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (positionsTsv.empty()) {
    return 0;
  }
  std::ifstream pf(positionsTsv);
  if (!pf) {
    throw std::runtime_error("failed to open --positions-tsv file: " + positionsTsv);
  }
  std::unordered_map<std::string, std::size_t> idIdx;
  idIdx.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    idIdx[nodes[i].modelId] = i;
  }
  std::string line;
  std::size_t applied = 0;
  while (std::getline(pf, line)) {
    if (line.empty()) continue;
    const auto t1 = line.find('\t');
    if (t1 == std::string::npos) continue;
    const auto t2 = line.find('\t', t1 + 1);
    if (t2 == std::string::npos) continue;
    const std::string mid = line.substr(0, t1);
    const std::string sx = line.substr(t1 + 1, t2 - t1 - 1);
    const std::string sy = line.substr(t2 + 1);
    auto it = idIdx.find(mid);
    if (it == idIdx.end()) continue;
    try {
      const double cx = std::stod(sx);
      const double cy = std::stod(sy);
      attributes.x(nodes[it->second].handle) = cx;
      attributes.y(nodes[it->second].handle) = cy;
      ++applied;
    } catch (const std::exception&) {
      continue;
    }
  }
  return applied;
}

std::size_t applyRoutesTsvOverride(
  const std::string& routesTsv,
  const std::vector<EdgeRecord>& edges,
  std::vector<std::vector<RoutePoint>>& routes) {
  if (routesTsv.empty()) {
    return 0;
  }
  std::unordered_map<std::string, std::size_t> edgeIndexById;
  edgeIndexById.reserve(edges.size());
  for (std::size_t e = 0; e < edges.size(); ++e) {
    edgeIndexById[edges[e].edgeId] = e;
  }
  std::ifstream rf(routesTsv);
  if (!rf) {
    throw std::runtime_error("failed to open --routes-tsv file: " + routesTsv);
  }
  std::size_t appliedRoutes = 0;
  std::string line;
  while (std::getline(rf, line)) {
    if (line.empty()) continue;
    std::vector<std::string> cols;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, '\t')) {
      cols.push_back(cell);
    }
    if (cols.size() < 5 || cols.size() % 2 == 0) {
      continue;
    }
    auto edgeIt = edgeIndexById.find(cols[0]);
    if (edgeIt == edgeIndexById.end() || edgeIt->second >= routes.size()) {
      continue;
    }
    std::vector<RoutePoint> route;
    route.reserve((cols.size() - 1) / 2);
    bool ok = true;
    for (std::size_t i = 1; i + 1 < cols.size(); i += 2) {
      char* xEnd = nullptr;
      char* yEnd = nullptr;
      const double x = std::strtod(cols[i].c_str(), &xEnd);
      const double y = std::strtod(cols[i + 1].c_str(), &yEnd);
      if (
          xEnd == cols[i].c_str()
          || yEnd == cols[i + 1].c_str()
          || !std::isfinite(x)
          || !std::isfinite(y)) {
        ok = false;
        break;
      }
      route.push_back({x, y});
    }
    if (!ok || route.size() < 2) {
      continue;
    }
    routes[edgeIt->second] = compressRoutePoints(std::move(route));
    ++appliedRoutes;
  }
  return appliedRoutes;
}

bool segmentIntersectsRect(const RoutePoint& start, const RoutePoint& end, const Rect& rect) {
  if (std::abs(start.x - end.x) < 1e-9) {
    const double minY = std::min(start.y, end.y);
    const double maxY = std::max(start.y, end.y);
    return start.x > rect.left
      && start.x < rect.right
      && maxY > rect.top
      && minY < rect.bottom;
  }

  if (std::abs(start.y - end.y) < 1e-9) {
    const double minX = std::min(start.x, end.x);
    const double maxX = std::max(start.x, end.x);
    return start.y > rect.top
      && start.y < rect.bottom
      && maxX > rect.left
      && minX < rect.right;
  }

  double minT = 0.0;
  double maxT = 1.0;
  const double dx = end.x - start.x;
  const double dy = end.y - start.y;
  const auto clip = [&](double edge, double distance) {
    if (std::abs(edge) < 1e-9) {
      return distance >= 0.0;
    }

    const double t = distance / edge;
    if (edge < 0.0) {
      if (t > maxT) {
        return false;
      }
      minT = std::max(minT, t);
    } else {
      if (t < minT) {
        return false;
      }
      maxT = std::min(maxT, t);
    }
    return true;
  };

  if (!clip(-dx, start.x - rect.left)) {
    return false;
  }
  if (!clip(dx, rect.right - start.x)) {
    return false;
  }
  if (!clip(-dy, start.y - rect.top)) {
    return false;
  }
  if (!clip(dy, rect.bottom - start.y)) {
    return false;
  }

  // A shallow penetration is still visible at normal zoom and must not be
  // discarded merely because it occupies a small fraction of a long segment.
  // Match the canvas Liang-Barsky audit: reject tangency, but count every
  // positive interior interval above the numerical epsilon.
  return maxT - minT > 1e-9;
}

long long metricLaneKey(double value) {
  return static_cast<long long>(std::llround(value * kMetricCoordinateScale));
}

bool intervalsOverlap(double leftStart, double leftEnd, double rightStart, double rightEnd) {
  return std::min(leftEnd, rightEnd) - std::max(leftStart, rightStart) > 1.0;
}

double distributedLaneOffset(std::size_t lineIndex) {
  constexpr std::size_t laneCount = 73;
  constexpr double laneStep = 8.0;
  const std::size_t lane = (lineIndex * 37) % laneCount;
  const double center = static_cast<double>(laneCount - 1) / 2.0;
  return (static_cast<double>(lane) - center) * laneStep;
}

LineIntent makeLineIntent(
  const EdgeRecord& edge,
  std::size_t lineIndex,
  ogdf::GraphAttributes& attributes) {
  const Rect sourceRect = handleRect(edge.sourceHandle, attributes);
  const Rect targetRect = handleRect(edge.targetHandle, attributes);
  return {
    lineIndex,
    edge.edgeId,
    distributedLaneOffset(lineIndex),
    std::abs(rectCenterX(targetRect) - rectCenterX(sourceRect))
      >= std::abs(rectCenterY(targetRect) - rectCenterY(sourceRect)),
    edge.sourceHandle,
    edge.sourceModelId,
    sourceRect,
    edge.targetHandle,
    edge.targetModelId,
    targetRect,
  };
}

std::vector<NodeObstacle> makeNodeObstacles(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  double margin,
  ogdf::node sourceHandle,
  ogdf::node targetHandle) {
  std::vector<NodeObstacle> obstacles;
  obstacles.reserve(nodes.size());
  for (const NodeRecord& node : nodes) {
    if (node.handle == sourceHandle || node.handle == targetHandle) {
      continue;
    }

    obstacles.push_back({ node.handle, node.modelId, nodeRect(node, attributes, margin) });
  }
  return obstacles;
}

std::vector<Rect> collectObstacleRects(const std::vector<NodeObstacle>& obstacles) {
  std::vector<Rect> rects;
  rects.reserve(obstacles.size());
  for (const NodeObstacle& obstacle : obstacles) {
    rects.push_back(obstacle.rect);
  }
  return rects;
}

std::pair<double, double> leafBundleRenderSize(std::size_t memberCount) {
  const double n = static_cast<double>(std::max<std::size_t>(1, memberCount));
  const double cols = std::max(1.0, std::ceil(std::sqrt(n)));
  const double rows = std::max(1.0, std::ceil(n / cols));
  const double innerW =
    cols * kRenderedLeafCellW + (cols - 1.0) * kRenderedLeafGapX;
  const double innerH =
    rows * kRenderedLeafCellH + (rows - 1.0) * kRenderedLeafGapY;
  return {
    innerW + kRenderedBundlePad * 2.0,
    kRenderedBundleHeader + innerH + kRenderedBundlePad,
  };
}

Rect renderedLeafBundleRect(const LeafBundleRecord& bundle, double margin) {
  const auto [width, height] = leafBundleRenderSize(bundle.leafModelIds.size());
  const double cx = bundle.bboxX + bundle.bboxWidth / 2.0;
  const double cy = bundle.bboxY + bundle.bboxHeight / 2.0;
  Rect rect;
  rect.left = cx - width / 2.0 - margin;
  rect.right = cx + width / 2.0 + margin;
  rect.top = cy - height / 2.0 - margin;
  rect.bottom = cy + height / 2.0 + margin;
  return rect;
}

std::vector<Rect> renderedLeafTileRects(
    const LeafBundleRecord& bundle,
    double margin) {
  std::vector<Rect> rects;
  const std::size_t memberCount = bundle.leafModelIds.size();
  if (memberCount == 0) return rects;
  rects.reserve(memberCount);

  const std::size_t cols = std::max<std::size_t>(
    1,
    static_cast<std::size_t>(
      std::ceil(std::sqrt(static_cast<double>(memberCount)))));
  const auto [outerWidth, outerHeight] = leafBundleRenderSize(memberCount);
  const double cx = bundle.bboxX + bundle.bboxWidth / 2.0;
  const double cy = bundle.bboxY + bundle.bboxHeight / 2.0;
  const double outerLeft = cx - outerWidth / 2.0;
  const double outerTop = cy - outerHeight / 2.0;
  for (std::size_t index = 0; index < memberCount; ++index) {
    const std::size_t col = index % cols;
    const std::size_t row = index / cols;
    const double left = outerLeft + kRenderedBundlePad
      + static_cast<double>(col)
        * (kRenderedLeafCellW + kRenderedLeafGapX);
    const double top = outerTop + kRenderedBundleHeader
      + static_cast<double>(row)
        * (kRenderedLeafCellH + kRenderedLeafGapY);
    rects.push_back({
      top + kRenderedLeafCellH + margin,
      left - margin,
      left + kRenderedLeafCellW + margin,
      top - margin,
    });
  }
  return rects;
}

void recomputeLeafBundleBboxesFromNodes(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (leafBundles.empty()) {
    return;
  }
  std::unordered_map<std::string, std::size_t> idToIndex;
  idToIndex.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    idToIndex[nodes[i].modelId] = i;
  }
  for (LeafBundleRecord& bundle : leafBundles) {
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    double sumX = 0.0;
    double sumY = 0.0;
    std::size_t count = 0;
    for (const std::string& leaf : bundle.leafModelIds) {
      auto it = idToIndex.find(leaf);
      if (it == idToIndex.end()) {
        continue;
      }
      const NodeRecord& node = nodes[it->second];
      const double cx = sanitizeNodeCenterX(node, attributes);
      const double cy = sanitizeNodeCenterY(node, attributes);
      const double w = sanitizeNodeWidth(node, attributes);
      const double h = sanitizeNodeHeight(node, attributes);
      minX = std::min(minX, cx - w / 2.0);
      minY = std::min(minY, cy - h / 2.0);
      maxX = std::max(maxX, cx + w / 2.0);
      maxY = std::max(maxY, cy + h / 2.0);
      sumX += cx;
      sumY += cy;
      ++count;
    }
    if (count == 0 || !std::isfinite(minX)) {
      continue;
    }
    bundle.bboxX = minX;
    bundle.bboxY = minY;
    bundle.bboxWidth = maxX - minX;
    bundle.bboxHeight = maxY - minY;
    const double leafCx = sumX / static_cast<double>(count);
    const double leafCy = sumY / static_cast<double>(count);
    auto parentIt = idToIndex.find(bundle.parentModelId);
    if (parentIt != idToIndex.end()) {
      const NodeRecord& parent = nodes[parentIt->second];
      bundle.anchorX = 0.5 * (sanitizeNodeCenterX(parent, attributes) + leafCx);
      bundle.anchorY = 0.5 * (sanitizeNodeCenterY(parent, attributes) + leafCy);
    }
  }
}

std::size_t clearLeafBundleNodeMargins(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  bool logResult) {
  if (leafBundles.empty() || !readBoolEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL", false)) {
    return 0;
  }

  const int passes = static_cast<int>(
    readDoubleEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PASSES", 3.0, 1.0, 12.0));
  const double maxShift =
    readDoubleEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_MAX_SHIFT", 1200.0, 80.0, 12000.0);
  const double nodeMargin = visualNodeMargin();
  const double bundleMargin = leafBundleVisualMargin();
  const double extraClearance =
    readDoubleEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EXTRA", 12.0, 0.0, 160.0);

  std::unordered_map<std::string, std::size_t> idToIndex;
  idToIndex.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    idToIndex[nodes[i].modelId] = i;
  }

  std::unordered_set<std::string> absorbed;
  for (const LeafBundleRecord& bundle : leafBundles) {
    absorbed.insert(bundle.parentModelId);
    for (const std::string& leaf : bundle.leafModelIds) {
      absorbed.insert(leaf);
    }
  }

  auto translateBundleLeaves = [&](const LeafBundleRecord& bundle, double dx, double dy) {
    for (const std::string& leaf : bundle.leafModelIds) {
      auto it = idToIndex.find(leaf);
      if (it == idToIndex.end()) {
        continue;
      }
      const NodeRecord& node = nodes[it->second];
      attributes.x(node.handle) += dx;
      attributes.y(node.handle) += dy;
    }
  };

  std::size_t moved = 0;
  for (int pass = 0; pass < passes; ++pass) {
    recomputeLeafBundleBboxesFromNodes(leafBundles, nodes, attributes);
    std::size_t movedThisPass = 0;
    for (LeafBundleRecord& bundle : leafBundles) {
      Rect bundleRect = renderedLeafBundleRect(bundle, bundleMargin);
      const double bundleCx = rectCenterX(bundleRect);
      const double bundleCy = rectCenterY(bundleRect);
      double shiftX = 0.0;
      double shiftY = 0.0;
      std::size_t conflicts = 0;

      for (const NodeRecord& node : nodes) {
        if (absorbed.count(node.modelId)) {
          continue;
        }
        const Rect nodeBox = nodeRect(node, attributes, nodeMargin);
        if (!rectsOverlap(bundleRect, nodeBox)) {
          continue;
        }
        const double overlapX =
          std::min(bundleRect.right, nodeBox.right)
          - std::max(bundleRect.left, nodeBox.left);
        const double overlapY =
          std::min(bundleRect.bottom, nodeBox.bottom)
          - std::max(bundleRect.top, nodeBox.top);
        if (overlapX <= 0.0 || overlapY <= 0.0) {
          continue;
        }
        ++conflicts;
        const double nodeCx = rectCenterX(nodeBox);
        const double nodeCy = rectCenterY(nodeBox);
        if (overlapX <= overlapY) {
          const double dir = bundleCx < nodeCx ? -1.0 : 1.0;
          shiftX += dir * (overlapX + extraClearance);
        } else {
          const double dir = bundleCy < nodeCy ? -1.0 : 1.0;
          shiftY += dir * (overlapY + extraClearance);
        }
      }

      if (conflicts == 0) {
        continue;
      }
      const double length = std::hypot(shiftX, shiftY);
      if (length < 0.01) {
        shiftX = (static_cast<int>(moved + movedThisPass) % 2 == 0)
          ? extraClearance
          : -extraClearance;
        shiftY = 0.0;
      } else if (length > maxShift) {
        const double scale = maxShift / length;
        shiftX *= scale;
        shiftY *= scale;
      }
      translateBundleLeaves(bundle, shiftX, shiftY);
      ++moved;
      ++movedThisPass;
    }
    if (movedThisPass == 0) {
      break;
    }
  }

  if (moved > 0 && logResult) {
    recomputeLeafBundleBboxesFromNodes(leafBundles, nodes, attributes);
    std::fprintf(stderr,
      "[leaf-bundle-node-clear-final] moved %zu bundle blocks "
      "(nodeMargin=%.1f bundleMargin=%.1f).\n",
      moved,
      nodeMargin,
      bundleMargin);
  }
  return moved;
}

std::size_t clearLeafBundleExternalNodeMargins(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (leafBundles.empty()
      || !readBoolEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODES", true)) {
    return 0;
  }

  const int passes = static_cast<int>(
    readDoubleEnv(
      "DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_PASSES",
      2.0,
      1.0,
      8.0));
  const double maxShift =
    readDoubleEnv(
      "DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_MAX_SHIFT",
      600.0,
      40.0,
      4000.0);
  const double nodeMargin = visualNodeMargin();
  const double bundleMargin = leafBundleVisualMargin();
  const double extraClearance =
    readDoubleEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EXTRA", 12.0, 0.0, 160.0);

  std::unordered_set<std::string> absorbed;
  for (const LeafBundleRecord& bundle : leafBundles) {
    absorbed.insert(bundle.parentModelId);
    for (const std::string& leaf : bundle.leafModelIds) {
      absorbed.insert(leaf);
    }
    for (const std::string& root : bundle.sharedRootModelIds) {
      absorbed.insert(root);
    }
  }

  std::size_t moved = 0;
  for (int pass = 0; pass < passes; ++pass) {
    recomputeLeafBundleBboxesFromNodes(leafBundles, nodes, attributes);
    std::size_t movedThisPass = 0;
    for (const LeafBundleRecord& bundle : leafBundles) {
      const Rect bundleRect = renderedLeafBundleRect(bundle, bundleMargin);
      const double bundleCx = rectCenterX(bundleRect);
      const double bundleCy = rectCenterY(bundleRect);
      for (const NodeRecord& node : nodes) {
        if (absorbed.count(node.modelId)) {
          continue;
        }
        const Rect nodeBox = nodeRect(node, attributes, nodeMargin);
        if (!rectsOverlap(bundleRect, nodeBox)) {
          continue;
        }
        const double overlapX =
          std::min(bundleRect.right, nodeBox.right)
          - std::max(bundleRect.left, nodeBox.left);
        const double overlapY =
          std::min(bundleRect.bottom, nodeBox.bottom)
          - std::max(bundleRect.top, nodeBox.top);
        if (overlapX <= 0.0 || overlapY <= 0.0) {
          continue;
        }

        double shiftX = 0.0;
        double shiftY = 0.0;
        const double nodeCx = rectCenterX(nodeBox);
        const double nodeCy = rectCenterY(nodeBox);
        if (overlapX <= overlapY) {
          const double dir = nodeCx < bundleCx ? -1.0 : 1.0;
          shiftX = dir * (overlapX + extraClearance);
        } else {
          const double dir = nodeCy < bundleCy ? -1.0 : 1.0;
          shiftY = dir * (overlapY + extraClearance);
        }
        const double length = std::hypot(shiftX, shiftY);
        if (length > maxShift && length > 1e-6) {
          const double scale = maxShift / length;
          shiftX *= scale;
          shiftY *= scale;
        }
        attributes.x(node.handle) += shiftX;
        attributes.y(node.handle) += shiftY;
        ++moved;
        ++movedThisPass;
      }
    }
    if (movedThisPass == 0) {
      break;
    }
  }
  return moved;
}

std::size_t clearNodeVisualOverlaps(
  const std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 1 || !readBoolEnv("DJERD_NODE_OVERLAP_CLEAR_FINAL", false)) {
    return 0;
  }

  const int passes = static_cast<int>(
    readDoubleEnv("DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES", 6.0, 1.0, 24.0));
  const double margin = visualNodeMargin();
  const double extra =
    readDoubleEnv("DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA", 10.0, 0.0, 160.0);
  const double maxShift =
    readDoubleEnv("DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT", 240.0, 8.0, 4000.0);

  std::unordered_set<std::string> absorbed;
  for (const LeafBundleRecord& bundle : leafBundles) {
    absorbed.insert(bundle.parentModelId);
    for (const std::string& leaf : bundle.leafModelIds) {
      absorbed.insert(leaf);
    }
  }

  std::size_t movedTotal = 0;
  for (int pass = 0; pass < passes; ++pass) {
    std::vector<std::pair<Rect, std::size_t>> rects;
    rects.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      if (absorbed.count(nodes[i].modelId)) continue;
      rects.emplace_back(nodeRect(nodes[i], attributes, margin), i);
    }
    std::sort(rects.begin(), rects.end(),
      [](const auto& left, const auto& right) {
        return left.first.left < right.first.left;
      });

    std::vector<double> shiftX(nodes.size(), 0.0);
    std::vector<double> shiftY(nodes.size(), 0.0);
    std::size_t conflicts = 0;
    for (std::size_t leftOrder = 0; leftOrder < rects.size(); ++leftOrder) {
      const Rect& left = rects[leftOrder].first;
      const std::size_t leftIndex = rects[leftOrder].second;
      for (std::size_t rightOrder = leftOrder + 1; rightOrder < rects.size(); ++rightOrder) {
        const Rect& right = rects[rightOrder].first;
        if (right.left >= left.right) break;
        if (!rectsOverlap(left, right)) continue;

        const double overlapX =
          std::min(left.right, right.right) - std::max(left.left, right.left);
        const double overlapY =
          std::min(left.bottom, right.bottom) - std::max(left.top, right.top);
        if (overlapX <= 0.0 || overlapY <= 0.0) continue;

        const std::size_t rightIndex = rects[rightOrder].second;
        ++conflicts;
        if (overlapX <= overlapY) {
          const double dx = rectCenterX(right) - rectCenterX(left);
          const double dir = std::abs(dx) < 0.01
            ? (leftIndex % 2 == 0 ? 1.0 : -1.0)
            : (dx >= 0.0 ? 1.0 : -1.0);
          const double shift = overlapX * 0.5 + extra * 0.5;
          shiftX[leftIndex] -= dir * shift;
          shiftX[rightIndex] += dir * shift;
        } else {
          const double dy = rectCenterY(right) - rectCenterY(left);
          const double dir = std::abs(dy) < 0.01
            ? (leftIndex % 2 == 0 ? 1.0 : -1.0)
            : (dy >= 0.0 ? 1.0 : -1.0);
          const double shift = overlapY * 0.5 + extra * 0.5;
          shiftY[leftIndex] -= dir * shift;
          shiftY[rightIndex] += dir * shift;
        }
      }
    }
    if (conflicts == 0) break;

    std::size_t movedThisPass = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      double dx = shiftX[i];
      double dy = shiftY[i];
      const double length = std::hypot(dx, dy);
      if (length <= 0.01) continue;
      if (length > maxShift) {
        const double scale = maxShift / length;
        dx *= scale;
        dy *= scale;
      }
      attributes.x(nodes[i].handle) = sanitizeNodeCenterX(nodes[i], attributes) + dx;
      attributes.y(nodes[i].handle) = sanitizeNodeCenterY(nodes[i], attributes) + dy;
      ++movedThisPass;
    }
    movedTotal += movedThisPass;
    if (movedThisPass == 0) break;
  }

  return movedTotal;
}

std::size_t clearNodeSpacingOverlaps(
  const std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 1 || !readBoolEnv("DJERD_NODE_SPACING_CLEAR_FINAL", false)) {
    return 0;
  }

  const int passes = static_cast<int>(
    readDoubleEnv("DJERD_NODE_SPACING_CLEAR_FINAL_PASSES", 6.0, 1.0, 24.0));
  const double extra =
    readDoubleEnv("DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA", 8.0, 0.0, 200.0);
  const double maxShift =
    readDoubleEnv("DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT", 220.0, 8.0, 4000.0);
  const std::unordered_set<std::string> absorbed =
    absorbedLeafBundleIds(leafBundles);

  std::size_t movedTotal = 0;
  for (int pass = 0; pass < passes; ++pass) {
    std::vector<std::pair<Rect, std::size_t>> rects;
    rects.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      if (absorbed.count(nodes[i].modelId)) continue;
      rects.emplace_back(
        expandedNodeRectAt(
          nodes[i],
          attributes,
          sanitizeNodeCenterX(nodes[i], attributes),
          sanitizeNodeCenterY(nodes[i], attributes)),
        i);
    }
    std::sort(rects.begin(), rects.end(),
      [](const auto& left, const auto& right) {
        return left.first.left < right.first.left;
      });

    std::vector<double> shiftX(nodes.size(), 0.0);
    std::vector<double> shiftY(nodes.size(), 0.0);
    std::size_t conflicts = 0;
    for (std::size_t leftOrder = 0; leftOrder < rects.size(); ++leftOrder) {
      const Rect& left = rects[leftOrder].first;
      const std::size_t leftIndex = rects[leftOrder].second;
      for (std::size_t rightOrder = leftOrder + 1; rightOrder < rects.size(); ++rightOrder) {
        const Rect& right = rects[rightOrder].first;
        if (right.left >= left.right) break;
        if (!rectsOverlap(left, right)) continue;
        const double overlapX =
          std::min(left.right, right.right) - std::max(left.left, right.left);
        const double overlapY =
          std::min(left.bottom, right.bottom) - std::max(left.top, right.top);
        if (overlapX <= 0.0 || overlapY <= 0.0) continue;

        const std::size_t rightIndex = rects[rightOrder].second;
        ++conflicts;
        if (overlapX <= overlapY) {
          const double dx = rectCenterX(right) - rectCenterX(left);
          const double dir = std::abs(dx) < 0.01
            ? (leftIndex % 2 == 0 ? 1.0 : -1.0)
            : (dx >= 0.0 ? 1.0 : -1.0);
          const double shift = overlapX * 0.5 + extra * 0.5;
          shiftX[leftIndex] -= dir * shift;
          shiftX[rightIndex] += dir * shift;
        } else {
          const double dy = rectCenterY(right) - rectCenterY(left);
          const double dir = std::abs(dy) < 0.01
            ? (leftIndex % 2 == 0 ? 1.0 : -1.0)
            : (dy >= 0.0 ? 1.0 : -1.0);
          const double shift = overlapY * 0.5 + extra * 0.5;
          shiftY[leftIndex] -= dir * shift;
          shiftY[rightIndex] += dir * shift;
        }
      }
    }
    if (conflicts == 0) break;

    std::size_t movedThisPass = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      double dx = shiftX[i];
      double dy = shiftY[i];
      const double length = std::hypot(dx, dy);
      if (length <= 0.01) continue;
      if (length > maxShift) {
        const double scale = maxShift / length;
        dx *= scale;
        dy *= scale;
      }
      attributes.x(nodes[i].handle) =
        sanitizeNodeCenterX(nodes[i], attributes) + dx;
      attributes.y(nodes[i].handle) =
        sanitizeNodeCenterY(nodes[i], attributes) + dy;
      ++movedThisPass;
    }
    movedTotal += movedThisPass;
    if (movedThisPass == 0) break;
  }

  return movedTotal;
}

bool makeLineSegment(
  const std::string& lineId,
  std::size_t lineIndex,
  const RoutePoint& start,
  const RoutePoint& end,
  LineSegment& segment) {
  const bool horizontal = std::abs(start.y - end.y) < 0.01;
  const bool vertical = std::abs(start.x - end.x) < 0.01;
  const double axisStart = horizontal ? start.x : (vertical ? start.y : 0.0);
  const double axisEnd = horizontal
    ? end.x
    : (vertical ? end.y : std::hypot(end.x - start.x, end.y - start.y));
  const double minAxis = std::min(axisStart, axisEnd);
  const double maxAxis = std::max(axisStart, axisEnd);
  if (maxAxis - minAxis <= 1.0) {
    return false;
  }

  segment = {
    maxAxis,
    minAxis,
    horizontal,
    horizontal || vertical ? metricLaneKey(horizontal ? start.y : start.x) : 0,
    lineIndex,
    lineId,
    end,
    start,
    vertical,
  };
  return true;
}

std::vector<LineSegment> buildLineSegments(
  const std::vector<RoutePoint>& points,
  std::size_t lineIndex,
  const std::string& lineId) {
  std::vector<LineSegment> segments;
  if (points.size() < 2) {
    return segments;
  }

  segments.reserve(points.size() - 1);
  for (std::size_t index = 1; index < points.size(); ++index) {
    LineSegment segment;
    if (makeLineSegment(lineId, lineIndex, points[index - 1], points[index], segment)) {
      segments.push_back(segment);
    }
  }

  return segments;
}

double occupancyCostForAxisSegment(
  const RouteOccupancy* occupancy,
  bool horizontal,
  long long laneKey,
  double start,
  double end) {
  if (occupancy == nullptr) {
    return 0.0;
  }

  const double minAxis = std::min(start, end);
  const double maxAxis = std::max(start, end);
  if (maxAxis - minAxis <= 1.0) {
    return 0.0;
  }

  const auto& groups = horizontal
    ? occupancy->horizontalSegmentsByLane
    : occupancy->verticalSegmentsByLane;
  const auto found = groups.find(laneKey);
  if (found == groups.end()) {
    return 0.0;
  }

  double penalty = 0.0;
  for (const LineSegment& used : found->second) {
    if (!intervalsOverlap(minAxis, maxAxis, used.axisStart, used.axisEnd)) {
      continue;
    }
    const double overlap = std::min(maxAxis, used.axisEnd) - std::max(minAxis, used.axisStart);
    penalty += 1'200'000.0 + overlap * 900.0;
  }
  return penalty;
}

double routeLength(const std::vector<RoutePoint>& points) {
  double length = 0.0;
  for (std::size_t index = 1; index < points.size(); ++index) {
    length += std::abs(points[index].x - points[index - 1].x)
      + std::abs(points[index].y - points[index - 1].y);
  }
  return length;
}

double routeOccupancyPenalty(
  const std::vector<RoutePoint>& points,
  const RouteOccupancy* occupancy) {
  if (occupancy == nullptr) {
    return 0.0;
  }

  double penalty = 0.0;
  for (const LineSegment& segment : buildLineSegments(
      points,
      std::numeric_limits<std::size_t>::max(),
      "")) {
    if (!segment.horizontal && !segment.vertical) {
      continue;
    }
    penalty += occupancyCostForAxisSegment(
      occupancy,
      segment.horizontal,
      segment.laneKey,
      segment.axisStart,
      segment.axisEnd);
  }

  return penalty;
}

double routeAxisOverlapDebt(
  const std::vector<RoutePoint>& points,
  const RouteOccupancy* occupancy,
  double lengthWeight) {
  if (occupancy == nullptr) {
    return 0.0;
  }

  // Score the same condition used by edgeSegmentOverlaps: an axis-aligned
  // segment is debt only when its interval overlaps another segment on the
  // same lane. A lane-presence proxy over-penalizes safe same-lane segments and
  // can miss the small overlap debts that final repair is trying to clear.
  double debt = 0.0;
  for (const LineSegment& segment : buildLineSegments(
      points,
      std::numeric_limits<std::size_t>::max(),
      "")) {
    if (!segment.horizontal && !segment.vertical) {
      continue;
    }
    const auto& groups = segment.horizontal
      ? occupancy->horizontalSegmentsByLane
      : occupancy->verticalSegmentsByLane;
    const auto found = groups.find(segment.laneKey);
    if (found == groups.end()) {
      continue;
    }
    for (const LineSegment& used : found->second) {
      if (!intervalsOverlap(
          segment.axisStart,
          segment.axisEnd,
          used.axisStart,
          used.axisEnd)) {
        continue;
      }
      const double overlap =
        std::min(segment.axisEnd, used.axisEnd)
        - std::max(segment.axisStart, used.axisStart);
      if (overlap <= 1.0) {
        continue;
      }
      debt += 1.0 + lengthWeight * overlap;
    }
  }

  return debt;
}

double routeScore(
  const std::vector<RoutePoint>& points,
  const std::vector<NodeObstacle>& obstacles,
  const RouteOccupancy* occupancy) {
  double intersections = 0.0;
  for (const LineSegment& segment : buildLineSegments(
      points,
      std::numeric_limits<std::size_t>::max(),
      "")) {
    for (const NodeObstacle& obstacle : obstacles) {
      if (segmentIntersectsRect(segment.start, segment.end, obstacle.rect)) {
        intersections += 1.0;
      }
    }
  }

  return intersections * 1'000'000.0
    + routeLength(points)
    + static_cast<double>(points.size()) * 20.0
    + routeOccupancyPenalty(points, occupancy);
}

void recordRouteOccupancy(
  const std::vector<RoutePoint>& points,
  const LineIntent& line,
  RouteOccupancy& occupancy) {
  for (const LineSegment& segment : buildLineSegments(points, line.lineIndex, line.lineId)) {
    if (segment.horizontal) {
      occupancy.horizontalSegmentsByLane[segment.laneKey].push_back(segment);
    } else if (segment.vertical) {
      occupancy.verticalSegmentsByLane[segment.laneKey].push_back(segment);
    }
  }
}

void removeRouteOccupancy(
  const std::vector<RoutePoint>& points,
  const LineIntent& line,
  RouteOccupancy& occupancy) {
  for (const LineSegment& segment : buildLineSegments(points, line.lineIndex, line.lineId)) {
    if (!segment.horizontal && !segment.vertical) {
      continue;
    }
    auto& groups = segment.horizontal
      ? occupancy.horizontalSegmentsByLane
      : occupancy.verticalSegmentsByLane;
    auto found = groups.find(segment.laneKey);
    if (found == groups.end()) {
      continue;
    }
    auto& laneSegments = found->second;
    laneSegments.erase(
      std::remove_if(
        laneSegments.begin(),
        laneSegments.end(),
        [&](const LineSegment& used) {
          return used.lineIndex == line.lineIndex;
        }),
      laneSegments.end());
    if (laneSegments.empty()) {
      groups.erase(found);
    }
  }
}

std::size_t countAxisSegmentOverlaps(
  std::vector<LineSegment>& segments,
  std::vector<bool>& overlappingEdgeFlags) {
  std::sort(
    segments.begin(),
    segments.end(),
    [](const LineSegment& left, const LineSegment& right) {
      if (std::abs(left.axisStart - right.axisStart) > 0.01) {
        return left.axisStart < right.axisStart;
      }
      return left.axisEnd < right.axisEnd;
    });

  std::size_t overlaps = 0;
  for (std::size_t leftIndex = 0; leftIndex < segments.size(); ++leftIndex) {
    const LineSegment& left = segments[leftIndex];
    for (std::size_t rightIndex = leftIndex + 1; rightIndex < segments.size(); ++rightIndex) {
      const LineSegment& right = segments[rightIndex];
      if (right.axisStart >= left.axisEnd - 1.0) {
        break;
      }
      if (left.lineIndex == right.lineIndex) {
        continue;
      }
      if (!intervalsOverlap(left.axisStart, left.axisEnd, right.axisStart, right.axisEnd)) {
        continue;
      }

      overlaps += 1;
      if (left.lineIndex < overlappingEdgeFlags.size()) {
        overlappingEdgeFlags[left.lineIndex] = true;
      }
      if (right.lineIndex < overlappingEdgeFlags.size()) {
        overlappingEdgeFlags[right.lineIndex] = true;
      }
    }
  }

  return overlaps;
}

std::size_t countNodeRectOverlaps(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  bool includeSpacing,
  const std::unordered_set<std::string>* ignoredModelIds) {
  std::vector<Rect> rects;
  rects.reserve(nodes.size());
  for (const NodeRecord& node : nodes) {
    if (ignoredModelIds && ignoredModelIds->count(node.modelId)) continue;
    rects.push_back(
      includeSpacing
        ? expandedNodeRectAt(
            node,
            attributes,
            sanitizeNodeCenterX(node, attributes),
            sanitizeNodeCenterY(node, attributes))
        : nodeRect(node, attributes));
  }

  std::sort(
    rects.begin(),
    rects.end(),
    [](const Rect& left, const Rect& right) {
      return left.left < right.left;
    });

  std::size_t overlaps = 0;
  for (std::size_t leftIndex = 0; leftIndex < rects.size(); ++leftIndex) {
    const Rect& left = rects[leftIndex];
    for (std::size_t rightIndex = leftIndex + 1; rightIndex < rects.size(); ++rightIndex) {
      const Rect& right = rects[rightIndex];
      if (right.left >= left.right) {
        break;
      }
      if (rectsOverlap(left, right)) {
        overlaps += 1;
      }
    }
  }

  return overlaps;
}

std::unordered_set<std::string> absorbedLeafBundleIds(
  const std::vector<LeafBundleRecord>& leafBundles) {
  std::unordered_set<std::string> absorbed;
  for (const LeafBundleRecord& bundle : leafBundles) {
    absorbed.insert(bundle.parentModelId);
    for (const std::string& leaf : bundle.leafModelIds) {
      absorbed.insert(leaf);
    }
  }
  return absorbed;
}

std::unordered_set<std::string> renderedLeafTileIds(
  const std::vector<LeafBundleRecord>& leafBundles) {
  std::unordered_set<std::string> leaves;
  for (const LeafBundleRecord& bundle : leafBundles) {
    for (const std::string& leaf : bundle.leafModelIds) {
      leaves.insert(leaf);
    }
  }
  return leaves;
}

Rect expandForRenderedNodeClearance(const Rect& rect) {
  Rect expanded = rect;
  expanded.left -= kPostLayoutNodeGapX / 2.0;
  expanded.right += kPostLayoutNodeGapX / 2.0;
  expanded.top -= kPostLayoutNodeGapY / 2.0;
  expanded.bottom += kPostLayoutNodeGapY / 2.0;
  return expanded;
}

double rectangleClearance(const Rect& left, const Rect& right) {
  const double dx = std::max({
    0.0,
    left.left - right.right,
    right.left - left.right,
  });
  const double dy = std::max({
    0.0,
    left.top - right.bottom,
    right.top - left.bottom,
  });
  return std::hypot(dx, dy);
}

RenderedNodeClearanceMetrics measureRenderedNodeClearance(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  const std::vector<LeafBundleRecord>& leafBundles) {
  const std::unordered_set<std::string> leafTiles =
    renderedLeafTileIds(leafBundles);
  std::vector<Rect> rawRects;
  rawRects.reserve(nodes.size() + leafBundles.size());
  for (const NodeRecord& node : nodes) {
    if (leafTiles.count(node.modelId)) continue;
    rawRects.push_back(nodeRect(node, attributes));
  }
  for (const LeafBundleRecord& bundle : leafBundles) {
    rawRects.push_back(renderedLeafBundleRect(bundle));
  }

  RenderedNodeClearanceMetrics metrics;
  if (rawRects.size() < 2) return metrics;

  metrics.minimum = std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < rawRects.size(); ++i) {
    for (std::size_t j = i + 1; j < rawRects.size(); ++j) {
      metrics.minimum = std::min(
        metrics.minimum,
        rectangleClearance(rawRects[i], rawRects[j]));
    }
  }
  if (!std::isfinite(metrics.minimum)) metrics.minimum = 0.0;

  std::vector<Rect> clearanceRects;
  clearanceRects.reserve(rawRects.size());
  for (const Rect& rect : rawRects) {
    clearanceRects.push_back(expandForRenderedNodeClearance(rect));
  }
  std::sort(
    clearanceRects.begin(),
    clearanceRects.end(),
    [](const Rect& left, const Rect& right) {
      return left.left < right.left;
    });
  for (std::size_t i = 0; i < clearanceRects.size(); ++i) {
    for (std::size_t j = i + 1; j < clearanceRects.size(); ++j) {
      if (clearanceRects[j].left >= clearanceRects[i].right) break;
      if (rectsOverlap(clearanceRects[i], clearanceRects[j])) {
        ++metrics.violations;
      }
    }
  }
  return metrics;
}

std::size_t clearRenderedNodeClearance(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() + leafBundles.size() < 2) return 0;

  const int passes = static_cast<int>(readDoubleEnv(
    "DJERD_RENDERED_NODE_CLEARANCE_FINAL_PASSES", 24.0, 1.0, 48.0));
  const double extra = readDoubleEnv(
    "DJERD_RENDERED_NODE_CLEARANCE_FINAL_EXTRA", 2.0, 0.0, 80.0);
  const double maxShift = readDoubleEnv(
    "DJERD_RENDERED_NODE_CLEARANCE_FINAL_MAX_SHIFT", 1200.0, 20.0, 12000.0);

  std::unordered_map<std::string, std::size_t> nodeIndex;
  nodeIndex.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    nodeIndex[nodes[i].modelId] = i;
  }
  const std::unordered_set<std::string> leafTiles =
    renderedLeafTileIds(leafBundles);
  auto translateBundle = [&](std::size_t bundleIndex, double dx, double dy) {
    if (bundleIndex >= leafBundles.size()) return;
    for (const std::string& leaf : leafBundles[bundleIndex].leafModelIds) {
      auto nodeIt = nodeIndex.find(leaf);
      if (nodeIt == nodeIndex.end()) continue;
      const NodeRecord& node = nodes[nodeIt->second];
      attributes.x(node.handle) += dx;
      attributes.y(node.handle) += dy;
    }
  };

  struct ClearanceObject {
    Rect rect;
    bool bundle = false;
    std::size_t index = 0;
  };

  std::size_t movedTotal = 0;
  for (int pass = 0; pass < passes; ++pass) {
    recomputeLeafBundleBboxesFromNodes(leafBundles, nodes, attributes);
    std::vector<ClearanceObject> objects;
    objects.reserve(nodes.size() + leafBundles.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      if (leafTiles.count(nodes[i].modelId)) continue;
      objects.push_back({
        expandForRenderedNodeClearance(nodeRect(nodes[i], attributes)),
        false,
        i,
      });
    }
    for (std::size_t i = 0; i < leafBundles.size(); ++i) {
      objects.push_back({
        expandForRenderedNodeClearance(renderedLeafBundleRect(leafBundles[i])),
        true,
        i,
      });
    }
    std::sort(
      objects.begin(),
      objects.end(),
      [](const ClearanceObject& left, const ClearanceObject& right) {
        return left.rect.left < right.rect.left;
      });

    std::vector<double> nodeShiftX(nodes.size(), 0.0);
    std::vector<double> nodeShiftY(nodes.size(), 0.0);
    std::vector<double> bundleShiftX(leafBundles.size(), 0.0);
    std::vector<double> bundleShiftY(leafBundles.size(), 0.0);
    std::size_t conflicts = 0;

    auto addShift = [&](const ClearanceObject& object, double dx, double dy) {
      if (object.bundle) {
        bundleShiftX[object.index] += dx;
        bundleShiftY[object.index] += dy;
      } else {
        nodeShiftX[object.index] += dx;
        nodeShiftY[object.index] += dy;
      }
    };

    for (std::size_t i = 0; i < objects.size(); ++i) {
      const ClearanceObject& left = objects[i];
      for (std::size_t j = i + 1; j < objects.size(); ++j) {
        const ClearanceObject& right = objects[j];
        if (right.rect.left >= left.rect.right) break;
        if (!rectsOverlap(left.rect, right.rect)) continue;
        const double overlapX =
          std::min(left.rect.right, right.rect.right)
          - std::max(left.rect.left, right.rect.left);
        const double overlapY =
          std::min(left.rect.bottom, right.rect.bottom)
          - std::max(left.rect.top, right.rect.top);
        if (overlapX <= 0.0 || overlapY <= 0.0) continue;
        ++conflicts;

        const bool useX = overlapX <= overlapY;
        double direction = 1.0;
        if (useX) {
          const double delta = rectCenterX(right.rect) - rectCenterX(left.rect);
          direction = std::abs(delta) < 0.01
            ? ((left.index + right.index) % 2 == 0 ? 1.0 : -1.0)
            : (delta >= 0.0 ? 1.0 : -1.0);
        } else {
          const double delta = rectCenterY(right.rect) - rectCenterY(left.rect);
          direction = std::abs(delta) < 0.01
            ? ((left.index + right.index) % 2 == 0 ? 1.0 : -1.0)
            : (delta >= 0.0 ? 1.0 : -1.0);
        }
        const double required = (useX ? overlapX : overlapY) + extra;

        // Ordinary positions have already passed their own spacing cleanup.
        // Preserve them when only a synthetic bundle is in conflict; moving
        // the bundle's leaf block is both cheaper and less disruptive to the
        // graph topology. Split bundle/bundle and node/node corrections.
        if (left.bundle != right.bundle) {
          const ClearanceObject& bundleObject = left.bundle ? left : right;
          const double bundleDirection = left.bundle ? -direction : direction;
          addShift(
            bundleObject,
            useX ? bundleDirection * required : 0.0,
            useX ? 0.0 : bundleDirection * required);
        } else {
          const double half = required / 2.0;
          addShift(
            left,
            useX ? -direction * half : 0.0,
            useX ? 0.0 : -direction * half);
          addShift(
            right,
            useX ? direction * half : 0.0,
            useX ? 0.0 : direction * half);
        }
      }
    }
    if (conflicts == 0) break;

    auto clampShift = [&](double& dx, double& dy) {
      const double length = std::hypot(dx, dy);
      if (length > maxShift) {
        const double scale = maxShift / length;
        dx *= scale;
        dy *= scale;
      }
    };
    std::size_t movedThisPass = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      double dx = nodeShiftX[i];
      double dy = nodeShiftY[i];
      clampShift(dx, dy);
      if (std::hypot(dx, dy) < 0.01) continue;
      attributes.x(nodes[i].handle) += dx;
      attributes.y(nodes[i].handle) += dy;
      ++movedThisPass;
    }
    for (std::size_t i = 0; i < leafBundles.size(); ++i) {
      double dx = bundleShiftX[i];
      double dy = bundleShiftY[i];
      clampShift(dx, dy);
      if (std::hypot(dx, dy) < 0.01) continue;
      translateBundle(i, dx, dy);
      ++movedThisPass;
    }

    // A bundle can be trapped between obstacles on opposite sides, making
    // summed repulsion cancel forever. For each still-conflicting bundle,
    // search the finite set of nearest separating X/Y projections and choose
    // the smallest move that strictly reduces its total conflicts. Candidate
    // coordinates come only from current rectangles, so this remains
    // project-agnostic and deterministic.
    recomputeLeafBundleBboxesFromNodes(leafBundles, nodes, attributes);
    for (std::size_t bundleIndex = 0;
         bundleIndex < leafBundles.size();
         ++bundleIndex) {
      const Rect current = expandForRenderedNodeClearance(
        renderedLeafBundleRect(leafBundles[bundleIndex]));
      std::vector<Rect> obstacles;
      obstacles.reserve(nodes.size() + leafBundles.size() - 1);
      for (const NodeRecord& node : nodes) {
        if (leafTiles.count(node.modelId)) continue;
        obstacles.push_back(
          expandForRenderedNodeClearance(nodeRect(node, attributes)));
      }
      for (std::size_t other = 0; other < leafBundles.size(); ++other) {
        if (other == bundleIndex) continue;
        obstacles.push_back(expandForRenderedNodeClearance(
          renderedLeafBundleRect(leafBundles[other])));
      }

      std::vector<const Rect*> currentConflicts;
      for (const Rect& obstacle : obstacles) {
        if (rectsOverlap(current, obstacle)) {
          currentConflicts.push_back(&obstacle);
        }
      }
      if (currentConflicts.empty()) continue;

      std::vector<double> dxCandidates{0.0};
      std::vector<double> dyCandidates{0.0};
      auto appendDistinct = [](std::vector<double>& values, double candidate) {
        for (double value : values) {
          if (std::abs(value - candidate) < 1e-6) return;
        }
        values.push_back(candidate);
      };
      for (const Rect* obstacle : currentConflicts) {
        appendDistinct(
          dxCandidates, obstacle->left - current.right - extra);
        appendDistinct(
          dxCandidates, obstacle->right - current.left + extra);
        appendDistinct(
          dyCandidates, obstacle->top - current.bottom - extra);
        appendDistinct(
          dyCandidates, obstacle->bottom - current.top + extra);
      }

      std::size_t bestConflicts = currentConflicts.size();
      double bestDistance = std::numeric_limits<double>::infinity();
      double bestDx = 0.0;
      double bestDy = 0.0;
      for (double dx : dxCandidates) {
        for (double dy : dyCandidates) {
          const double distance = std::hypot(dx, dy);
          if (distance < 0.01 || distance > maxShift) continue;
          Rect candidate = current;
          candidate.left += dx;
          candidate.right += dx;
          candidate.top += dy;
          candidate.bottom += dy;
          std::size_t candidateConflicts = 0;
          for (const Rect& obstacle : obstacles) {
            if (rectsOverlap(candidate, obstacle)) {
              ++candidateConflicts;
            }
          }
          if (
              candidateConflicts < bestConflicts
              || (
                candidateConflicts == bestConflicts
                && candidateConflicts < currentConflicts.size()
                && distance < bestDistance)) {
            bestConflicts = candidateConflicts;
            bestDistance = distance;
            bestDx = dx;
            bestDy = dy;
          }
        }
      }
      if (bestConflicts >= currentConflicts.size()
          || !std::isfinite(bestDistance)) {
        continue;
      }
      translateBundle(bundleIndex, bestDx, bestDy);
      recomputeLeafBundleBboxesFromNodes(leafBundles, nodes, attributes);
      ++movedThisPass;
    }
    movedTotal += movedThisPass;
    if (movedThisPass == 0) break;
  }
  recomputeLeafBundleBboxesFromNodes(leafBundles, nodes, attributes);
  return movedTotal;
}

RenderedDensityMetrics measureRenderedDensity(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  const std::vector<LeafBundleRecord>& leafBundles,
  double requestedCellSize) {
  RenderedDensityMetrics metrics;
  const double nodeMargin = visualNodeMargin();
  const double bundleMargin = leafBundleVisualMargin();
  const double cellSize = std::max(200.0, requestedCellSize);
  const std::unordered_set<std::string> absorbed =
    absorbedLeafBundleIds(leafBundles);

  std::vector<Rect> objects;
  objects.reserve(nodes.size() + leafBundles.size());
  for (const NodeRecord& node : nodes) {
    if (absorbed.count(node.modelId)) continue;
    objects.push_back(nodeRect(node, attributes, nodeMargin));
  }
  for (const LeafBundleRecord& bundle : leafBundles) {
    objects.push_back(renderedLeafBundleRect(bundle, bundleMargin));
  }
  metrics.objectCount = objects.size();
  if (objects.empty()) {
    return metrics;
  }

  Rect bounds = objects.front();
  for (const Rect& rect : objects) {
    bounds.left = std::min(bounds.left, rect.left);
    bounds.right = std::max(bounds.right, rect.right);
    bounds.top = std::min(bounds.top, rect.top);
    bounds.bottom = std::max(bounds.bottom, rect.bottom);
  }
  const double width = rectWidth(bounds);
  const double height = rectHeight(bounds);
  if (width <= 1.0 || height <= 1.0) {
    return metrics;
  }

  const int gridW = std::clamp(
    static_cast<int>(std::ceil(width / cellSize)),
    1,
    400);
  const int gridH = std::clamp(
    static_cast<int>(std::ceil(height / cellSize)),
    1,
    400);
  const double actualCellW = width / static_cast<double>(gridW);
  const double actualCellH = height / static_cast<double>(gridH);
  std::vector<int> counts(static_cast<std::size_t>(gridW) * gridH, 0);

  for (const Rect& rect : objects) {
    int gx = static_cast<int>((rectCenterX(rect) - bounds.left) / actualCellW);
    int gy = static_cast<int>((rectCenterY(rect) - bounds.top) / actualCellH);
    gx = std::clamp(gx, 0, gridW - 1);
    gy = std::clamp(gy, 0, gridH - 1);
    counts[static_cast<std::size_t>(gy) * gridW + gx] += 1;
  }

  std::vector<int> occupied;
  occupied.reserve(counts.size());
  for (int count : counts) {
    if (count == 0) {
      ++metrics.emptyCells;
    } else {
      occupied.push_back(count);
    }
  }
  metrics.totalCells = counts.size();
  metrics.occupiedCells = occupied.size();
  metrics.emptyRatio = metrics.totalCells > 0
    ? static_cast<double>(metrics.emptyCells) / static_cast<double>(metrics.totalCells)
    : 0.0;
  if (occupied.empty()) {
    return metrics;
  }

  std::sort(occupied.begin(), occupied.end());
  auto percentile = [&](double p) {
    const std::size_t idx = std::min<std::size_t>(
      occupied.size() - 1,
      static_cast<std::size_t>(std::floor(p * static_cast<double>(occupied.size() - 1))));
    return static_cast<double>(occupied[idx]);
  };
  metrics.p50 = percentile(0.50);
  metrics.p90 = percentile(0.90);
  metrics.maxCell = static_cast<double>(occupied.back());
  metrics.imbalance = metrics.p50 > 0.0
    ? metrics.p90 / metrics.p50
    : metrics.p90;
  const int denseThreshold = std::max(3, static_cast<int>(std::ceil(metrics.p90)));
  for (int count : occupied) {
    if (count >= denseThreshold) ++metrics.denseCells;
  }
  metrics.score =
    metrics.imbalance * 100.0
    + metrics.p90 * 12.0
    + metrics.maxCell * 6.0
    + metrics.emptyRatio * 10.0;
  return metrics;
}

bool compactRigidLayoutFootprint(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 2 || !readBoolEnv("DJERD_RIGID_COMPACT_BBOX_FINAL", false)) {
    return false;
  }

  const Rect bounds = graphNodeBounds(nodes, attributes);
  const double width = rectWidth(bounds);
  const double height = rectHeight(bounds);
  if (width <= 1.0 || height <= 1.0) {
    return false;
  }

  const double area = width * height;
  const double targetArea =
    readDoubleEnv("DJERD_RIGID_COMPACT_BBOX_TARGET_B", 6.0, 0.25, 80.0) * 1e9;
  if (area <= targetArea) {
    return false;
  }

  const double minScale =
    readDoubleEnv("DJERD_RIGID_COMPACT_BBOX_MIN_SCALE", 0.45, 0.10, 1.0);
  const double desiredScale =
    std::clamp(std::sqrt(targetArea / area), minScale, 1.0);
  if (desiredScale >= 0.995) {
    return false;
  }

  std::vector<std::pair<double, double>> original;
  original.reserve(nodes.size());
  for (const NodeRecord& node : nodes) {
    original.push_back({
      sanitizeNodeCenterX(node, attributes),
      sanitizeNodeCenterY(node, attributes),
    });
  }
  const double centerX = rectCenterX(bounds);
  const double centerY = rectCenterY(bounds);
  const std::size_t baseBareOverlaps = countNodeRectOverlaps(nodes, attributes, false);
  const std::size_t baseSpacingOverlaps = countNodeRectOverlaps(nodes, attributes, true);
  auto countVisualMarginOverlaps = [&]() {
    const double margin = visualNodeMargin();
    std::vector<Rect> rects;
    rects.reserve(nodes.size());
    for (const NodeRecord& node : nodes) {
      rects.push_back(nodeRect(node, attributes, margin));
    }
    std::sort(rects.begin(), rects.end(),
      [](const Rect& left, const Rect& right) {
        return left.left < right.left;
      });
    std::size_t overlaps = 0;
    for (std::size_t i = 0; i < rects.size(); ++i) {
      for (std::size_t j = i + 1; j < rects.size(); ++j) {
        if (rects[j].left >= rects[i].right) {
          break;
        }
        if (rectsOverlap(rects[i], rects[j])) {
          ++overlaps;
        }
      }
    }
    return overlaps;
  };
  const std::size_t baseVisualOverlaps = countVisualMarginOverlaps();
  const std::size_t spacingSlack = static_cast<std::size_t>(
    readDoubleEnv("DJERD_RIGID_COMPACT_BBOX_SPACING_SLACK", 0.0, 0.0, 100000.0));
  const std::size_t visualSlack = static_cast<std::size_t>(
    readDoubleEnv("DJERD_RIGID_COMPACT_BBOX_VISUAL_SLACK", 0.0, 0.0, 100000.0));

  auto restore = [&]() {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      attributes.x(nodes[i].handle) = original[i].first;
      attributes.y(nodes[i].handle) = original[i].second;
    }
  };

  auto applyScale = [&](double scale) {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      const double x = original[i].first;
      const double y = original[i].second;
      attributes.x(nodes[i].handle) = centerX + (x - centerX) * scale;
      attributes.y(nodes[i].handle) = centerY + (y - centerY) * scale;
    }
  };

  auto valid = [&]() {
    return countNodeRectOverlaps(nodes, attributes, false) <= baseBareOverlaps
      && countVisualMarginOverlaps() <= baseVisualOverlaps + visualSlack
      && countNodeRectOverlaps(nodes, attributes, true)
        <= baseSpacingOverlaps + spacingSlack;
  };

  double bestScale = 1.0;
  if (readBoolEnv("DJERD_RIGID_COMPACT_BBOX_UNIFORM", true)) {
    applyScale(desiredScale);
    if (valid()) {
      bestScale = desiredScale;
    } else {
      double lo = desiredScale;
      double hi = 1.0;
      for (int iter = 0; iter < 18; ++iter) {
        const double mid = (lo + hi) / 2.0;
        applyScale(mid);
        if (valid()) {
          bestScale = mid;
          hi = mid;
        } else {
          lo = mid;
        }
      }
    }
  }

  if (bestScale >= 0.995) {
    restore();
    if (!readBoolEnv("DJERD_RIGID_COMPACT_BBOX_WHITESPACE", false)) {
      return false;
    }
    const double gap =
      readDoubleEnv("DJERD_RIGID_COMPACT_BBOX_WHITESPACE_GAP", 28.0, 0.0, 240.0);
    compactGlobalLayout(nodes, attributes, gap);
    if (!valid()) {
      restore();
      return false;
    }
    const Rect packedBounds = graphNodeBounds(nodes, attributes);
    const double packedArea = rectWidth(packedBounds) * rectHeight(packedBounds);
    if (packedArea >= area * 0.98) {
      restore();
      return false;
    }
    std::fprintf(stderr,
      "[rigid-bbox-compact] whitespace gap=%.1f bbox %.2fB -> %.2fB "
      "(nodeOverlaps=%zu visual=%zu spacing=%zu).\n",
      gap,
      area / 1e9,
      packedArea / 1e9,
      countNodeRectOverlaps(nodes, attributes, false),
      countVisualMarginOverlaps(),
      countNodeRectOverlaps(nodes, attributes, true));
    return true;
  }

  applyScale(bestScale);
  const Rect nextBounds = graphNodeBounds(nodes, attributes);
  std::fprintf(stderr,
    "[rigid-bbox-compact] scale=%.3f bbox %.2fB -> %.2fB "
    "(nodeOverlaps=%zu spacing=%zu).\n",
    bestScale,
    area / 1e9,
    (rectWidth(nextBounds) * rectHeight(nextBounds)) / 1e9,
    countNodeRectOverlaps(nodes, attributes, false),
    countNodeRectOverlaps(nodes, attributes, true));
  return true;
}

std::vector<std::string> modelNameTokens(const std::string& modelId) {
  std::string leaf = modelId;
  const std::size_t dot = leaf.rfind('.');
  if (dot != std::string::npos && dot + 1 < leaf.size()) {
    leaf = leaf.substr(dot + 1);
  }

  std::vector<std::string> tokens;
  std::string current;
  auto flush = [&]() {
    if (current.size() >= 2) {
      tokens.push_back(current);
    }
    current.clear();
  };

  for (std::size_t i = 0; i < leaf.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(leaf[i]);
    if (!std::isalnum(ch)) {
      flush();
      continue;
    }
    const bool upper = std::isupper(ch);
    if (upper && !current.empty()) {
      flush();
    }
    current.push_back(static_cast<char>(std::tolower(ch)));
  }
  flush();
  std::sort(tokens.begin(), tokens.end());
  tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
  return tokens;
}

std::size_t compactIsolatedBBoxOutliers(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  if (!readBoolEnv("DJERD_ISOLATED_BBOX_COMPACT_FINAL", false) || nodes.empty()) {
    return 0;
  }

  std::unordered_set<std::string> connectedIds;
  connectedIds.reserve(edges.size() * 2);
  for (const EdgeRecord& edge : edges) {
    connectedIds.insert(edge.sourceModelId);
    connectedIds.insert(edge.targetModelId);
  }

  std::vector<std::size_t> connected;
  std::vector<std::size_t> isolated;
  connected.reserve(nodes.size());
  isolated.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (connectedIds.count(nodes[i].modelId)) {
      connected.push_back(i);
    } else {
      isolated.push_back(i);
    }
  }
  if (connected.empty() || isolated.empty()) {
    return 0;
  }

  Rect connectedBounds;
  bool initialized = false;
  for (std::size_t idx : connected) {
    const Rect rect = nodeRect(nodes[idx], attributes);
    if (!initialized) {
      connectedBounds = rect;
      initialized = true;
    } else {
      connectedBounds.left = std::min(connectedBounds.left, rect.left);
      connectedBounds.right = std::max(connectedBounds.right, rect.right);
      connectedBounds.top = std::min(connectedBounds.top, rect.top);
      connectedBounds.bottom = std::max(connectedBounds.bottom, rect.bottom);
    }
  }
  if (!initialized
      || rectWidth(connectedBounds) <= 1.0
      || rectHeight(connectedBounds) <= 1.0) {
    return 0;
  }

  const Rect beforeBounds = graphNodeBounds(nodes, attributes);
  const double beforeArea = rectWidth(beforeBounds) * rectHeight(beforeBounds);
  if (beforeArea <= 1.0) {
    return 0;
  }

  const double outlierMargin =
    readDoubleEnv("DJERD_ISOLATED_BBOX_COMPACT_MARGIN", 8.0, 0.0, 240.0);
  const bool compactAll =
    readBoolEnv("DJERD_ISOLATED_BBOX_COMPACT_ALL", false);

  struct Candidate {
    std::size_t index = 0;
    std::string app;
    std::vector<std::string> tokens;
    std::string modelId;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(isolated.size());
  for (std::size_t idx : isolated) {
    const Rect rect = nodeRect(nodes[idx], attributes);
    const bool expandsConnectedBounds =
      rect.left < connectedBounds.left - outlierMargin
      || rect.right > connectedBounds.right + outlierMargin
      || rect.top < connectedBounds.top - outlierMargin
      || rect.bottom > connectedBounds.bottom + outlierMargin;
    if (!compactAll && !expandsConnectedBounds) {
      continue;
    }
    const NodeRecord& node = nodes[idx];
    const std::size_t dot = node.modelId.find('.');
    candidates.push_back({
      idx,
      dot == std::string::npos ? node.appLabel : node.modelId.substr(0, dot),
      modelNameTokens(node.modelId),
      node.modelId,
    });
  }
  if (candidates.empty()) {
    return 0;
  }

  std::sort(candidates.begin(), candidates.end(),
    [](const Candidate& left, const Candidate& right) {
      if (left.app != right.app) return left.app < right.app;
      if (left.tokens != right.tokens) return left.tokens < right.tokens;
      return left.modelId < right.modelId;
    });

  std::vector<std::pair<double, double>> original;
  original.reserve(candidates.size());
  for (const Candidate& candidate : candidates) {
    const NodeRecord& node = nodes[candidate.index];
    original.push_back({attributes.x(node.handle), attributes.y(node.handle)});
  }

  const double gapX =
    readDoubleEnv("DJERD_ISOLATED_BBOX_COMPACT_GAP_X", 220.0, 0.0, 2000.0);
  const double gapY =
    readDoubleEnv("DJERD_ISOLATED_BBOX_COMPACT_GAP_Y", 46.0, 0.0, 1000.0);
  const double offsetY =
    readDoubleEnv("DJERD_ISOLATED_BBOX_COMPACT_OFFSET_Y", 180.0, 0.0, 4000.0);
  const double minGain =
    readDoubleEnv("DJERD_ISOLATED_BBOX_COMPACT_MIN_GAIN", 0.01, 0.0, 0.9);

  const double startX = connectedBounds.left;
  const double maxRight = connectedBounds.right;
  double x = startX;
  double rowTop = connectedBounds.bottom + offsetY;
  double rowHeight = 0.0;
  for (const Candidate& candidate : candidates) {
    const NodeRecord& node = nodes[candidate.index];
    const double width = sanitizeNodeWidth(node, attributes);
    const double height = sanitizeNodeHeight(node, attributes);
    if (x > startX && x + width > maxRight) {
      x = startX;
      rowTop += rowHeight + gapY;
      rowHeight = 0.0;
    }
    attributes.x(node.handle) = x + width / 2.0;
    attributes.y(node.handle) = rowTop + height / 2.0;
    x += width + gapX;
    rowHeight = std::max(rowHeight, height);
  }

  const Rect afterBounds = graphNodeBounds(nodes, attributes);
  const double afterArea = rectWidth(afterBounds) * rectHeight(afterBounds);
  if (!(afterArea < beforeArea * (1.0 - minGain))) {
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      const NodeRecord& node = nodes[candidates[i].index];
      attributes.x(node.handle) = original[i].first;
      attributes.y(node.handle) = original[i].second;
    }
    return 0;
  }

  std::fprintf(stderr,
    "[isolated-bbox-compact-final] moved %zu/%zu edge-less outliers "
    "bbox %.2fB -> %.2fB (connected %.2fB).\n",
    candidates.size(),
    isolated.size(),
    beforeArea / 1e9,
    afterArea / 1e9,
    (rectWidth(connectedBounds) * rectHeight(connectedBounds)) / 1e9);
  return candidates.size();
}

std::size_t compactSidecarBBoxComponents(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  if (!readBoolEnv("DJERD_SIDECAR_BBOX_COMPACT_FINAL", false)
      || nodes.size() <= 2
      || edges.empty()) {
    return 0;
  }

  std::unordered_map<std::string, std::size_t> idToIndex;
  idToIndex.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    idToIndex[nodes[i].modelId] = i;
  }
  std::vector<std::size_t> degree(nodes.size(), 0);
  for (const EdgeRecord& edge : edges) {
    auto sIt = idToIndex.find(edge.sourceModelId);
    auto tIt = idToIndex.find(edge.targetModelId);
    if (sIt == idToIndex.end()
        || tIt == idToIndex.end()
        || sIt->second == tIt->second) {
      continue;
    }
    ++degree[sIt->second];
    ++degree[tIt->second];
  }

  std::vector<std::vector<std::size_t>> components =
    collectConnectedComponents(nodes, edges);
  if (components.size() <= 1) {
    return 0;
  }

  std::size_t mainComponent = std::numeric_limits<std::size_t>::max();
  for (std::size_t ci = 0; ci < components.size(); ++ci) {
    bool hasEdge = false;
    for (std::size_t idx : components[ci]) {
      if (degree[idx] > 0) {
        hasEdge = true;
        break;
      }
    }
    if (!hasEdge) {
      continue;
    }
    if (mainComponent == std::numeric_limits<std::size_t>::max()
        || components[ci].size() > components[mainComponent].size()) {
      mainComponent = ci;
    }
  }
  if (mainComponent == std::numeric_limits<std::size_t>::max()) {
    return 0;
  }

  const Rect mainRect = componentRect(nodes, components[mainComponent], attributes);
  const double mainWidth = rectWidth(mainRect);
  const double mainHeight = rectHeight(mainRect);
  if (mainWidth <= 1.0 || mainHeight <= 1.0) {
    return 0;
  }

  const Rect beforeBounds = graphNodeBounds(nodes, attributes);
  const double beforeArea = rectWidth(beforeBounds) * rectHeight(beforeBounds);
  if (beforeArea <= 1.0) {
    return 0;
  }
  const std::size_t beforeBareOverlaps =
    countNodeRectOverlaps(nodes, attributes, false);
  const std::size_t beforeSpacingOverlaps =
    countNodeRectOverlaps(nodes, attributes, true);

  const double outlierMargin =
    readDoubleEnv("DJERD_SIDECAR_BBOX_COMPACT_MARGIN", 8.0, 0.0, 240.0);
  const bool moveConnectedComponents =
    readBoolEnv("DJERD_SIDECAR_BBOX_COMPACT_CONNECTED", true);
  const bool moveIsolated =
    readBoolEnv("DJERD_SIDECAR_BBOX_COMPACT_ISOLATED", true);

  auto expandsMain = [&](const Rect& rect) {
    return rect.left < mainRect.left - outlierMargin
      || rect.right > mainRect.right + outlierMargin
      || rect.top < mainRect.top - outlierMargin
      || rect.bottom > mainRect.bottom + outlierMargin;
  };

  struct SidecarItem {
    std::vector<std::size_t> indices;
    Rect rect;
    bool isolated = false;
    double area = 0.0;
    std::string app;
    std::vector<std::string> tokens;
    std::string modelId;
  };

  std::vector<SidecarItem> items;
  items.reserve(components.size());
  for (std::size_t ci = 0; ci < components.size(); ++ci) {
    if (ci == mainComponent) {
      continue;
    }
    const Rect rect = componentRect(nodes, components[ci], attributes);
    if (!expandsMain(rect)) {
      continue;
    }
    bool hasEdge = false;
    for (std::size_t idx : components[ci]) {
      if (degree[idx] > 0) {
        hasEdge = true;
        break;
      }
    }
    if (hasEdge && !moveConnectedComponents) {
      continue;
    }
    if (!hasEdge && !moveIsolated) {
      continue;
    }
    SidecarItem item;
    item.indices = components[ci];
    item.rect = rect;
    item.isolated = !hasEdge;
    item.area = rectWidth(rect) * rectHeight(rect);
    if (!components[ci].empty()) {
      const NodeRecord& node = nodes[components[ci].front()];
      const std::size_t dot = node.modelId.find('.');
      item.app = dot == std::string::npos
        ? node.appLabel
        : node.modelId.substr(0, dot);
      item.tokens = modelNameTokens(node.modelId);
      item.modelId = node.modelId;
    }
    items.push_back(std::move(item));
  }
  if (items.empty()) {
    return 0;
  }

  std::sort(items.begin(), items.end(),
    [](const SidecarItem& left, const SidecarItem& right) {
      if (left.isolated != right.isolated) return !left.isolated;
      if (!left.isolated && std::abs(left.area - right.area) > 0.01) {
        return left.area > right.area;
      }
      if (left.app != right.app) return left.app < right.app;
      if (left.tokens != right.tokens) return left.tokens < right.tokens;
      return left.modelId < right.modelId;
    });

  std::vector<std::pair<std::size_t, std::pair<double, double>>> snapshot;
  std::unordered_set<std::size_t> snapSeen;
  for (const SidecarItem& item : items) {
    for (std::size_t idx : item.indices) {
      if (!snapSeen.insert(idx).second) {
        continue;
      }
      snapshot.push_back({
        idx,
        {attributes.x(nodes[idx].handle), attributes.y(nodes[idx].handle)}
      });
    }
  }

  const double gapX =
    readDoubleEnv("DJERD_SIDECAR_BBOX_COMPACT_GAP_X", 300.0, 0.0, 4000.0);
  const double gapY =
    readDoubleEnv("DJERD_SIDECAR_BBOX_COMPACT_GAP_Y", 180.0, 0.0, 4000.0);
  const double maxLaneHeight =
    readDoubleEnv(
      "DJERD_SIDECAR_BBOX_COMPACT_LANE_HEIGHT",
      mainHeight,
      1000.0,
      std::max(mainHeight, beforeArea));
  const double minGain =
    readDoubleEnv("DJERD_SIDECAR_BBOX_COMPACT_MIN_GAIN", 0.01, 0.0, 0.9);
  const double maxAspect =
    readDoubleEnv("DJERD_SIDECAR_BBOX_COMPACT_MAX_ASPECT", 2.2, 1.0, 10.0);
  const std::size_t spacingSlack = static_cast<std::size_t>(
    readDoubleEnv("DJERD_SIDECAR_BBOX_COMPACT_SPACING_SLACK", 0.0, 0.0, 100000.0));

  double columnX = mainRect.right + gapX;
  double columnY = mainRect.top;
  double columnWidth = 0.0;
  std::size_t columns = 1;
  for (const SidecarItem& item : items) {
    const double width = rectWidth(item.rect);
    const double height = rectHeight(item.rect);
    if (columnY > mainRect.top && columnY + height > mainRect.top + maxLaneHeight) {
      columnX += columnWidth + gapX;
      columnY = mainRect.top;
      columnWidth = 0.0;
      ++columns;
    }
    const double dx = columnX - item.rect.left;
    const double dy = columnY - item.rect.top;
    translateComponent(nodes, item.indices, attributes, dx, dy);
    columnY += height + gapY;
    columnWidth = std::max(columnWidth, width);
  }

  const Rect afterBounds = graphNodeBounds(nodes, attributes);
  const double afterWidth = rectWidth(afterBounds);
  const double afterHeight = rectHeight(afterBounds);
  const double afterArea = afterWidth * afterHeight;
  const double bigger = std::max(afterWidth, afterHeight);
  const double smaller = std::max(1.0, std::min(afterWidth, afterHeight));
  const double aspect = bigger / smaller;
  const bool accepted =
    afterArea < beforeArea * (1.0 - minGain)
    && aspect <= maxAspect
    && countNodeRectOverlaps(nodes, attributes, false) <= beforeBareOverlaps
    && countNodeRectOverlaps(nodes, attributes, true)
      <= beforeSpacingOverlaps + spacingSlack;

  if (!accepted) {
    for (const auto& entry : snapshot) {
      const std::size_t idx = entry.first;
      attributes.x(nodes[idx].handle) = entry.second.first;
      attributes.y(nodes[idx].handle) = entry.second.second;
    }
    return 0;
  }

  std::size_t connectedItems = 0;
  std::size_t isolatedItems = 0;
  for (const SidecarItem& item : items) {
    if (item.isolated) {
      ++isolatedItems;
    } else {
      ++connectedItems;
    }
  }
  std::fprintf(stderr,
    "[sidecar-bbox-compact-final] moved %zu connected components and "
    "%zu edge-less nodes into %zu sidecar columns; bbox %.2fB -> %.2fB "
    "(aspect=%.3f).\n",
    connectedItems,
    isolatedItems,
    columns,
    beforeArea / 1e9,
    afterArea / 1e9,
    aspect);
  return snapshot.size();
}

std::size_t attachIsolatedNodesByName(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  const bool enabled = readBoolEnv(
    "DJERD_ATTACH_ISOLATED_BY_NAME_FINAL",
    readBoolEnv("DJERD_RIGID_ATTACH_ISOLATED_FINAL", false));
  if (!enabled || nodes.empty()) {
    return 0;
  }

  std::unordered_set<std::string> connectedIds;
  connectedIds.reserve(edges.size() * 2);
  for (const EdgeRecord& edge : edges) {
    connectedIds.insert(edge.sourceModelId);
    connectedIds.insert(edge.targetModelId);
  }

  std::vector<std::size_t> connected;
  std::vector<std::size_t> isolated;
  connected.reserve(nodes.size());
  isolated.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (connectedIds.count(nodes[i].modelId)) {
      connected.push_back(i);
    } else {
      isolated.push_back(i);
    }
  }
  if (connected.empty() || isolated.empty()) {
    return 0;
  }

  const double margin = visualNodeMargin();
  const double maxRadius =
    readDoubleEnv("DJERD_RIGID_ATTACH_ISOLATED_MAX_RADIUS", 3600.0, 400.0, 24000.0);
  const double ringStep =
    readDoubleEnv("DJERD_RIGID_ATTACH_ISOLATED_RING_STEP", 260.0, 80.0, 1600.0);
  const int directions = static_cast<int>(
    readDoubleEnv("DJERD_RIGID_ATTACH_ISOLATED_DIRECTIONS", 16.0, 8.0, 48.0));
  const int minScore = static_cast<int>(std::round(
    readDoubleEnv("DJERD_ATTACH_ISOLATED_MIN_SCORE", 3.0, 0.0, 1000.0)));
  const bool checkRoutes =
    readBoolEnv("DJERD_ATTACH_ISOLATED_ROUTE_CHECK", false);
  const double bboxWeight =
    readDoubleEnv("DJERD_ATTACH_ISOLATED_BBOX_WEIGHT", 20.0, 0.0, 10000.0);
  const double radiusWeight =
    readDoubleEnv("DJERD_ATTACH_ISOLATED_RADIUS_WEIGHT", 1.0, 0.0, 10000.0);
  const double outwardWeight =
    readDoubleEnv("DJERD_ATTACH_ISOLATED_OUTWARD_WEIGHT", 35.0, 0.0, 10000.0);

  std::vector<std::vector<std::string>> tokens(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    tokens[i] = modelNameTokens(nodes[i].modelId);
  }

  auto appOf = [](const NodeRecord& node) {
    const std::size_t dot = node.modelId.find('.');
    if (dot != std::string::npos) {
      return node.modelId.substr(0, dot);
    }
    return node.appLabel;
  };

  auto tokenOverlap = [&](std::size_t a, std::size_t b) {
    const auto& left = tokens[a];
    const auto& right = tokens[b];
    std::size_t li = 0;
    std::size_t ri = 0;
    int overlap = 0;
    while (li < left.size() && ri < right.size()) {
      if (left[li] == right[ri]) {
        ++overlap;
        ++li;
        ++ri;
      } else if (left[li] < right[ri]) {
        ++li;
      } else {
        ++ri;
      }
    }
    return overlap;
  };

  const std::vector<std::vector<RoutePoint>> fixedRoutes =
    checkRoutes ? routeAllEdgesStraight(edges, attributes)
                : std::vector<std::vector<RoutePoint>>{};
  std::vector<Rect> occupied;
  occupied.reserve(nodes.size());
  for (std::size_t idx : connected) {
    occupied.push_back(nodeRect(nodes[idx], attributes, margin));
  }
  double connectedMinX = std::numeric_limits<double>::infinity();
  double connectedMinY = std::numeric_limits<double>::infinity();
  double connectedMaxX = -std::numeric_limits<double>::infinity();
  double connectedMaxY = -std::numeric_limits<double>::infinity();
  for (std::size_t idx : connected) {
    const Rect rect = nodeRect(nodes[idx], attributes, margin);
    connectedMinX = std::min(connectedMinX, rect.left);
    connectedMinY = std::min(connectedMinY, rect.top);
    connectedMaxX = std::max(connectedMaxX, rect.right);
    connectedMaxY = std::max(connectedMaxY, rect.bottom);
  }
  const double graphCenterX =
    std::isfinite(connectedMinX) ? (connectedMinX + connectedMaxX) * 0.5 : 0.0;
  const double graphCenterY =
    std::isfinite(connectedMinY) ? (connectedMinY + connectedMaxY) * 0.5 : 0.0;

  auto routeHitsRect = [&](const Rect& rect) {
    if (!checkRoutes) {
      return false;
    }
    for (const std::vector<RoutePoint>& route : fixedRoutes) {
      if (route.size() < 2) {
        continue;
      }
      for (std::size_t i = 1; i < route.size(); ++i) {
        if (segmentIntersectsRect(route[i - 1], route[i], rect)) {
          return true;
        }
      }
    }
    return false;
  };

  auto overlapsOccupied = [&](const Rect& rect) {
    for (const Rect& other : occupied) {
      if (rectsOverlap(rect, other)) {
        return true;
      }
    }
    return false;
  };

  auto rectAt = [&](const NodeRecord& node, double cx, double cy) {
    const double w = sanitizeNodeWidth(node, attributes);
    const double h = sanitizeNodeHeight(node, attributes);
    return Rect{
      cy + h / 2.0 + margin,
      cx - w / 2.0 - margin,
      cx + w / 2.0 + margin,
      cy - h / 2.0 - margin,
    };
  };

  std::sort(isolated.begin(), isolated.end(), [&](std::size_t left, std::size_t right) {
    return nodes[left].modelId < nodes[right].modelId;
  });

  constexpr double kPi = 3.14159265358979323846;
  std::size_t moved = 0;
  for (std::size_t idx : isolated) {
    int bestScore = std::numeric_limits<int>::min();
    std::size_t bestConnected = connected.front();
    const std::string isolatedApp = appOf(nodes[idx]);
    for (std::size_t candidate : connected) {
      int score = tokenOverlap(idx, candidate) * 10;
      if (!isolatedApp.empty() && isolatedApp == appOf(nodes[candidate])) {
        score += 2;
      }
    if (
        !tokens[idx].empty()
        && !tokens[candidate].empty()
        && tokens[idx].front() == tokens[candidate].front()) {
      score += 3;
      }
      if (score > bestScore) {
        bestScore = score;
        bestConnected = candidate;
      }
    }
    if (bestScore < minScore) {
      continue;
    }

    const NodeRecord& anchorNode = nodes[bestConnected];
    const double anchorX = sanitizeNodeCenterX(anchorNode, attributes);
    const double anchorY = sanitizeNodeCenterY(anchorNode, attributes);
    const double outwardAngle = std::atan2(anchorY - graphCenterY, anchorX - graphCenterX);
    const NodeRecord& node = nodes[idx];
    bool placed = false;
    double bestX = sanitizeNodeCenterX(node, attributes);
    double bestY = sanitizeNodeCenterY(node, attributes);
    double bestPlacementScore = std::numeric_limits<double>::infinity();

    auto angleDistance = [](double left, double right) {
      constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
      double diff = std::fmod(std::abs(left - right), kTwoPi);
      if (diff > 3.14159265358979323846) {
        diff = kTwoPi - diff;
      }
      return diff;
    };
    auto bboxOverflow = [&](const Rect& rect) {
      if (!std::isfinite(connectedMinX)) {
        return 0.0;
      }
      return
        std::max(0.0, connectedMinX - rect.left)
        + std::max(0.0, rect.right - connectedMaxX)
        + std::max(0.0, connectedMinY - rect.top)
        + std::max(0.0, rect.bottom - connectedMaxY);
    };

    for (double radius = ringStep; radius <= maxRadius; radius += ringStep) {
      for (int d = 0; d < directions; ++d) {
        const int step = (d + 1) / 2;
        const double sign = (d % 2 == 0) ? -1.0 : 1.0;
        const double angle = outwardAngle
          + sign * static_cast<double>(step) * (2.0 * kPi / static_cast<double>(directions));
        const double cx = anchorX + std::cos(angle) * radius;
        const double cy = anchorY + std::sin(angle) * radius;
        const Rect candidateRect = rectAt(node, cx, cy);
        if (overlapsOccupied(candidateRect)) {
          continue;
        }
        if (routeHitsRect(candidateRect)) {
          continue;
        }
        const double score =
          bboxOverflow(candidateRect) * bboxWeight
          + radius * radiusWeight
          + angleDistance(angle, outwardAngle) * outwardWeight;
        if (score + 1e-6 < bestPlacementScore) {
          bestPlacementScore = score;
          bestX = cx;
          bestY = cy;
          placed = true;
        }
      }
    }

    if (!placed) {
      continue;
    }
    attributes.x(node.handle) = bestX;
    attributes.y(node.handle) = bestY;
    occupied.push_back(rectAt(node, bestX, bestY));
    ++moved;
  }

  if (moved > 0) {
    std::fprintf(stderr,
      "[isolated-name-attach-final] attached %zu/%zu edge-less nodes by name tokens.\n",
      moved,
      isolated.size());
  }
  return moved;
}

LayoutQualityMetrics measureLayoutQuality(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::vector<LeafBundleRecord>* leafBundles,
  const std::unordered_map<std::string, std::string>* clusterByModelId) {
  LayoutQualityMetrics metrics;

  // Visual margin — node's "personal space" added to its 4-corner rect.
  // Per user spec: collision is judged on (rect + margin) area, not bare
  // rect. Edges entering this margin area count as a hit; nodes whose
  // margin areas overlap count as a node-node collision.
  const double kVisualMargin = visualNodeMargin();
  const double kLeafBundleMargin = leafBundleVisualMargin();

  // Per-bundle route-exempt set (parent + leaves) and bbox. Only leaf tables
  // are replaced by the synthetic table in the webview; the parent remains a
  // visible ordinary table and must participate in node/bundle clearance.
  std::vector<std::unordered_set<std::string>> bundleExempt;
  std::vector<Rect> bundleRects;
  // Set of modelIds visually replaced by ANY bundle. Parent ids deliberately
  // stay out of this set because the webview continues to draw them.
  std::unordered_set<std::string> bundleAbsorbed;
  if (leafBundles != nullptr) {
    bundleExempt.reserve(leafBundles->size());
    bundleRects.reserve(leafBundles->size());
    for (const LeafBundleRecord& bundle : *leafBundles) {
      std::unordered_set<std::string> exempt;
      exempt.insert(bundle.parentModelId);
      for (const std::string& leaf : bundle.leafModelIds) {
        exempt.insert(leaf);
        bundleAbsorbed.insert(leaf);
      }
      bundleExempt.push_back(std::move(exempt));
      bundleRects.push_back(renderedLeafBundleRect(bundle, kLeafBundleMargin));
    }
    // Bundle-vs-node overlap: count when bundle's margin-expanded bbox
    // overlaps an external node's margin-expanded rect.
    for (std::size_t bi = 0; bi < bundleRects.size(); ++bi) {
      const Rect& br = bundleRects[bi];
      for (const NodeRecord& nd : nodes) {
        if (bundleAbsorbed.count(nd.modelId)) continue;
        const Rect nr = nodeRect(nd, attributes, kVisualMargin);
        if (rectsOverlap(br, nr)) {
          metrics.bundleNodeOverlaps += 1;
        }
      }
    }
  }

  // Node-node rect overlaps — 4-corner rect-rect intersection on the
  // margin-expanded rectangles. Skip nodes absorbed by a bundle.
  {
    std::vector<std::pair<Rect, std::size_t>> rects;
    rects.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      if (bundleAbsorbed.count(nodes[i].modelId)) continue;
      rects.emplace_back(nodeRect(nodes[i], attributes, kVisualMargin), i);
    }
    std::sort(rects.begin(), rects.end(),
      [](const auto& a, const auto& b) { return a.first.left < b.first.left; });
    for (std::size_t i = 0; i < rects.size(); ++i) {
      for (std::size_t j = i + 1; j < rects.size(); ++j) {
        if (rects[j].first.left >= rects[i].first.right) break;
        if (rectsOverlap(rects[i].first, rects[j].first)) {
          metrics.nodeOverlaps += 1;
        }
      }
    }
  }
  // Clearance is measured over the exact table objects emitted by the
  // webview: non-leaf nodes (including bundle parents) plus synthetic bundle
  // tables. A zero violation count guarantees at least the standard 56px
  // horizontal or 42px vertical separation for every pair.
  const std::vector<LeafBundleRecord> noLeafBundles;
  const std::vector<LeafBundleRecord>& renderedBundles =
    leafBundles == nullptr ? noLeafBundles : *leafBundles;
  const RenderedNodeClearanceMetrics clearance =
    measureRenderedNodeClearance(nodes, attributes, renderedBundles);
  metrics.nodeSpacingOverlaps = clearance.violations;
  metrics.nodeClearanceMin = clearance.minimum;
  metrics.nodeClearanceTarget = kMinimumNodeClearance;

  const bool reuseQualityObstacles = [] {
    const char* value = std::getenv("DJERD_QUALITY_REUSE_OBSTACLES");
    return !value || std::strcmp(value, "0") != 0;
  }();
  std::vector<NodeObstacle> qualityObstacles;
  if (reuseQualityObstacles) {
    qualityObstacles.reserve(nodes.size());
    for (const NodeRecord& node : nodes) {
      qualityObstacles.push_back({
        node.handle,
        node.modelId,
        nodeRect(node, attributes, kVisualMargin),
      });
    }
  }

  RouteOccupancy occupancy;

  for (std::size_t edgeIndex = 0; edgeIndex < routes.size() && edgeIndex < edges.size(); ++edgeIndex) {
    const std::vector<RoutePoint>& route = routes[edgeIndex];
    if (route.size() < 2) {
      continue;
    }

    const LineIntent line = makeLineIntent(edges[edgeIndex], edgeIndex, attributes);
    // Margin-expanded obstacle rects: edge entering the margin area
    // counts as a collision.
    std::vector<NodeObstacle> edgeObstacles;
    const std::vector<NodeObstacle>* obstacles = &qualityObstacles;
    if (!reuseQualityObstacles) {
      edgeObstacles = makeNodeObstacles(
        nodes,
        attributes,
        kVisualMargin,
        line.sourceHandle,
        line.targetHandle);
      obstacles = &edgeObstacles;
    }
    const std::vector<LineSegment> segments = buildLineSegments(route, line.lineIndex, line.lineId);

    const std::string& srcId = edges[edgeIndex].sourceModelId;
    const std::string& tgtId = edges[edgeIndex].targetModelId;

    for (const LineSegment& segment : segments) {
      metrics.routeSegments += 1;

      for (const NodeObstacle& obstacle : *obstacles) {
        if (obstacle.handle == line.sourceHandle
            || obstacle.handle == line.targetHandle) {
          continue;
        }
        // Skip nodes absorbed by a leaf bundle — the bundle's own bbox
        // is checked separately. Counting absorbed leaves and the
        // bundle bbox would double-count the same visual block.
        if (bundleAbsorbed.count(obstacle.nodeId)) continue;
        if (segmentIntersectsRect(segment.start, segment.end, obstacle.rect)) {
          metrics.edgeNodeIntersections += 1;
        }
      }
      // Edge segments that pass through a leaf-bundle bbox (excluding
      // bundles whose parent OR any leaf the edge connects to).
      for (std::size_t bi = 0; bi < bundleRects.size(); ++bi) {
        if (bundleExempt[bi].count(srcId) || bundleExempt[bi].count(tgtId)) continue;
        if (segmentIntersectsRect(segment.start, segment.end, bundleRects[bi])) {
          metrics.bundleEdgeIntersections += 1;
        }
      }
    }

    recordRouteOccupancy(route, line, occupancy);
  }

  std::vector<bool> overlappingEdgeFlags(edges.size(), false);
  for (auto& entry : occupancy.horizontalSegmentsByLane) {
    metrics.edgeSegmentOverlaps += countAxisSegmentOverlaps(entry.second, overlappingEdgeFlags);
  }
  for (auto& entry : occupancy.verticalSegmentsByLane) {
    metrics.edgeSegmentOverlaps += countAxisSegmentOverlaps(entry.second, overlappingEdgeFlags);
  }

  metrics.overlappingEdges = static_cast<std::size_t>(
    std::count(overlappingEdgeFlags.begin(), overlappingEdgeFlags.end(), true));

  double minX = std::numeric_limits<double>::infinity();
  double minY = std::numeric_limits<double>::infinity();
  double maxX = -std::numeric_limits<double>::infinity();
  double maxY = -std::numeric_limits<double>::infinity();
  for (const NodeRecord& node : nodes) {
    const double centerX = sanitizeNodeCenterX(node, attributes);
    const double centerY = sanitizeNodeCenterY(node, attributes);
    const double width = sanitizeNodeWidth(node, attributes);
    const double height = sanitizeNodeHeight(node, attributes);
    const double left = centerX - width / 2.0;
    const double right = centerX + width / 2.0;
    const double top = centerY - height / 2.0;
    const double bottom = centerY + height / 2.0;
    if (left < minX) minX = left;
    if (right > maxX) maxX = right;
    if (top < minY) minY = top;
    if (bottom > maxY) maxY = bottom;
  }
  if (std::isfinite(minX) && std::isfinite(maxX) && std::isfinite(minY) && std::isfinite(maxY)
      && maxX > minX && maxY > minY) {
    const double width = maxX - minX;
    const double height = maxY - minY;
    metrics.boundingBoxArea = width * height;
    const double bigger = std::max(width, height);
    const double smaller = std::max(1.0, std::min(width, height));
    metrics.aspectRatio = bigger / smaller;

    // nodeAreaCoverage: Σ node area / bbox area. Bundle-absorbed nodes
    // are excluded since the bundle bbox already represents them.
    double nodeAreaSum = 0.0;
    for (const NodeRecord& node : nodes) {
      if (bundleAbsorbed.count(node.modelId)) continue;
      nodeAreaSum +=
        sanitizeNodeWidth(node, attributes)
        * sanitizeNodeHeight(node, attributes);
    }
    if (metrics.boundingBoxArea > 1e-6) {
      metrics.nodeAreaCoverage = nodeAreaSum / metrics.boundingBoxArea;
    }

    // emptySpaceCv: divide bbox into a fixed 16×16 grid and compute the
    // CV of per-cell occupancy (fraction of cell covered by some node
    // rect). 0 = perfectly uniform fill; high = clumpy distribution
    // with concentrated whitespace.
    constexpr std::size_t kGridN = 16;
    if (width > 1e-6 && height > 1e-6) {
      const double cellW = width / static_cast<double>(kGridN);
      const double cellH = height / static_cast<double>(kGridN);
      std::vector<double> cellOccupied(kGridN * kGridN, 0.0);
      const double cellArea = cellW * cellH;
      for (const NodeRecord& node : nodes) {
        if (bundleAbsorbed.count(node.modelId)) continue;
        const double cx = sanitizeNodeCenterX(node, attributes);
        const double cy = sanitizeNodeCenterY(node, attributes);
        const double nw = sanitizeNodeWidth(node, attributes);
        const double nh = sanitizeNodeHeight(node, attributes);
        const double nLeft = cx - nw / 2.0;
        const double nRight = cx + nw / 2.0;
        const double nTop = cy - nh / 2.0;
        const double nBottom = cy + nh / 2.0;
        const std::size_t gxLo = static_cast<std::size_t>(std::max(0.0,
          std::floor((nLeft - minX) / cellW)));
        const std::size_t gxHi = std::min(kGridN - 1,
          static_cast<std::size_t>(std::max(0.0,
            std::floor((nRight - minX) / cellW))));
        const std::size_t gyLo = static_cast<std::size_t>(std::max(0.0,
          std::floor((nTop - minY) / cellH)));
        const std::size_t gyHi = std::min(kGridN - 1,
          static_cast<std::size_t>(std::max(0.0,
            std::floor((nBottom - minY) / cellH))));
        for (std::size_t gy = gyLo; gy <= gyHi; ++gy) {
          const double cellTop = minY + static_cast<double>(gy) * cellH;
          const double cellBottom = cellTop + cellH;
          const double overlapTop = std::max(nTop, cellTop);
          const double overlapBottom = std::min(nBottom, cellBottom);
          const double overlapH = std::max(0.0, overlapBottom - overlapTop);
          if (overlapH <= 0.0) continue;
          for (std::size_t gx = gxLo; gx <= gxHi; ++gx) {
            const double cellLeft = minX + static_cast<double>(gx) * cellW;
            const double cellRight = cellLeft + cellW;
            const double overlapLeft = std::max(nLeft, cellLeft);
            const double overlapRight = std::min(nRight, cellRight);
            const double overlapW = std::max(0.0, overlapRight - overlapLeft);
            cellOccupied[gy * kGridN + gx] += overlapW * overlapH;
          }
        }
      }
      double occSum = 0.0;
      double occSumSq = 0.0;
      for (double occ : cellOccupied) {
        const double frac = std::min(1.0, occ / std::max(cellArea, 1e-9));
        occSum += frac;
        occSumSq += frac * frac;
      }
      const double cellCount = static_cast<double>(cellOccupied.size());
      const double occMean = occSum / cellCount;
      const double occVar = std::max(0.0, occSumSq / cellCount - occMean * occMean);
      const double occStd = std::sqrt(occVar);
      metrics.emptySpaceCv = occMean > 1e-6 ? occStd / occMean : 0.0;
    }
  }

  double lengthSum = 0.0;
  double lengthSumSq = 0.0;
  std::size_t lengthCount = 0;
  for (const std::vector<RoutePoint>& route : routes) {
    if (route.size() < 2) {
      continue;
    }
    double length = 0.0;
    for (std::size_t pointIndex = 1; pointIndex < route.size(); ++pointIndex) {
      const double dx = route[pointIndex].x - route[pointIndex - 1].x;
      const double dy = route[pointIndex].y - route[pointIndex - 1].y;
      length += std::sqrt(dx * dx + dy * dy);
    }
    lengthSum += length;
    lengthSumSq += length * length;
    lengthCount += 1;
  }
  if (lengthCount > 0) {
    const double count = static_cast<double>(lengthCount);
    const double mean = lengthSum / count;
    metrics.meanEdgeLength = mean;
    const double variance = std::max(0.0, (lengthSumSq / count) - mean * mean);
    metrics.edgeLengthStddev = std::sqrt(variance);
    // B. edge_length_cv = stddev / mean. Uniform edge lengths → ~0.
    metrics.edgeLengthCv = mean > 1e-6 ? metrics.edgeLengthStddev / mean : 0.0;
  }

  // D. edge_bend_total — sum across all edges of internal-waypoint angle
  // changes. Straight polyline = 0; heavily bent routing > 0.
  {
    double bendSum = 0.0;
    for (const std::vector<RoutePoint>& route : routes) {
      if (route.size() < 3) continue;
      for (std::size_t i = 1; i + 1 < route.size(); ++i) {
        const double ax = route[i].x - route[i - 1].x;
        const double ay = route[i].y - route[i - 1].y;
        const double bx = route[i + 1].x - route[i].x;
        const double by = route[i + 1].y - route[i].y;
        const double aLen = std::sqrt(ax * ax + ay * ay);
        const double bLen = std::sqrt(bx * bx + by * by);
        if (aLen < 1e-6 || bLen < 1e-6) continue;
        double cosA = (ax * bx + ay * by) / (aLen * bLen);
        if (cosA > 1.0) cosA = 1.0;
        if (cosA < -1.0) cosA = -1.0;
        // π - angle between successive segments. Straight = 0; back-turn = π.
        bendSum += std::acos(cosA);
      }
    }
    metrics.edgeBendTotal = bendSum;
  }

  // C. crossing_angle_dist + cross subdivisions — for every edge-edge
  // segment crossing record the acute angle, the per-edge participation
  // count, and whether both edges connect different louvain clusters.
  // 90° = best visual clarity; near 0° = visually confusing. Brute-
  // force segment-pair enumeration with bbox prefilter.
  {
    struct Seg { double x1, y1, x2, y2; double minX, minY, maxX, maxY; std::size_t edgeIdx; };
    std::vector<Seg> segs;
    segs.reserve(routes.size() * 2);
    for (std::size_t e = 0; e < routes.size(); ++e) {
      const std::vector<RoutePoint>& route = routes[e];
      if (route.size() < 2) continue;
      for (std::size_t i = 1; i < route.size(); ++i) {
        Seg s;
        s.x1 = route[i - 1].x; s.y1 = route[i - 1].y;
        s.x2 = route[i].x;     s.y2 = route[i].y;
        s.minX = std::min(s.x1, s.x2); s.maxX = std::max(s.x1, s.x2);
        s.minY = std::min(s.y1, s.y2); s.maxY = std::max(s.y1, s.y2);
        s.edgeIdx = e;
        segs.push_back(s);
      }
    }
    // Pre-compute "is edge between distinct clusters" once per edge.
    std::vector<bool> edgeIsCrossCluster(edges.size(), false);
    if (clusterByModelId != nullptr && !clusterByModelId->empty()) {
      for (std::size_t e = 0; e < edges.size(); ++e) {
        auto sIt = clusterByModelId->find(edges[e].sourceModelId);
        auto tIt = clusterByModelId->find(edges[e].targetModelId);
        if (sIt == clusterByModelId->end() || tIt == clusterByModelId->end()) {
          continue;
        }
        edgeIsCrossCluster[e] =
          !sIt->second.empty() && !tIt->second.empty()
          && sIt->second != tIt->second;
      }
    }
    double angleSum = 0.0;
    double angleSumSq = 0.0;
    std::size_t crossCount = 0;
    std::size_t crossClusterCrossings = 0;
    std::vector<std::size_t> perEdgeCrossings(edges.size(), 0);
    for (std::size_t i = 0; i < segs.size(); ++i) {
      for (std::size_t j = i + 1; j < segs.size(); ++j) {
        if (segs[i].edgeIdx == segs[j].edgeIdx) continue;
        // bbox prefilter
        if (segs[i].maxX < segs[j].minX || segs[j].maxX < segs[i].minX) continue;
        if (segs[i].maxY < segs[j].minY || segs[j].maxY < segs[i].minY) continue;
        RoutePoint isect;
        if (!properSegmentIntersection(
            RoutePoint{segs[i].x1, segs[i].y1},
            RoutePoint{segs[i].x2, segs[i].y2},
            RoutePoint{segs[j].x1, segs[j].y1},
            RoutePoint{segs[j].x2, segs[j].y2}, isect)) {
          continue;
        }
        const double ax = segs[i].x2 - segs[i].x1;
        const double ay = segs[i].y2 - segs[i].y1;
        const double bx = segs[j].x2 - segs[j].x1;
        const double by = segs[j].y2 - segs[j].y1;
        const double aLen = std::sqrt(ax * ax + ay * ay);
        const double bLen = std::sqrt(bx * bx + by * by);
        if (aLen < 1e-6 || bLen < 1e-6) continue;
        double cosA = std::abs((ax * bx + ay * by) / (aLen * bLen));
        if (cosA > 1.0) cosA = 1.0;
        // acute angle in [0, π/2]; cosA=0 → π/2 (perpendicular, best)
        const double ang = std::acos(cosA);
        angleSum += ang;
        angleSumSq += ang * ang;
        ++crossCount;
        // Per-edge participation. Each crossing increments both edges.
        if (segs[i].edgeIdx < perEdgeCrossings.size()) {
          ++perEdgeCrossings[segs[i].edgeIdx];
        }
        if (segs[j].edgeIdx < perEdgeCrossings.size()) {
          ++perEdgeCrossings[segs[j].edgeIdx];
        }
        // Cross-cluster bridge crossings: both edges go between distinct
        // clusters. This isolates the cluster-bridge tangle from
        // intra-cluster routing noise.
        if (segs[i].edgeIdx < edgeIsCrossCluster.size()
            && segs[j].edgeIdx < edgeIsCrossCluster.size()
            && edgeIsCrossCluster[segs[i].edgeIdx]
            && edgeIsCrossCluster[segs[j].edgeIdx]) {
          ++crossClusterCrossings;
        }
      }
    }
    if (crossCount > 0) {
      const double count = static_cast<double>(crossCount);
      const double mean = angleSum / count;
      metrics.crossingAngleMean = mean;
      const double variance = std::max(0.0, (angleSumSq / count) - mean * mean);
      const double stddev = std::sqrt(variance);
      metrics.crossingAngleCv = mean > 1e-6 ? stddev / mean : 0.0;
    }
    metrics.edgeCrossingsBetweenClusters = crossClusterCrossings;
    // Per-edge percentiles + clean-edge ratio.
    if (!perEdgeCrossings.empty()) {
      std::vector<std::size_t> sorted = perEdgeCrossings;
      std::sort(sorted.begin(), sorted.end());
      const std::size_t lastIdx = sorted.size() - 1;
      const std::size_t idx50 = static_cast<std::size_t>(
        std::floor(0.50 * static_cast<double>(lastIdx)));
      const std::size_t idx90 = static_cast<std::size_t>(
        std::floor(0.90 * static_cast<double>(lastIdx)));
      metrics.crossingsPerEdgeP50 = sorted[idx50];
      metrics.crossingsPerEdgeP90 = sorted[idx90];
      const std::size_t cleanCount = static_cast<std::size_t>(
        std::count(perEdgeCrossings.begin(), perEdgeCrossings.end(),
                   static_cast<std::size_t>(0)));
      metrics.cleanEdgeRatio =
        static_cast<double>(cleanCount)
        / static_cast<double>(perEdgeCrossings.size());

      // Top-N high-cross edges. Sort edge indices by perEdgeCrossings
      // descending and emit up to N records. N is env-tunable
      // (DJERD_TOP_CROSS_EDGES_N, default 20). Edges with zero
      // crossings are skipped — they aren't offenders.
      const char* topNEnv = std::getenv("DJERD_TOP_CROSS_EDGES_N");
      const std::size_t topN = topNEnv
        ? static_cast<std::size_t>(std::max(0, std::atoi(topNEnv)))
        : 20;
      if (topN > 0) {
        std::vector<std::size_t> order(perEdgeCrossings.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
          [&perEdgeCrossings](std::size_t a, std::size_t b) {
            return perEdgeCrossings[a] > perEdgeCrossings[b];
          });
        metrics.topCrossEdges.reserve(std::min(topN, order.size()));
        for (std::size_t k = 0; k < topN && k < order.size(); ++k) {
          const std::size_t ei = order[k];
          if (perEdgeCrossings[ei] == 0) break;
          if (ei >= edges.size()) continue;
          CrossingEdgeRecord rec;
          rec.sourceModelId = edges[ei].sourceModelId;
          rec.targetModelId = edges[ei].targetModelId;
          rec.crossings = perEdgeCrossings[ei];
          metrics.topCrossEdges.push_back(std::move(rec));
        }
      }
    }
  }

  // A. stress_score (Gansner et al. 2005). For each pair of nodes (i,j)
  // with a finite graph-theoretic distance d_ij (BFS hops in the
  // unweighted graph), stress_ij = w_ij · (||p_i - p_j|| - L · d_ij)²
  // with w_ij = 1/d_ij² and L = meanEdgeLength as the ideal-length
  // calibration. Sum is normalized by the number of contributing pairs
  // so the metric is scale-invariant across graphs of different sizes.
  // Lower = positions match graph-theoretic distances; canonical
  // force-directed layout quality metric.
  if (metrics.meanEdgeLength > 1e-6 && !nodes.empty()) {
    const double L = metrics.meanEdgeLength;
    std::unordered_map<std::string, std::size_t> nodeIdxByModelId;
    nodeIdxByModelId.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      nodeIdxByModelId[nodes[i].modelId] = i;
    }
    std::vector<std::vector<std::size_t>> adj(nodes.size());
    for (const EdgeRecord& e : edges) {
      auto sIt = nodeIdxByModelId.find(e.sourceModelId);
      auto tIt = nodeIdxByModelId.find(e.targetModelId);
      if (sIt == nodeIdxByModelId.end() || tIt == nodeIdxByModelId.end()) continue;
      if (sIt->second == tIt->second) continue;
      adj[sIt->second].push_back(tIt->second);
      adj[tIt->second].push_back(sIt->second);
    }
    constexpr std::uint16_t kUnreached = 0xFFFF;
    std::vector<std::uint16_t> dist(nodes.size(), kUnreached);
    std::vector<std::size_t> bfsQueue;
    bfsQueue.reserve(nodes.size());
    double stressAccum = 0.0;
    std::size_t pairCount = 0;
    for (std::size_t src = 0; src < nodes.size(); ++src) {
      std::fill(dist.begin(), dist.end(), kUnreached);
      dist[src] = 0;
      bfsQueue.clear();
      bfsQueue.push_back(src);
      for (std::size_t head = 0; head < bfsQueue.size(); ++head) {
        const std::size_t u = bfsQueue[head];
        const std::uint16_t du = dist[u];
        if (du == kUnreached) continue;
        for (std::size_t v : adj[u]) {
          if (dist[v] != kUnreached) continue;
          dist[v] = static_cast<std::uint16_t>(du + 1);
          bfsQueue.push_back(v);
        }
      }
      // Only count each unordered pair once (j > src).
      const double srcX = sanitizeNodeCenterX(nodes[src], attributes);
      const double srcY = sanitizeNodeCenterY(nodes[src], attributes);
      for (std::size_t j = src + 1; j < nodes.size(); ++j) {
        const std::uint16_t d = dist[j];
        if (d == kUnreached || d == 0) continue;
        const double djX = sanitizeNodeCenterX(nodes[j], attributes);
        const double djY = sanitizeNodeCenterY(nodes[j], attributes);
        const double dx = djX - srcX;
        const double dy = djY - srcY;
        const double euclid = std::sqrt(dx * dx + dy * dy);
        const double ideal = L * static_cast<double>(d);
        const double diff = euclid - ideal;
        const double w = 1.0 / (static_cast<double>(d) * static_cast<double>(d));
        stressAccum += w * diff * diff;
        ++pairCount;
      }
    }
    if (pairCount > 0) {
      // Normalize by pair count → average per-pair stress. Divide by L²
      // to make it dimensionless (length² → unitless), so different
      // graphs with different ideal-lengths are still comparable.
      metrics.stressScore =
        stressAccum / (static_cast<double>(pairCount) * L * L);
    }

    // E. hub_clearance_p10 — for top-decile-degree nodes, compute
    // distance to nearest non-incident node. Aggregate as the 10th
    // percentile across these hubs (low value = some hubs crowded).
    // Reuses the adjacency built for stress. Top decile by degree
    // is a statistical quantile, not a per-data threshold.
    if (nodes.size() >= 10) {
      std::vector<std::size_t> degOrder(nodes.size());
      for (std::size_t i = 0; i < nodes.size(); ++i) degOrder[i] = i;
      std::sort(degOrder.begin(), degOrder.end(),
        [&adj](std::size_t a, std::size_t b) {
          return adj[a].size() > adj[b].size();
        });
      const std::size_t hubCount =
        std::max<std::size_t>(1, nodes.size() / 10);
      std::vector<double> hubClearances;
      hubClearances.reserve(hubCount);
      for (std::size_t k = 0; k < hubCount; ++k) {
        const std::size_t h = degOrder[k];
        if (adj[h].empty()) continue;
        std::unordered_set<std::size_t> incident(adj[h].begin(), adj[h].end());
        incident.insert(h);
        const double hx = sanitizeNodeCenterX(nodes[h], attributes);
        const double hy = sanitizeNodeCenterY(nodes[h], attributes);
        double nearest = std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < nodes.size(); ++j) {
          if (incident.count(j)) continue;
          const double dx = sanitizeNodeCenterX(nodes[j], attributes) - hx;
          const double dy = sanitizeNodeCenterY(nodes[j], attributes) - hy;
          const double d = std::sqrt(dx * dx + dy * dy);
          if (d < nearest) nearest = d;
        }
        if (std::isfinite(nearest)) hubClearances.push_back(nearest);
      }
      if (!hubClearances.empty()) {
        std::sort(hubClearances.begin(), hubClearances.end());
        // 10th percentile via linear interpolation between bracketing
        // indices. With ≥10 hubs gives the lower-tail clearance.
        const double rank = 0.10 * static_cast<double>(hubClearances.size() - 1);
        const std::size_t lo = static_cast<std::size_t>(std::floor(rank));
        const std::size_t hi = std::min(lo + 1, hubClearances.size() - 1);
        const double frac = rank - static_cast<double>(lo);
        metrics.hubClearanceP10 =
          hubClearances[lo] * (1.0 - frac) + hubClearances[hi] * frac;
      }
    }
  }

  // F. cluster_compactness_mean — for each cluster (≥2 members), compute
  // (cluster bbox area) / (Σ member node area). 1.0 = perfectly packed;
  // >1 = bbox bigger than node footprint (sparse); <1 cannot happen
  // since bbox encloses all member rects. Take mean across clusters.
  // Skipped when clusterByModelId is not provided.
  if (clusterByModelId != nullptr && !clusterByModelId->empty()) {
    std::unordered_map<std::string, std::vector<std::size_t>> membersBy;
    std::unordered_map<std::string, std::size_t> idxByModelId;
    idxByModelId.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) idxByModelId[nodes[i].modelId] = i;
    for (const auto& kv : *clusterByModelId) {
      auto it = idxByModelId.find(kv.first);
      if (it == idxByModelId.end()) continue;
      membersBy[kv.second].push_back(it->second);
    }
    double compactSum = 0.0;
    std::size_t compactCount = 0;
    for (const auto& kv : membersBy) {
      const std::vector<std::size_t>& members = kv.second;
      if (members.size() < 2) continue;
      double minX = std::numeric_limits<double>::infinity();
      double minY = std::numeric_limits<double>::infinity();
      double maxX = -std::numeric_limits<double>::infinity();
      double maxY = -std::numeric_limits<double>::infinity();
      double nodeAreaSum = 0.0;
      for (std::size_t i : members) {
        const double cx = sanitizeNodeCenterX(nodes[i], attributes);
        const double cy = sanitizeNodeCenterY(nodes[i], attributes);
        const double w = sanitizeNodeWidth(nodes[i], attributes);
        const double h = sanitizeNodeHeight(nodes[i], attributes);
        if (cx - w / 2.0 < minX) minX = cx - w / 2.0;
        if (cx + w / 2.0 > maxX) maxX = cx + w / 2.0;
        if (cy - h / 2.0 < minY) minY = cy - h / 2.0;
        if (cy + h / 2.0 > maxY) maxY = cy + h / 2.0;
        nodeAreaSum += w * h;
      }
      const double bboxArea = (maxX - minX) * (maxY - minY);
      if (nodeAreaSum < 1e-6 || bboxArea < 1e-6) continue;
      compactSum += bboxArea / nodeAreaSum;
      ++compactCount;
    }
    if (compactCount > 0) {
      metrics.clusterCompactnessMean =
        compactSum / static_cast<double>(compactCount);
    }
  }

  // Composite quality score. Weighted sum of 7 normalized sub-scores
  // (each ∈ [0, 1] with 1 = best). Weights from Bennett et al. 2007
  // aesthetic ranking: crossings ≈ 45% (clean + severity), crossing
  // angle 20%, stress 15%, compactness/uniformity 20%. All sub-scores
  // are exposed individually so downstream tooling can re-weight.
  {
    constexpr double kPi = 3.14159265358979;
    metrics.subCleanQuality = metrics.cleanEdgeRatio;
    metrics.subSeverityQuality =
      1.0 / (1.0 + static_cast<double>(metrics.crossingsPerEdgeP90) * 0.1);
    metrics.subAngleQuality = std::min(1.0,
      metrics.crossingAngleMean / (kPi / 2.0));
    metrics.subStressQuality = 1.0 / (1.0 + metrics.stressScore);
    metrics.subCompactQuality = std::min(1.0, metrics.nodeAreaCoverage * 5.0);
    metrics.subUniformQuality = 1.0 / (1.0 + metrics.edgeLengthCv);
    metrics.subSpreadQuality = 1.0 / (1.0 + metrics.emptySpaceCv * 0.5);
    metrics.compositeQuality =
        0.30 * metrics.subCleanQuality
      + 0.15 * metrics.subSeverityQuality
      + 0.20 * metrics.subAngleQuality
      + 0.15 * metrics.subStressQuality
      + 0.10 * metrics.subCompactQuality
      + 0.05 * metrics.subUniformQuality
      + 0.05 * metrics.subSpreadQuality;
  }

  // Unified visual crossing count per user spec: strict 4-corner rect-
  // segment and rect-rect tests only. nodeSpacingOverlaps and any
  // padding-based "near miss" detections are NOT included.
  metrics.visualCrossings =
    metrics.edgeCrossings
    + metrics.edgeNodeIntersections
    + metrics.nodeOverlaps
    + metrics.bundleEdgeIntersections
    + metrics.bundleNodeOverlaps;

  return metrics;
}

}  // namespace djerd
