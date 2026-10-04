#include "layoutPipeline.h"
#include "layoutAlgorithms.h"

namespace djerd {

void enforceNodeSeparationStrong(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  for (int attempt = 0; attempt < 4; ++attempt) {
    enforceNodeSeparation(nodes, attributes);
    if (
      countNodeRectOverlaps(nodes, attributes, false) == 0
      && countNodeRectOverlaps(nodes, attributes, true) == 0) {
      return;
    }
    placeNodesWithoutOverlaps(nodes, attributes);
  }
}

void addLaneValue(std::vector<double>& lanes, double value) {
  if (!isFiniteCoordinate(value)) {
    return;
  }
  lanes.push_back(value);
}

void ensureLaneValue(std::vector<double>& lanes, double value) {
  if (!isFiniteCoordinate(value)) {
    return;
  }

  const bool exists = std::any_of(
    lanes.begin(),
    lanes.end(),
    [=](double existing) {
      return std::abs(existing - value) < 0.01;
    });
  if (!exists) {
    lanes.insert(lanes.begin(), value);
  }
}

std::vector<double> nearestUniqueLaneValues(
  std::vector<double> lanes,
  double reference,
  std::size_t limit) {
  std::sort(
    lanes.begin(),
    lanes.end(),
    [=](double left, double right) {
      const double leftDistance = std::abs(left - reference);
      const double rightDistance = std::abs(right - reference);
      if (std::abs(leftDistance - rightDistance) > 0.01) {
        return leftDistance < rightDistance;
      }
      return left < right;
    });

  std::vector<double> selected;
  selected.reserve(std::min(limit, lanes.size()));
  for (double lane : lanes) {
    const bool duplicate = std::any_of(
      selected.begin(),
      selected.end(),
      [=](double existing) {
        return std::abs(existing - lane) < 28.0;
      });
    if (duplicate) {
      continue;
    }

    selected.push_back(lane);
    if (selected.size() >= limit) {
      break;
    }
  }

  return selected;
}

double normalizeLaneValue(double value) {
  return std::round(value * 100.0) / 100.0;
}

void addRequiredLane(std::vector<double>& lanes, double value) {
  if (!isFiniteCoordinate(value)) {
    return;
  }
  lanes.push_back(normalizeLaneValue(value));
}

double distanceToClosestAnchor(double value, const std::vector<double>& anchors) {
  double distance = std::numeric_limits<double>::infinity();
  for (double anchor : anchors) {
    distance = std::min(distance, std::abs(value - anchor));
  }
  return distance;
}

std::vector<double> selectVisibilityLanes(
  std::vector<double> required,
  std::vector<double> candidates,
  const std::vector<double>& anchors,
  std::size_t limit) {
  std::vector<double> lanes;
  lanes.reserve(limit);

  for (double lane : required) {
    addRequiredLane(lanes, lane);
  }

  std::sort(lanes.begin(), lanes.end());
  lanes.erase(
    std::unique(
      lanes.begin(),
      lanes.end(),
      [](double left, double right) {
        return std::abs(left - right) < 0.01;
      }),
    lanes.end());

  std::vector<double> normalizedCandidates;
  normalizedCandidates.reserve(candidates.size());
  for (double candidate : candidates) {
    if (isFiniteCoordinate(candidate)) {
      normalizedCandidates.push_back(normalizeLaneValue(candidate));
    }
  }
  std::sort(normalizedCandidates.begin(), normalizedCandidates.end());
  normalizedCandidates.erase(
    std::unique(
      normalizedCandidates.begin(),
      normalizedCandidates.end(),
      [](double left, double right) {
        return std::abs(left - right) < 0.01;
      }),
    normalizedCandidates.end());

  std::sort(
    normalizedCandidates.begin(),
    normalizedCandidates.end(),
    [&](double left, double right) {
      const double leftDistance = distanceToClosestAnchor(left, anchors);
      const double rightDistance = distanceToClosestAnchor(right, anchors);
      if (std::abs(leftDistance - rightDistance) > 0.01) {
        return leftDistance < rightDistance;
      }
      return left < right;
    });

  for (double candidate : normalizedCandidates) {
    if (lanes.size() >= limit) {
      break;
    }

    const bool exists = std::any_of(
      lanes.begin(),
      lanes.end(),
      [=](double existing) {
        return std::abs(existing - candidate) < 0.01;
      });
    if (!exists) {
      lanes.push_back(candidate);
    }
  }

  std::sort(lanes.begin(), lanes.end());
  return lanes;
}

int findLaneIndex(const std::vector<double>& lanes, double value) {
  const double normalized = normalizeLaneValue(value);
  for (std::size_t index = 0; index < lanes.size(); ++index) {
    if (std::abs(lanes[index] - normalized) < 0.01) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool pointInsideRect(const RoutePoint& point, const Rect& rect) {
  return point.x > rect.left
    && point.x < rect.right
    && point.y > rect.top
    && point.y < rect.bottom;
}

bool pointInsideAnyRect(const RoutePoint& point, const std::vector<Rect>& obstacles) {
  return std::any_of(
    obstacles.begin(),
    obstacles.end(),
    [&](const Rect& rect) {
      return pointInsideRect(point, rect);
    });
}

std::vector<std::pair<double, double>> blockedIntervalsForHorizontalLane(
  double y,
  const std::vector<Rect>& obstacles) {
  std::vector<std::pair<double, double>> intervals;
  for (const Rect& obstacle : obstacles) {
    if (y > obstacle.top && y < obstacle.bottom) {
      intervals.emplace_back(obstacle.left, obstacle.right);
    }
  }
  std::sort(intervals.begin(), intervals.end());
  return intervals;
}

std::vector<std::pair<double, double>> blockedIntervalsForVerticalLane(
  double x,
  const std::vector<Rect>& obstacles) {
  std::vector<std::pair<double, double>> intervals;
  for (const Rect& obstacle : obstacles) {
    if (x > obstacle.left && x < obstacle.right) {
      intervals.emplace_back(obstacle.top, obstacle.bottom);
    }
  }
  std::sort(intervals.begin(), intervals.end());
  return intervals;
}

bool intervalIntersectsAnyBlocked(
  double start,
  double end,
  const std::vector<std::pair<double, double>>& blockedIntervals) {
  const double minValue = std::min(start, end);
  const double maxValue = std::max(start, end);
  for (const auto& blocked : blockedIntervals) {
    if (blocked.first >= maxValue) {
      break;
    }
    if (blocked.second > minValue && blocked.first < maxValue) {
      return true;
    }
  }
  return false;
}

VisibilityRoute routeVisibilityGrid(
  const RoutePoint& start,
  const RoutePoint& end,
  const Rect& graphBounds,
  const std::vector<Rect>& obstacles,
  double laneOffset) {
  constexpr std::size_t maxVisibilityLanes = 64;
  constexpr double outerGap = 220.0;

  std::vector<double> requiredX;
  std::vector<double> requiredY;
  std::vector<double> candidateX;
  std::vector<double> candidateY;
  addRequiredLane(requiredX, start.x);
  addRequiredLane(requiredX, end.x);
  addRequiredLane(requiredX, graphBounds.left - outerGap - std::abs(laneOffset));
  addRequiredLane(requiredX, graphBounds.right + outerGap + std::abs(laneOffset));
  addRequiredLane(requiredY, start.y);
  addRequiredLane(requiredY, end.y);
  addRequiredLane(requiredY, graphBounds.top - outerGap - std::abs(laneOffset));
  addRequiredLane(requiredY, graphBounds.bottom + outerGap + std::abs(laneOffset));

  for (const Rect& obstacle : obstacles) {
    addRequiredLane(candidateX, obstacle.left - kVisibilityLaneClearance);
    addRequiredLane(candidateX, obstacle.right + kVisibilityLaneClearance);
    addRequiredLane(candidateY, obstacle.top - kVisibilityLaneClearance);
    addRequiredLane(candidateY, obstacle.bottom + kVisibilityLaneClearance);
  }

  const double midX = (start.x + end.x) / 2.0;
  const double midY = (start.y + end.y) / 2.0;
  const std::vector<double> xAnchors = { start.x, end.x, midX };
  const std::vector<double> yAnchors = { start.y, end.y, midY };
  const std::vector<double> xLanes =
    selectVisibilityLanes(std::move(requiredX), std::move(candidateX), xAnchors, maxVisibilityLanes);
  const std::vector<double> yLanes =
    selectVisibilityLanes(std::move(requiredY), std::move(candidateY), yAnchors, maxVisibilityLanes);
  const int startX = findLaneIndex(xLanes, start.x);
  const int startY = findLaneIndex(yLanes, start.y);
  const int endX = findLaneIndex(xLanes, end.x);
  const int endY = findLaneIndex(yLanes, end.y);

  if (startX < 0 || startY < 0 || endX < 0 || endY < 0) {
    return {};
  }

  const std::size_t width = xLanes.size();
  const std::size_t height = yLanes.size();
  const std::size_t total = width * height;
  const auto nodeIndex = [=](std::size_t x, std::size_t y) {
    return y * width + x;
  };
  const std::size_t startNode = nodeIndex(static_cast<std::size_t>(startX), static_cast<std::size_t>(startY));
  const std::size_t endNode = nodeIndex(static_cast<std::size_t>(endX), static_cast<std::size_t>(endY));

  std::vector<bool> blockedPoint(total, false);
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width; ++x) {
      const std::size_t index = nodeIndex(x, y);
      if (index == startNode || index == endNode) {
        continue;
      }
      blockedPoint[index] = pointInsideAnyRect({ xLanes[x], yLanes[y] }, obstacles);
    }
  }

  std::vector<std::vector<std::pair<double, double>>> horizontalBlocked;
  horizontalBlocked.reserve(height);
  for (double y : yLanes) {
    horizontalBlocked.push_back(blockedIntervalsForHorizontalLane(y, obstacles));
  }
  std::vector<std::vector<std::pair<double, double>>> verticalBlocked;
  verticalBlocked.reserve(width);
  for (double x : xLanes) {
    verticalBlocked.push_back(blockedIntervalsForVerticalLane(x, obstacles));
  }

  std::vector<double> distance(total, std::numeric_limits<double>::infinity());
  std::vector<std::size_t> previous(total, total);
  using QueueEntry = std::pair<double, std::size_t>;
  std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> pending;

  distance[startNode] = 0.0;
  pending.emplace(0.0, startNode);

  while (!pending.empty()) {
    const auto [cost, current] = pending.top();
    pending.pop();
    if (cost > distance[current] + 0.01) {
      continue;
    }
    if (current == endNode) {
      break;
    }

    const std::size_t x = current % width;
    const std::size_t y = current / width;
    const auto visit = [&](std::size_t nextX, std::size_t nextY, bool horizontal) {
      const std::size_t next = nodeIndex(nextX, nextY);
      if (blockedPoint[next]) {
        return;
      }

      const bool blocked = horizontal
        ? intervalIntersectsAnyBlocked(xLanes[x], xLanes[nextX], horizontalBlocked[y])
        : intervalIntersectsAnyBlocked(yLanes[y], yLanes[nextY], verticalBlocked[x]);
      if (blocked) {
        return;
      }

      const double stepCost = horizontal
        ? std::abs(xLanes[x] - xLanes[nextX])
        : std::abs(yLanes[y] - yLanes[nextY]);
      const double nextCost = cost + stepCost;
      if (nextCost + 0.01 >= distance[next]) {
        return;
      }

      distance[next] = nextCost;
      previous[next] = current;
      pending.emplace(nextCost, next);
    };

    if (x > 0) {
      visit(x - 1, y, true);
    }
    if (x + 1 < width) {
      visit(x + 1, y, true);
    }
    if (y > 0) {
      visit(x, y - 1, false);
    }
    if (y + 1 < height) {
      visit(x, y + 1, false);
    }
  }

  if (!std::isfinite(distance[endNode])) {
    return {};
  }

  std::vector<RoutePoint> reversedPoints;
  for (std::size_t current = endNode; current != total; current = previous[current]) {
    const std::size_t x = current % width;
    const std::size_t y = current / width;
    reversedPoints.push_back({ xLanes[x], yLanes[y] });
    if (current == startNode) {
      break;
    }
  }

  std::reverse(reversedPoints.begin(), reversedPoints.end());
  return { true, compressRoutePoints(std::move(reversedPoints)) };
}

VisibilityRoute routeVisibilityGridWithPorts(
  const std::vector<VisibilityPort>& sourcePorts,
  const std::vector<VisibilityPort>& targetPorts,
  const Rect& graphBounds,
  const std::vector<Rect>& obstacles,
  double laneOffset,
  const RouteOccupancy* occupancy) {
  constexpr std::size_t maxVisibilityLanes = 96;
  constexpr double outerGap = 220.0;

  if (sourcePorts.empty() || targetPorts.empty()) {
    return {};
  }

  std::vector<double> requiredX;
  std::vector<double> requiredY;
  std::vector<double> candidateX;
  std::vector<double> candidateY;
  std::vector<double> xAnchors;
  std::vector<double> yAnchors;

  addRequiredLane(requiredX, graphBounds.left - outerGap - std::abs(laneOffset));
  addRequiredLane(requiredX, graphBounds.right + outerGap + std::abs(laneOffset));
  addRequiredLane(requiredY, graphBounds.top - outerGap - std::abs(laneOffset));
  addRequiredLane(requiredY, graphBounds.bottom + outerGap + std::abs(laneOffset));

  for (const VisibilityPort& port : sourcePorts) {
    addRequiredLane(requiredX, port.stub.x);
    addRequiredLane(requiredY, port.stub.y);
    xAnchors.push_back(port.stub.x);
    yAnchors.push_back(port.stub.y);
  }
  for (const VisibilityPort& port : targetPorts) {
    addRequiredLane(requiredX, port.stub.x);
    addRequiredLane(requiredY, port.stub.y);
    xAnchors.push_back(port.stub.x);
    yAnchors.push_back(port.stub.y);
  }

  for (const Rect& obstacle : obstacles) {
    addRequiredLane(candidateX, obstacle.left - kVisibilityLaneClearance);
    addRequiredLane(candidateX, obstacle.right + kVisibilityLaneClearance);
    addRequiredLane(candidateY, obstacle.top - kVisibilityLaneClearance);
    addRequiredLane(candidateY, obstacle.bottom + kVisibilityLaneClearance);
  }

  const std::vector<double> xLanes =
    selectVisibilityLanes(std::move(requiredX), std::move(candidateX), xAnchors, maxVisibilityLanes);
  const std::vector<double> yLanes =
    selectVisibilityLanes(std::move(requiredY), std::move(candidateY), yAnchors, maxVisibilityLanes);
  const std::size_t width = xLanes.size();
  const std::size_t height = yLanes.size();
  const std::size_t total = width * height;
  const auto nodeIndex = [=](std::size_t x, std::size_t y) {
    return y * width + x;
  };

  std::vector<bool> blockedPoint(total, false);
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width; ++x) {
      blockedPoint[nodeIndex(x, y)] = pointInsideAnyRect({ xLanes[x], yLanes[y] }, obstacles);
    }
  }

  std::vector<std::vector<std::pair<double, double>>> horizontalBlocked;
  horizontalBlocked.reserve(height);
  for (double y : yLanes) {
    horizontalBlocked.push_back(blockedIntervalsForHorizontalLane(y, obstacles));
  }
  std::vector<std::vector<std::pair<double, double>>> verticalBlocked;
  verticalBlocked.reserve(width);
  for (double x : xLanes) {
    verticalBlocked.push_back(blockedIntervalsForVerticalLane(x, obstacles));
  }

  std::vector<int> sourcePortByNode(total, -1);
  std::vector<int> targetPortByNode(total, -1);
  std::vector<double> distance(total, std::numeric_limits<double>::infinity());
  std::vector<std::size_t> previous(total, total);
  using QueueEntry = std::pair<double, std::size_t>;
  std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> pending;

  for (std::size_t portIndex = 0; portIndex < sourcePorts.size(); ++portIndex) {
    const int x = findLaneIndex(xLanes, sourcePorts[portIndex].stub.x);
    const int y = findLaneIndex(yLanes, sourcePorts[portIndex].stub.y);
    if (x < 0 || y < 0) {
      continue;
    }
    const std::size_t index = nodeIndex(static_cast<std::size_t>(x), static_cast<std::size_t>(y));
    blockedPoint[index] = false;
    if (distance[index] <= 0.0) {
      continue;
    }
    distance[index] = 0.0;
    sourcePortByNode[index] = static_cast<int>(portIndex);
    pending.emplace(0.0, index);
  }

  for (std::size_t portIndex = 0; portIndex < targetPorts.size(); ++portIndex) {
    const int x = findLaneIndex(xLanes, targetPorts[portIndex].stub.x);
    const int y = findLaneIndex(yLanes, targetPorts[portIndex].stub.y);
    if (x < 0 || y < 0) {
      continue;
    }
    const std::size_t index = nodeIndex(static_cast<std::size_t>(x), static_cast<std::size_t>(y));
    blockedPoint[index] = false;
    targetPortByNode[index] = static_cast<int>(portIndex);
  }

  std::size_t bestEndNode = total;
  while (!pending.empty()) {
    const auto [cost, current] = pending.top();
    pending.pop();
    if (cost > distance[current] + 0.01) {
      continue;
    }
    if (targetPortByNode[current] >= 0) {
      bestEndNode = current;
      break;
    }

    const std::size_t x = current % width;
    const std::size_t y = current / width;
    const auto visit = [&](std::size_t nextX, std::size_t nextY, bool horizontal) {
      const std::size_t next = nodeIndex(nextX, nextY);
      if (blockedPoint[next]) {
        return;
      }

      const bool blocked = horizontal
        ? intervalIntersectsAnyBlocked(xLanes[x], xLanes[nextX], horizontalBlocked[y])
        : intervalIntersectsAnyBlocked(yLanes[y], yLanes[nextY], verticalBlocked[x]);
      if (blocked) {
        return;
      }

      const double stepCost = horizontal
        ? std::abs(xLanes[x] - xLanes[nextX])
        : std::abs(yLanes[y] - yLanes[nextY]);
      const double occupancyCost = horizontal
        ? occupancyCostForAxisSegment(
            occupancy,
            true,
            metricLaneKey(yLanes[y]),
            xLanes[x],
            xLanes[nextX])
        : occupancyCostForAxisSegment(
            occupancy,
            false,
            metricLaneKey(xLanes[x]),
            yLanes[y],
            yLanes[nextY]);
      const double nextCost = cost + stepCost + occupancyCost;
      if (nextCost + 0.01 >= distance[next]) {
        return;
      }

      distance[next] = nextCost;
      previous[next] = current;
      sourcePortByNode[next] = sourcePortByNode[current];
      pending.emplace(nextCost, next);
    };

    if (x > 0) {
      visit(x - 1, y, true);
    }
    if (x + 1 < width) {
      visit(x + 1, y, true);
    }
    if (y > 0) {
      visit(x, y - 1, false);
    }
    if (y + 1 < height) {
      visit(x, y + 1, false);
    }
  }

  if (bestEndNode == total || sourcePortByNode[bestEndNode] < 0 || targetPortByNode[bestEndNode] < 0) {
    return {};
  }

  std::vector<RoutePoint> reversedPoints;
  for (std::size_t current = bestEndNode; current != total; current = previous[current]) {
    const std::size_t x = current % width;
    const std::size_t y = current / width;
    reversedPoints.push_back({ xLanes[x], yLanes[y] });
    if (previous[current] == total) {
      break;
    }
  }

  std::reverse(reversedPoints.begin(), reversedPoints.end());
  return {
    true,
    compressRoutePoints(std::move(reversedPoints)),
    static_cast<std::size_t>(sourcePortByNode[bestEndNode]),
    static_cast<std::size_t>(targetPortByNode[bestEndNode]),
  };
}

VisibilityPort makeVisibilityPort(
  const Rect& rect,
  const std::string& side,
  double offset,
  double inset,
  double stub) {
  if (side == "left") {
    const double y = clampToSpan(rectCenterY(rect) + offset, rect.top + inset, rect.bottom - inset);
    return { { rect.left, y }, { rect.left - stub, y } };
  }
  if (side == "right") {
    const double y = clampToSpan(rectCenterY(rect) + offset, rect.top + inset, rect.bottom - inset);
    return { { rect.right, y }, { rect.right + stub, y } };
  }
  if (side == "top") {
    const double x = clampToSpan(rectCenterX(rect) + offset, rect.left + inset, rect.right - inset);
    return { { x, rect.top }, { x, rect.top - stub } };
  }

  const double x = clampToSpan(rectCenterX(rect) + offset, rect.left + inset, rect.right - inset);
  return { { x, rect.bottom }, { x, rect.bottom + stub } };
}

std::vector<VisibilityPort> makeVisibilityPorts(
  const Rect& rect,
  double offset,
  double inset,
  double stub) {
  return {
    makeVisibilityPort(rect, "left", offset, inset, stub),
    makeVisibilityPort(rect, "right", offset, inset, stub),
    makeVisibilityPort(rect, "top", offset, inset, stub),
    makeVisibilityPort(rect, "bottom", offset, inset, stub),
  };
}

std::vector<RoutePoint> routeObstacleAwareLine(
  const LineIntent& line,
  const Rect& graphBounds,
  const std::vector<NodeObstacle>& obstacles,
  const RouteOccupancy* occupancy) {
  const Rect source = line.sourceRect;
  const Rect target = line.targetRect;
  const bool horizontal = line.prefersHorizontal;
  const double laneOffset = line.laneOffset;
  constexpr double portInset = 18.0;
  constexpr double stub = 52.0;
  constexpr double outerGap = 170.0;
  constexpr double laneGap = kVisibilityLaneClearance;

  RoutePoint start;
  RoutePoint end;
  RoutePoint startStub;
  RoutePoint endStub;
  std::vector<std::vector<RoutePoint>> candidates;
  const std::vector<Rect> obstacleRects = collectObstacleRects(obstacles);

  if (horizontal) {
    const bool leftToRight = rectCenterX(target) >= rectCenterX(source);
    start = {
      leftToRight ? source.right : source.left,
      clampToSpan(rectCenterY(source) + laneOffset, source.top + portInset, source.bottom - portInset),
    };
    end = {
      leftToRight ? target.left : target.right,
      clampToSpan(rectCenterY(target) - laneOffset, target.top + portInset, target.bottom - portInset),
    };
    startStub = { start.x + (leftToRight ? stub : -stub), start.y };
    endStub = { end.x + (leftToRight ? -stub : stub), end.y };
    const double midX = (startStub.x + endStub.x) / 2.0;
    const double topLane = graphBounds.top - outerGap - std::abs(laneOffset);
    const double bottomLane = graphBounds.bottom + outerGap + std::abs(laneOffset);
    candidates.push_back({ start, startStub, { midX, startStub.y }, { midX, endStub.y }, endStub, end });

    std::vector<double> verticalLanes;
    std::vector<double> horizontalLanes;
    const double minX = std::min(startStub.x, endStub.x);
    const double maxX = std::max(startStub.x, endStub.x);
    const double minY = std::min(startStub.y, endStub.y);
    const double maxY = std::max(startStub.y, endStub.y);
    for (const NodeObstacle& obstacle : obstacles) {
      if (obstacle.rect.bottom >= minY - laneGap && obstacle.rect.top <= maxY + laneGap) {
        addLaneValue(verticalLanes, obstacle.rect.left - laneGap);
        addLaneValue(verticalLanes, obstacle.rect.right + laneGap);
      }
      if (obstacle.rect.right >= minX - laneGap && obstacle.rect.left <= maxX + laneGap) {
        addLaneValue(horizontalLanes, obstacle.rect.top - laneGap);
        addLaneValue(horizontalLanes, obstacle.rect.bottom + laneGap);
      }
    }

    for (double lane : nearestUniqueLaneValues(verticalLanes, midX, 8)) {
      candidates.push_back({ start, startStub, { lane, startStub.y }, { lane, endStub.y }, endStub, end });
    }
    for (double lane : nearestUniqueLaneValues(horizontalLanes, (startStub.y + endStub.y) / 2.0, 12)) {
      candidates.push_back({ start, startStub, { startStub.x, lane }, { endStub.x, lane }, endStub, end });
    }

    std::vector<double> sourceLanes = nearestUniqueLaneValues(verticalLanes, startStub.x, 4);
    std::vector<double> targetLanes = nearestUniqueLaneValues(verticalLanes, endStub.x, 4);
    std::vector<double> bridgeLanes = nearestUniqueLaneValues(
      horizontalLanes,
      (startStub.y + endStub.y) / 2.0,
      8);
    ensureLaneValue(sourceLanes, startStub.x);
    ensureLaneValue(targetLanes, endStub.x);
    ensureLaneValue(bridgeLanes, topLane);
    ensureLaneValue(bridgeLanes, bottomLane);
    for (double sourceLane : sourceLanes) {
      for (double targetLane : targetLanes) {
        for (double bridgeLane : bridgeLanes) {
          candidates.push_back({
            start,
            startStub,
            { sourceLane, startStub.y },
            { sourceLane, bridgeLane },
            { targetLane, bridgeLane },
            { targetLane, endStub.y },
            endStub,
            end,
          });
        }
      }
    }

    candidates.push_back({ start, startStub, { startStub.x, topLane }, { endStub.x, topLane }, endStub, end });
    candidates.push_back({ start, startStub, { startStub.x, bottomLane }, { endStub.x, bottomLane }, endStub, end });
  } else {
    const bool topToBottom = rectCenterY(target) >= rectCenterY(source);
    start = {
      clampToSpan(rectCenterX(source) + laneOffset, source.left + portInset, source.right - portInset),
      topToBottom ? source.bottom : source.top,
    };
    end = {
      clampToSpan(rectCenterX(target) - laneOffset, target.left + portInset, target.right - portInset),
      topToBottom ? target.top : target.bottom,
    };
    startStub = { start.x, start.y + (topToBottom ? stub : -stub) };
    endStub = { end.x, end.y + (topToBottom ? -stub : stub) };
    const double midY = (startStub.y + endStub.y) / 2.0;
    const double leftLane = graphBounds.left - outerGap - std::abs(laneOffset);
    const double rightLane = graphBounds.right + outerGap + std::abs(laneOffset);
    candidates.push_back({ start, startStub, { startStub.x, midY }, { endStub.x, midY }, endStub, end });

    std::vector<double> verticalLanes;
    std::vector<double> horizontalLanes;
    const double minX = std::min(startStub.x, endStub.x);
    const double maxX = std::max(startStub.x, endStub.x);
    const double minY = std::min(startStub.y, endStub.y);
    const double maxY = std::max(startStub.y, endStub.y);
    for (const NodeObstacle& obstacle : obstacles) {
      if (obstacle.rect.bottom >= minY - laneGap && obstacle.rect.top <= maxY + laneGap) {
        addLaneValue(verticalLanes, obstacle.rect.left - laneGap);
        addLaneValue(verticalLanes, obstacle.rect.right + laneGap);
      }
      if (obstacle.rect.right >= minX - laneGap && obstacle.rect.left <= maxX + laneGap) {
        addLaneValue(horizontalLanes, obstacle.rect.top - laneGap);
        addLaneValue(horizontalLanes, obstacle.rect.bottom + laneGap);
      }
    }

    for (double lane : nearestUniqueLaneValues(horizontalLanes, midY, 8)) {
      candidates.push_back({ start, startStub, { startStub.x, lane }, { endStub.x, lane }, endStub, end });
    }
    for (double lane : nearestUniqueLaneValues(verticalLanes, (startStub.x + endStub.x) / 2.0, 12)) {
      candidates.push_back({ start, startStub, { lane, startStub.y }, { lane, endStub.y }, endStub, end });
    }

    std::vector<double> sourceLanes = nearestUniqueLaneValues(horizontalLanes, startStub.y, 4);
    std::vector<double> targetLanes = nearestUniqueLaneValues(horizontalLanes, endStub.y, 4);
    std::vector<double> bridgeLanes = nearestUniqueLaneValues(
      verticalLanes,
      (startStub.x + endStub.x) / 2.0,
      8);
    ensureLaneValue(sourceLanes, startStub.y);
    ensureLaneValue(targetLanes, endStub.y);
    ensureLaneValue(bridgeLanes, leftLane);
    ensureLaneValue(bridgeLanes, rightLane);
    for (double sourceLane : sourceLanes) {
      for (double targetLane : targetLanes) {
        for (double bridgeLane : bridgeLanes) {
          candidates.push_back({
            start,
            startStub,
            { startStub.x, sourceLane },
            { bridgeLane, sourceLane },
            { bridgeLane, targetLane },
            { endStub.x, targetLane },
            endStub,
            end,
          });
        }
      }
    }

    candidates.push_back({ start, startStub, { leftLane, startStub.y }, { leftLane, endStub.y }, endStub, end });
    candidates.push_back({ start, startStub, { rightLane, startStub.y }, { rightLane, endStub.y }, endStub, end });
  }

  const std::vector<VisibilityPort> sourcePorts =
    makeVisibilityPorts(source, laneOffset, portInset, stub);
  const std::vector<VisibilityPort> targetPorts =
    makeVisibilityPorts(target, -laneOffset, portInset, stub);
  const VisibilityRoute visibilityRoute =
    routeVisibilityGridWithPorts(sourcePorts, targetPorts, graphBounds, obstacleRects, laneOffset, occupancy);
  if (visibilityRoute.found && visibilityRoute.points.size() >= 2) {
    std::vector<RoutePoint> candidate;
    candidate.reserve(visibilityRoute.points.size() + 2);
    candidate.push_back(sourcePorts[visibilityRoute.sourcePortIndex].point);
    candidate.insert(candidate.end(), visibilityRoute.points.begin(), visibilityRoute.points.end());
    candidate.push_back(targetPorts[visibilityRoute.targetPortIndex].point);
    candidate = compressRoutePoints(std::move(candidate));
    if (
      routeScore(candidate, obstacles) < 1'000'000.0
      && routeOccupancyPenalty(candidate, occupancy) < 0.01) {
      return candidate;
    }
    candidates.push_back(std::move(candidate));
  }

  const double outerLaneSpread = static_cast<double>((line.lineIndex * 17) % 29) * 18.0;
  const double outerTop = graphBounds.top - outerGap - std::abs(laneOffset) - outerLaneSpread;
  const double outerBottom = graphBounds.bottom + outerGap + std::abs(laneOffset) + outerLaneSpread;
  const double outerLeft = graphBounds.left - outerGap - std::abs(laneOffset) - outerLaneSpread;
  const double outerRight = graphBounds.right + outerGap + std::abs(laneOffset) + outerLaneSpread;
  for (const VisibilityPort& sourcePort : sourcePorts) {
    for (const VisibilityPort& targetPort : targetPorts) {
      std::vector<std::vector<RoutePoint>> outerCandidates = {
        {
          sourcePort.point,
          sourcePort.stub,
          { sourcePort.stub.x, outerTop },
          { targetPort.stub.x, outerTop },
          targetPort.stub,
          targetPort.point,
        },
        {
          sourcePort.point,
          sourcePort.stub,
          { sourcePort.stub.x, outerBottom },
          { targetPort.stub.x, outerBottom },
          targetPort.stub,
          targetPort.point,
        },
        {
          sourcePort.point,
          sourcePort.stub,
          { outerLeft, sourcePort.stub.y },
          { outerLeft, targetPort.stub.y },
          targetPort.stub,
          targetPort.point,
        },
        {
          sourcePort.point,
          sourcePort.stub,
          { outerRight, sourcePort.stub.y },
          { outerRight, targetPort.stub.y },
          targetPort.stub,
          targetPort.point,
        },
      };

      for (std::vector<RoutePoint>& candidate : outerCandidates) {
        candidate = compressRoutePoints(std::move(candidate));
        if (
          routeScore(candidate, obstacles) < 1'000'000.0
          && routeOccupancyPenalty(candidate, occupancy) < 0.01) {
          return candidate;
        }
        candidates.push_back(std::move(candidate));
      }
    }
  }

  std::vector<RoutePoint> best;
  double bestScore = std::numeric_limits<double>::infinity();
  for (std::vector<RoutePoint> candidate : candidates) {
    candidate = compressRoutePoints(std::move(candidate));
    const double score = routeScore(candidate, obstacles, occupancy);
    if (score < bestScore) {
      bestScore = score;
      best = std::move(candidate);
    }
  }

  return best;
}

PlanarBackboneLayoutResult runPlanarBackboneLayout(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  PlanarBackboneLayoutResult result;
  if (nodes.empty()) return result;

  ogdf::Graph backbone;
  std::vector<ogdf::node> backboneNodes(nodes.size(), nullptr);
  std::unordered_map<ogdf::node, std::size_t> originalIndex;
  originalIndex.reserve(nodes.size());
  ogdf::NodeArray<std::size_t> backboneIndex(backbone);
  for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
    backboneNodes[nodeIndex] = backbone.newNode();
    backboneIndex[backboneNodes[nodeIndex]] = nodeIndex;
    originalIndex[nodes[nodeIndex].handle] = nodeIndex;
  }

  std::set<std::pair<std::size_t, std::size_t>> uniquePairs;
  for (const EdgeRecord& edge : edges) {
    const auto sourceIt = originalIndex.find(edge.sourceHandle);
    const auto targetIt = originalIndex.find(edge.targetHandle);
    if (
        sourceIt == originalIndex.end()
        || targetIt == originalIndex.end()
        || sourceIt->second == targetIt->second) {
      continue;
    }
    uniquePairs.insert(std::minmax(sourceIt->second, targetIt->second));
  }
  result.uniqueEdges = uniquePairs.size();

  ogdf::EdgeArray<std::pair<std::size_t, std::size_t>> endpoints(backbone);
  std::vector<std::size_t> degree(nodes.size(), 0);
  for (const auto& pair : uniquePairs) {
    const ogdf::edge edge = backbone.newEdge(
      backboneNodes[pair.first], backboneNodes[pair.second]);
    endpoints[edge] = pair;
    ++degree[pair.first];
    ++degree[pair.second];
  }

  ogdf::PlanarSubgraphFast<int> planarSubgraph;
  const int runs = static_cast<int>(readDoubleEnv(
    "DJERD_PLANAR_BACKBONE_RUNS", 0.0, 0.0, 64.0));
  planarSubgraph.runs(runs);
  planarSubgraph.maxThreads(1);
  ogdf::List<ogdf::edge> deletedEdges;
  planarSubgraph.call(backbone, deletedEdges);

  std::vector<std::pair<std::size_t, std::size_t>> deletedPairs;
  deletedPairs.reserve(deletedEdges.size());
  for (ogdf::edge edge : deletedEdges) {
    deletedPairs.push_back(endpoints[edge]);
  }
  result.initiallyDeleted = deletedPairs.size();
  for (ogdf::edge edge : deletedEdges) {
    backbone.delEdge(edge);
  }

  // PlanarSubgraphFast is intentionally fast, but its result is not
  // guaranteed maximal. Reinsert candidates one at a time, preferring links
  // between structural hubs. This remains data-independent and keeps only a
  // linear-size graph resident in memory.
  std::sort(deletedPairs.begin(), deletedPairs.end(), [&](const auto& left, const auto& right) {
    const std::size_t leftDegree = degree[left.first] + degree[left.second];
    const std::size_t rightDegree = degree[right.first] + degree[right.second];
    if (leftDegree != rightDegree) return leftDegree > rightDegree;
    return left < right;
  });
  ogdf::BoyerMyrvold planarityTest;
  for (const auto& pair : deletedPairs) {
    const ogdf::edge candidate = backbone.newEdge(
      backboneNodes[pair.first], backboneNodes[pair.second]);
    if (planarityTest.isPlanar(backbone)) {
      ++result.reinserted;
    } else {
      backbone.delEdge(candidate);
    }
  }
  result.remainingDeleted = result.initiallyDeleted - result.reinserted;

  ogdf::NodeArray<int> componentOf(backbone, -1);
  std::vector<std::vector<ogdf::node>> components;
  for (ogdf::node start : backbone.nodes) {
    if (componentOf[start] >= 0) continue;
    const int componentIndex = static_cast<int>(components.size());
    components.emplace_back();
    std::queue<ogdf::node> pending;
    pending.push(start);
    componentOf[start] = componentIndex;
    while (!pending.empty()) {
      const ogdf::node current = pending.front();
      pending.pop();
      components.back().push_back(current);
      for (ogdf::adjEntry adjacency : current->adjEntries) {
        const ogdf::node neighbor = adjacency->twinNode();
        if (componentOf[neighbor] >= 0) continue;
        componentOf[neighbor] = componentIndex;
        pending.push(neighbor);
      }
    }
  }
  std::sort(components.begin(), components.end(), [](const auto& left, const auto& right) {
    return left.size() > right.size();
  });
  result.components = components.size();

  const double separation = readDoubleEnv(
    "DJERD_PLANAR_BACKBONE_SEPARATION", 48.0, 0.0, 10000.0);
  const double componentGap = readDoubleEnv(
    "DJERD_PLANAR_BACKBONE_COMPONENT_GAP", 1000.0, 1.0, 100000.0);
  double cursorX = 0.0;
  for (const std::vector<ogdf::node>& component : components) {
    ogdf::Graph componentGraph;
    ogdf::GraphAttributes componentAttributes(
      componentGraph,
      ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
    std::unordered_map<ogdf::node, ogdf::node> componentCopy;
    componentCopy.reserve(component.size());
    for (ogdf::node sourceNode : component) {
      const std::size_t nodeIndex = backboneIndex[sourceNode];
      const ogdf::node copyNode = componentGraph.newNode();
      componentCopy[sourceNode] = copyNode;
      componentAttributes.width(copyNode) = std::max(1.0, nodes[nodeIndex].width);
      componentAttributes.height(copyNode) = std::max(1.0, nodes[nodeIndex].height);
    }
    for (ogdf::edge edge : backbone.edges) {
      if (componentOf[edge->source()] != componentOf[component.front()]) continue;
      componentGraph.newEdge(
        componentCopy.at(edge->source()), componentCopy.at(edge->target()));
    }

    ogdf::FPPLayout layout;
    layout.separation(separation);
    layout.call(componentAttributes);

    double minX = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    for (ogdf::node sourceNode : component) {
      const std::size_t nodeIndex = backboneIndex[sourceNode];
      const ogdf::node copyNode = componentCopy.at(sourceNode);
      minX = std::min(
        minX,
        componentAttributes.x(copyNode) - nodes[nodeIndex].width * 0.5);
      maxX = std::max(
        maxX,
        componentAttributes.x(copyNode) + nodes[nodeIndex].width * 0.5);
    }
    if (!std::isfinite(minX) || !std::isfinite(maxX)) {
      minX = maxX = 0.0;
    }
    for (ogdf::node sourceNode : component) {
      const std::size_t nodeIndex = backboneIndex[sourceNode];
      const ogdf::node copyNode = componentCopy.at(sourceNode);
      attributes.x(nodes[nodeIndex].handle) =
        componentAttributes.x(copyNode) + cursorX - minX;
      attributes.y(nodes[nodeIndex].handle) = componentAttributes.y(copyNode);
    }
    cursorX += std::max(1.0, maxX - minX) + componentGap;
  }

  std::fprintf(
    stderr,
    "[planar-backbone] nodes=%zu uniqueEdges=%zu initiallyDeleted=%zu "
    "reinserted=%zu remainingDeleted=%zu components=%zu runs=%d.\n",
    nodes.size(),
    result.uniqueEdges,
    result.initiallyDeleted,
    result.reinserted,
    result.remainingDeleted,
    result.components,
    runs);
  return result;
}

std::string describeLayoutAlgorithm(const std::string& mode) {
  if (mode == "hierarchical") {
    return "SugiyamaLayout + MedianHeuristic";
  }
  if (mode == "hierarchical_barycenter") {
    return "SugiyamaLayout + BarycenterHeuristic";
  }
  if (mode == "hierarchical_sifting") {
    return "SugiyamaLayout + SiftingHeuristic";
  }
  if (mode == "hierarchical_global_sifting") {
    return "SugiyamaLayout + GlobalSifting";
  }
  if (mode == "hierarchical_greedy_insert") {
    return "SugiyamaLayout + GreedyInsertHeuristic";
  }
  if (mode == "hierarchical_greedy_switch") {
    return "SugiyamaLayout + GreedySwitchHeuristic";
  }
  if (mode == "hierarchical_grid_sifting") {
    return "SugiyamaLayout + GridSifting";
  }
  if (mode == "hierarchical_split") {
    return "SugiyamaLayout + SplitHeuristic";
  }
  if (mode == "circular") {
    return "CircularLayout";
  }
  if (mode == "linear") {
    return "LinearLayout";
  }
  if (mode == "clustered" || mode == "fmmm") {
    return "FMMMLayout";
  }
  if (mode == "constrained_force") {
    return "ConstrainedForceDirectedLayout";
  }
  if (mode == "constrained_force_straight") {
    return "ConstrainedForceDirectedLayout + StraightLineRouter";
  }
  if (mode == "fast_multipole") {
    return "FastMultipoleEmbedder";
  }
  if (mode == "fast_multipole_multilevel") {
    return "FastMultipoleMultilevelEmbedder";
  }
  if (mode == "stress_minimization") {
    return "StressMinimization";
  }
  if (mode == "pivot_mds") {
    return "PivotMDS";
  }
  if (mode == "davidson_harel") {
    return "DavidsonHarelLayout";
  }
  if (mode == "planarization") {
    return "PlanarizationLayout";
  }
  if (mode == "planarization_grid") {
    return "PlanarizationGridLayout";
  }
  if (mode == "planar_backbone") {
    return "PlanarSubgraphFast + FPPLayout";
  }
  if (mode == "ortho") {
    return "PlanarizationLayout + OrthoLayout";
  }
  if (mode == "planar_draw") {
    return "PlanarDrawLayout";
  }
  if (mode == "planar_straight") {
    return "PlanarStraightLayout";
  }
  if (mode == "schnyder") {
    return "SchnyderLayout";
  }
  if (mode == "upward_layer_based") {
    return "UpwardPlanarizationLayout + LayerBasedUPRLayout";
  }
  if (mode == "upward_planarization") {
    return "UpwardPlanarizationLayout";
  }
  if (mode == "visibility") {
    return "VisibilityLayout";
  }
  if (mode == "cluster_planarization") {
    return "ClusterPlanarizationLayout";
  }
  if (mode == "cluster_ortho") {
    return "ClusterPlanarizationLayout + ClusterOrthoLayout";
  }
  if (mode == "uml_ortho") {
    return "PlanarizationLayoutUML + OrthoLayoutUML";
  }
  if (mode == "uml_planarization") {
    return "PlanarizationLayoutUML";
  }
  if (mode == "tree") {
    return "TreeLayout";
  }
  if (mode == "radial_tree") {
    return "RadialTreeLayout";
  }

  return mode;
}

LayoutRunMetadata makeLayoutRunMetadata(const std::string& mode) {
  const std::string algorithm = describeLayoutAlgorithm(mode);
  return {
    mode,
    mode == "clustered" ? "fmmm" : mode,
    algorithm,
    algorithm,
    "exact",
    "",
  };
}

LayoutRunMetadata runLayout(
  const std::string& mode,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  LayoutRunMetadata metadata = makeLayoutRunMetadata(mode);

  if (isSugiyamaMode(mode)) {
    const bool requiresSurrogate =
      mode == "hierarchical_global_sifting"
      || mode == "hierarchical_grid_sifting";
    const bool useSurrogate =
      requiresSurrogate
      || (
        nodes.size() >= kSugiyamaSurrogateNodeThreshold
        && mode != "hierarchical"
        && mode != "hierarchical_barycenter");
    std::string actualRunMode = mode;

    if (useSurrogate) {
      actualRunMode =
        mode == "hierarchical_grid_sifting" || mode == "hierarchical_greedy_switch"
          ? "hierarchical"
          : "hierarchical_barycenter";
      metadata.actualMode = mode;
      metadata.strategy = requiresSurrogate ? "surrogate" : "large_graph_surrogate";

      if (mode == "hierarchical_sifting") {
        metadata.actualAlgorithm =
          "DjangoErdSiftingSurrogate(SugiyamaLayout + BarycenterHeuristic, layerStagger)";
        metadata.strategyReason =
          nodeThresholdReason(
            kSugiyamaSurrogateNodeThreshold,
            "sifting cross minimization uses a bounded barycenter base plus layer staggering");
      } else if (mode == "hierarchical_global_sifting") {
        metadata.actualAlgorithm =
          "DjangoErdGlobalSiftingSurrogate(SugiyamaLayout + BarycenterHeuristic, globalLayerDrift)";
        metadata.strategyReason =
          "GlobalSifting is unstable on ERD-scale cyclic graphs, so ERD mode uses a bounded barycenter base plus global layer drift";
      } else if (mode == "hierarchical_greedy_insert") {
        metadata.actualAlgorithm =
          "DjangoErdGreedyInsertSurrogate(SugiyamaLayout + BarycenterHeuristic, compactInsert)";
        metadata.strategyReason =
          nodeThresholdReason(
            kSugiyamaSurrogateNodeThreshold,
            "greedy insert uses a bounded barycenter base plus compact insertion offsets");
      } else if (mode == "hierarchical_greedy_switch") {
        metadata.actualAlgorithm =
          "DjangoErdGreedySwitchSurrogate(SugiyamaLayout + MedianHeuristic, alternatingSwitch)";
        metadata.strategyReason =
          nodeThresholdReason(
            kSugiyamaSurrogateNodeThreshold,
            "greedy switch uses a bounded median base plus alternating layer switches");
      } else if (mode == "hierarchical_grid_sifting") {
        metadata.actualAlgorithm =
          "DjangoErdGridSiftingSurrogate(SugiyamaLayout + MedianHeuristic, layerGridSnap)";
        metadata.strategyReason =
          "GridSifting is unstable on ERD-scale cyclic graphs, so ERD mode uses a bounded median base plus layer grid snapping";
      } else {
        metadata.actualAlgorithm =
          "DjangoErdSplitHeuristicSurrogate(SugiyamaLayout + BarycenterHeuristic, splitBands)";
        metadata.strategyReason =
          nodeThresholdReason(
            kSugiyamaSurrogateNodeThreshold,
            "split heuristic uses a bounded barycenter base plus split bands");
      }
    } else {
      metadata.actualAlgorithm += "(runs=1)";
      metadata.strategy = "bounded";
      metadata.strategyReason = "Sugiyama runs/fails are capped for interactive layout";
    }

    // DJERD_DISABLE_CLUSTER_FALLBACK=1 — keep pure SugiyamaLayout for
    // hierarchical_barycenter on large graphs. The default ClusteredLayout
    // (FMMM meta-layout) gives nicer-looking placement but takes ~5 min on
    // 1000-node ERDs; the pure Sugiyama path drops that to ~30 s at the
    // cost of more crossings.
    const char* skipClusterFallbackEnv =
      std::getenv("DJERD_DISABLE_CLUSTER_FALLBACK");
    const bool skipClusterFallback =
      skipClusterFallbackEnv != nullptr
      && std::strcmp(skipClusterFallbackEnv, "0") != 0;
    if (!useSurrogate && mode == "hierarchical_barycenter" && !skipClusterFallback) {
      const bool useAppClusters = hasMeaningfulClusters(nodes);
      const bool useStructuralFallback = !useAppClusters && nodes.size() >= 80;
      if (useAppClusters || useStructuralFallback) {
        std::vector<NodeRecord> clusteredNodes = nodes;
        if (useStructuralFallback) {
          std::size_t bccBridgeCount = 0;
          std::size_t bccComponentCount = 0;
          std::size_t bccLargestClusterSize = 0;
          std::vector<std::string> labels =
            assignBiconnectedClusterLabels(nodes, edges, bccBridgeCount, bccComponentCount, bccLargestClusterSize);
          const bool bccDegenerate =
            bccComponentCount < 2
            || bccComponentCount > nodes.size() / 2
            || bccBridgeCount == 0
            || bccLargestClusterSize * 2 > nodes.size();
          if (bccDegenerate) {
            // Hub-excluded BCC: pull dominant hubs out and run BCC on the
            // residual subgraph. Use this when standard BCC is degenerate
            // (typically because a giant biconnected blob exists).
            std::size_t hubCount = 0;
            std::size_t residualBridges = 0;
            std::size_t residualBcc = 0;
            std::vector<std::string> hubLabels = assignHubExcludedBccLabels(
              nodes, edges, hubCount, residualBridges, residualBcc);
            const bool hubMethodWorked =
              hubCount > 0 && residualBridges > 0 && residualBcc >= 4;
            if (hubMethodWorked) {
              labels = std::move(hubLabels);
            } else {
              std::size_t louvL1Count = 0;
              std::size_t louvL2Count = 0;
              std::size_t louvIters = 0;
              std::vector<std::string> louvLabels =
                assignTwoLevelLouvainLabels(nodes, edges, louvL1Count, louvL2Count, louvIters);
              const std::size_t louvCommCount = louvL1Count;
              const bool louvWorked =
                louvCommCount >= 4
                && louvCommCount * 2 < nodes.size();
              if (louvWorked) {
                labels = std::move(louvLabels);
              } else {
                const std::size_t targetClusters =
                  std::max<std::size_t>(8, std::min<std::size_t>(32, nodes.size() / 50));
                labels = assignStructuralClusterLabels(nodes, edges, targetClusters);
              }
            }
          }
          for (std::size_t i = 0; i < clusteredNodes.size(); ++i) {
            clusteredNodes[i].appLabel = labels[i];
          }
        }
        ClusterRunOptions opts;
        opts.innerMode = "sugiyama";
        opts.innerLayerDistance = 60.0;
        opts.innerNodeDistance = 28.0;
        opts.metaMode = "fmmm";
        opts.metaUnitEdgeLength = 80.0;
        opts.interClusterPadding = 20.0;
        std::size_t clusterCount = 0;
        std::size_t interEdges = 0;
        runClusteredByAppLayout(opts, clusteredNodes, edges, attributes, clusterCount, interEdges); for (const auto& cn : clusteredNodes) metadata.clusterByModelId[cn.modelId] = cn.appLabel;
        const std::string clusterSource = useAppClusters ? "appLabel" : "graphStructure";
        metadata.actualAlgorithm =
          "ClusteredLayout(source=" + clusterSource
          + ", inner=SugiyamaLayout + BarycenterHeuristic, meta=FMMM(weighted), clusters="
          + std::to_string(clusterCount) + ", interClusterEdges=" + std::to_string(interEdges) + ")";
        metadata.strategy = "clustered";
        metadata.strategyReason = useAppClusters
          ? "nodes grouped by appLabel; per-app Sugiyama with FMMM weighted meta-layout"
          : "BCC → hub-excluded BCC → Louvain modularity → high-degree-hub BFS fallback chain; FMMM weighted meta-layout";
        return metadata;
      }
    }

    runSugiyamaLayout(actualRunMode, attributes);
    if (useSurrogate) {
      if (mode == "hierarchical_sifting") {
        applySiftingSurrogateGeometry(nodes, edges, attributes);
      } else if (mode == "hierarchical_global_sifting") {
        applyGlobalSiftingSurrogateGeometry(nodes, edges, attributes);
      } else if (mode == "hierarchical_greedy_insert") {
        applyGreedyInsertSurrogateGeometry(nodes, edges, attributes);
      } else if (mode == "hierarchical_greedy_switch") {
        applyGreedySwitchSurrogateGeometry(nodes, edges, attributes);
      } else if (mode == "hierarchical_grid_sifting") {
        applyGridSiftingSurrogateGeometry(nodes, edges, attributes);
      } else {
        applySplitSurrogateGeometry(nodes, edges, attributes);
      }
    }
    return metadata;
  }

  if (mode == "circular") {
    {
      const bool useAppClusters = hasMeaningfulClusters(nodes);
      const bool useStructuralFallback = !useAppClusters && nodes.size() >= 80;
      if (useAppClusters || useStructuralFallback) {
        std::vector<NodeRecord> clusteredNodes = nodes;
        if (useStructuralFallback) {
          std::size_t bccBridgeCount = 0;
          std::size_t bccComponentCount = 0;
          std::size_t bccLargestClusterSize = 0;
          std::vector<std::string> labels =
            assignBiconnectedClusterLabels(nodes, edges, bccBridgeCount, bccComponentCount, bccLargestClusterSize);
          const bool bccDegenerate =
            bccComponentCount < 2
            || bccComponentCount > nodes.size() / 2
            || bccBridgeCount == 0
            || bccLargestClusterSize * 2 > nodes.size();
          if (bccDegenerate) {
            // Hub-excluded BCC: pull dominant hubs out and run BCC on the
            // residual subgraph. Use this when standard BCC is degenerate
            // (typically because a giant biconnected blob exists).
            std::size_t hubCount = 0;
            std::size_t residualBridges = 0;
            std::size_t residualBcc = 0;
            std::vector<std::string> hubLabels = assignHubExcludedBccLabels(
              nodes, edges, hubCount, residualBridges, residualBcc);
            const bool hubMethodWorked =
              hubCount > 0 && residualBridges > 0 && residualBcc >= 4;
            if (hubMethodWorked) {
              labels = std::move(hubLabels);
            } else {
              std::size_t louvL1Count = 0;
              std::size_t louvL2Count = 0;
              std::size_t louvIters = 0;
              std::vector<std::string> louvLabels =
                assignTwoLevelLouvainLabels(nodes, edges, louvL1Count, louvL2Count, louvIters);
              const std::size_t louvCommCount = louvL1Count;
              const bool louvWorked =
                louvCommCount >= 4
                && louvCommCount * 2 < nodes.size();
              if (louvWorked) {
                labels = std::move(louvLabels);
              } else {
                const std::size_t targetClusters =
                  std::max<std::size_t>(8, std::min<std::size_t>(32, nodes.size() / 50));
                labels = assignStructuralClusterLabels(nodes, edges, targetClusters);
              }
            }
          }
          for (std::size_t i = 0; i < clusteredNodes.size(); ++i) {
            clusteredNodes[i].appLabel = labels[i];
          }
        }
        ClusterRunOptions opts;
        opts.innerMode = "circular";
        opts.metaMode = "fmmm";
        opts.metaUnitEdgeLength = 80.0;
        opts.interClusterPadding = 20.0;
        std::size_t clusterCount = 0;
        std::size_t interEdges = 0;
        runClusteredByAppLayout(opts, clusteredNodes, edges, attributes, clusterCount, interEdges); for (const auto& cn : clusteredNodes) metadata.clusterByModelId[cn.modelId] = cn.appLabel;
        const std::string clusterSource = useAppClusters ? "appLabel" : "graphStructure";
        metadata.actualAlgorithm =
          "ClusteredLayout(source=" + clusterSource
          + ", inner=CircularLayout, meta=FMMM, clusters="
          + std::to_string(clusterCount) + ", interClusterEdges=" + std::to_string(interEdges) + ")";
        metadata.strategy = "clustered";
        metadata.strategyReason =
          "per-cluster CircularLayout produces ring-shaped clusters; FMMM meta-layout separates them by structural attraction";
        return metadata;
      }
    }

    ogdf::CircularLayout layout;
    layout.minDistCircle(96.0);
    layout.minDistCC(96.0);
    layout.minDistLevel(96.0);
    layout.minDistSibling(48.0);
    layout.call(attributes);
    return metadata;
  }

  if (mode == "linear") {
    ogdf::LinearLayout layout;
    layout.call(attributes);
    return metadata;
  }

  if (mode == "clustered" || mode == "fmmm") {
    ogdf::FMMMLayout layout;
    layout.useHighLevelOptions(true);
    layout.unitEdgeLength(140.0);
    layout.newInitialPlacement(true);
    layout.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::BeautifulAndFast);
    layout.call(attributes);
    metadata.actualMode = "fmmm";
    metadata.actualAlgorithm = "FMMMLayout(BeautifulAndFast, unitEdgeLength=140)";
    return metadata;
  }

  if (isConstrainedForceMode(mode)) {
    ogdf::FMMMLayout seedLayout;
    seedLayout.useHighLevelOptions(true);
    seedLayout.unitEdgeLength(170.0);
    seedLayout.newInitialPlacement(true);
    seedLayout.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::BeautifulAndFast);
    seedLayout.call(attributes);

    ogdf::StressMinimization stressLayout;
    stressLayout.hasInitialLayout(true);
    stressLayout.setIterations(90);
    stressLayout.setEdgeCosts(170.0);
    stressLayout.layoutComponentsSeparately(true);
    stressLayout.call(attributes);

    metadata.actualMode = mode;
    metadata.actualAlgorithm = isStraightLineRoutingMode(mode)
      ? "ConstrainedForceDirectedLayout(FMMM seed + StressMinimization, degree-hub axis refinement, straight-line routing)"
      : "ConstrainedForceDirectedLayout(FMMM seed + StressMinimization, constrained post-process)";
    metadata.strategy = "constrained";
    metadata.strategyReason = isStraightLineRoutingMode(mode)
      ? "force-directed layout is refined around high-degree hubs, separated, and rendered with direct straight-line edges"
      : "force-directed layout is refined with node separation, edge-node clearance, and occupancy-aware visibility routing";
    return metadata;
  }

  if (mode == "fast_multipole") {
    const bool useAppClusters = hasMeaningfulClusters(nodes);
    const bool useStructuralFallback = false;
    if (useAppClusters || useStructuralFallback) {
      std::vector<NodeRecord> clusteredNodes = nodes;
      if (useStructuralFallback) {
        std::size_t bccBridgeCount = 0;
        std::size_t bccComponentCount = 0;
        std::size_t bccLargestClusterSize = 0;
        std::vector<std::string> labels =
          assignBiconnectedClusterLabels(nodes, edges, bccBridgeCount, bccComponentCount, bccLargestClusterSize);
        const bool bccDegenerate =
          bccComponentCount < 2
          || bccComponentCount > nodes.size() / 2
          || bccBridgeCount == 0
          || bccLargestClusterSize * 2 > nodes.size();
        if (bccDegenerate) {
          std::size_t hubCount = 0;
          std::size_t residualBridges = 0;
          std::size_t residualBcc = 0;
          std::vector<std::string> hubLabels = assignHubExcludedBccLabels(
            nodes, edges, hubCount, residualBridges, residualBcc);
          const bool hubMethodWorked =
            hubCount > 0 && residualBridges > 0 && residualBcc >= 4;
          if (hubMethodWorked) {
            labels = std::move(hubLabels);
          } else {
            std::size_t louvL1Count = 0;
            std::size_t louvL2Count = 0;
            std::size_t louvIters = 0;
            std::vector<std::string> louvLabels =
              assignTwoLevelLouvainLabels(nodes, edges, louvL1Count, louvL2Count, louvIters);
            const std::size_t louvCommCount = louvL1Count;
            const bool louvWorked =
              louvCommCount >= 4
              && louvCommCount * 2 < nodes.size();
            if (louvWorked) {
              labels = std::move(louvLabels);
            } else {
              const std::size_t targetClusters =
                std::max<std::size_t>(8, std::min<std::size_t>(32, nodes.size() / 50));
              labels = assignStructuralClusterLabels(nodes, edges, targetClusters);
            }
          }
        }
        for (std::size_t i = 0; i < clusteredNodes.size(); ++i) {
          clusteredNodes[i].appLabel = labels[i];
        }
      }
      ClusterRunOptions opts;
      opts.innerMode = "fmm";
      opts.innerFmmEdgeLength = 220.0;
      opts.innerFmmNodeSize = 72.0;
      opts.innerFmmIterations = 300;
      opts.metaUnitEdgeLength = 1500.0;
      opts.interClusterPadding = 320.0;
      std::size_t clusterCount = 0;
      std::size_t interEdges = 0;
      runClusteredByAppLayout(opts, clusteredNodes, edges, attributes, clusterCount, interEdges); for (const auto& cn : clusteredNodes) metadata.clusterByModelId[cn.modelId] = cn.appLabel;
      const std::string clusterSource = useAppClusters ? "appLabel" : "graphStructure";
      metadata.actualAlgorithm =
        "ClusteredLayout(source=" + clusterSource
        + ", inner=FastMultipoleEmbedder, meta=FMMM, clusters="
        + std::to_string(clusterCount) + ", interClusterEdges=" + std::to_string(interEdges) + ")";
      metadata.strategy = "clustered";
      metadata.strategyReason = useAppClusters
        ? "nodes grouped by appLabel; per-app FMM keeps intra-app edges short while FMMM meta-layout separates app clusters"
        : "no useful appLabel split; BCC → hub-excluded BCC → Louvain modularity → high-degree-hub BFS fallback chain; FMMM meta-layout";
      return metadata;
    }
    runFastMultipoleLayout(attributes, 300, 6, true);
    metadata.actualAlgorithm = "FastMultipoleEmbedder(iterations=300, multipolePrecision=6)";
    metadata.strategy = "bounded";
    metadata.strategyReason = "iteration count is capped for interactive layout";
    return metadata;
  }

  if (mode == "fast_multipole_multilevel") {
    if (nodes.size() >= kEnergySurrogateNodeThreshold) {
      runFastMultipoleLayout(attributes, 180, 4, true);
      metadata.actualMode = "fast_multipole_multilevel";
      metadata.actualAlgorithm =
        "DjangoErdFastMultipoleMultilevelSurrogate(FastMultipoleEmbedder, iterations=180, multipolePrecision=4)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        nodeThresholdReason(
          kEnergySurrogateNodeThreshold,
          "multilevel embedder is replaced with bounded fast multipole");
      return metadata;
    }

    ogdf::FastMultipoleMultilevelEmbedder layout;
    layout.multilevelUntilNumNodesAreLess(kFastMultipoleMultilevelCoarseNodeBound);
    layout.maxNumThreads(static_cast<int>(std::min<std::size_t>(4, idealThreadCount())));
    layout.call(attributes);
    metadata.actualAlgorithm =
      "FastMultipoleMultilevelEmbedder(minCoarseNodes=1024,maxThreads<=4)";
    metadata.strategy = "bounded";
    metadata.strategyReason =
      "coarsening stops earlier because OGDF multilevel iterations grow quadratically by level";
    return metadata;
  }

  if (mode == "stress_minimization") {
    ogdf::StressMinimization layout;
    layout.hasInitialLayout(true);
    layout.setIterations(150);
    layout.setEdgeCosts(140.0);
    layout.layoutComponentsSeparately(true);
    layout.call(attributes);
    metadata.actualAlgorithm = "StressMinimization(initialLayout=true, iterations=150)";
    metadata.strategy = "bounded";
    metadata.strategyReason = "iteration count is capped and analyzer positions seed the layout";
    return metadata;
  }

  if (mode == "pivot_mds") {
    ogdf::PivotMDS layout;
    layout.setNumberOfPivots(std::max(16, std::min(256, static_cast<int>(nodes.size()))));
    layout.setEdgeCosts(140.0);
    layout.setForcing2DLayout(true);
    layout.call(attributes);
    applyPivotMdsGeometry(nodes, edges, attributes);
    metadata.actualAlgorithm = "PivotMDS(pivots<=256, edgeCosts=140, rotatedScale=true)";
    metadata.strategy = "bounded";
    metadata.strategyReason =
      "pivot count is capped and output is normalized apart from stress minimization";
    return metadata;
  }

  if (mode == "davidson_harel") {
    ogdf::DavidsonHarelLayout layout;
    const bool largeGraph = nodes.size() >= kDavidsonHarelReducedNodeThreshold;
    const bool forcePlanarity = readBoolEnv(
      "DJERD_DH_PLANAR",
      !largeGraph);
    layout.fixSettings(
      forcePlanarity
        ? ogdf::DavidsonHarelLayout::SettingsParameter::Planar
        : ogdf::DavidsonHarelLayout::SettingsParameter::Standard);
    const int iterations = static_cast<int>(readDoubleEnv(
      "DJERD_DH_ITERATIONS",
      largeGraph ? 18.0 : 120.0,
      1.0,
      1000000.0));
    const int startTemperature = static_cast<int>(readDoubleEnv(
      "DJERD_DH_START_TEMPERATURE",
      largeGraph ? 80.0 : 240.0,
      1.0,
      100000.0));
    const double repulsionWeight = readDoubleEnv(
      "DJERD_DH_REPULSION_WEIGHT", 900.0, 0.0, 1000000.0);
    const double attractionWeight = readDoubleEnv(
      "DJERD_DH_ATTRACTION_WEIGHT", 250.0, 0.0, 1000000.0);
    const double overlapWeight = readDoubleEnv(
      "DJERD_DH_OVERLAP_WEIGHT", 1450.0, 0.0, 1000000.0);
    const double planarityWeight = readDoubleEnv(
      "DJERD_DH_PLANARITY_WEIGHT",
      forcePlanarity ? 3000.0 : 300.0,
      0.0,
      10000000.0);
    const double preferredEdgeLength = readDoubleEnv(
      "DJERD_DH_EDGE_LENGTH", 140.0, 1.0, 100000.0);
    layout.setNumberOfIterations(iterations);
    layout.setStartTemperature(startTemperature);
    layout.setRepulsionWeight(repulsionWeight);
    layout.setAttractionWeight(attractionWeight);
    layout.setNodeOverlapWeight(overlapWeight);
    layout.setPlanarityWeight(planarityWeight);
    layout.setPreferredEdgeLength(preferredEdgeLength);
    layout.call(attributes);
    metadata.actualAlgorithm =
      "DavidsonHarelLayout(" + std::string(forcePlanarity ? "Planar" : "Standard")
      + ", iterations=" + std::to_string(iterations)
      + ", startTemperature=" + std::to_string(startTemperature)
      + ", planarityWeight=" + std::to_string(planarityWeight) + ")";
    metadata.strategy = largeGraph ? "large_graph_bounded" : "bounded";
    metadata.strategyReason = largeGraph
      ? nodeThresholdReason(
          kDavidsonHarelReducedNodeThreshold,
          "Davidson-Harel iterations and temperature are reduced")
      : "Davidson-Harel iterations are capped";
    return metadata;
  }

  if (mode == "planar_backbone") {
    const PlanarBackboneLayoutResult result = runPlanarBackboneLayout(
      nodes, edges, attributes);
    metadata.actualAlgorithm =
      "PlanarSubgraphFast+maximalReinsert+FPPLayout(uniqueEdges="
      + std::to_string(result.uniqueEdges)
      + ",initiallyDeleted=" + std::to_string(result.initiallyDeleted)
      + ",reinserted=" + std::to_string(result.reinserted)
      + ",remainingDeleted=" + std::to_string(result.remainingDeleted)
      + ",components=" + std::to_string(result.components) + ")";
    metadata.strategy = "structural_planar_backbone";
    metadata.strategyReason =
      "a data-independent maximal planar backbone determines straight real-node coordinates";
    return metadata;
  }

  if (mode == "planarization") {
    if (nodes.size() >= kTopologySurrogateNodeThreshold) {
      runSugiyamaLayout("hierarchical_barycenter", attributes);
      applyPlanarSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "planarization";
      metadata.actualAlgorithm =
        "DjangoErdPlanarizationSurrogate(SugiyamaLayout + BarycenterHeuristic, planarSkew)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        nodeThresholdReason(
          kTopologySurrogateNodeThreshold,
          "planarization uses a bounded Sugiyama base plus planar skewing");
      return metadata;
    }

    ogdf::PlanarizationLayout layout;
    layout.setCrossMin(createBoundedSubgraphPlanarizer());
    layout.pageRatio(kPlanarizationPageRatio);
    layout.call(attributes);
    metadata.actualAlgorithm =
      "PlanarizationLayout(boundedCrossMin=PlanarSubgraphFast(runs=1)+VariableEmbeddingInserter(removeReinsert=None),pageRatio=1.0)";
    metadata.strategy = "bounded";
    metadata.strategyReason =
      "crossing minimization uses one planar subgraph run and fixed embedding insertion for 60s layout";
    return metadata;
  }

  if (mode == "planarization_grid") {
    if (nodes.size() >= kPlanarizationGridSurrogateNodeThreshold) {
      runSugiyamaLayout("hierarchical", attributes);
      applyPlanarGridSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "planarization_grid";
      metadata.actualAlgorithm =
        "DjangoErdPlanarizationGridSurrogate(SugiyamaLayout + MedianHeuristic, gridSnap)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        nodeThresholdReason(
          kPlanarizationGridSurrogateNodeThreshold,
          "PlanarizationGridLayout is too slow for interactive ERD layout, so ERD mode uses a bounded Sugiyama base snapped to a grid");
      return metadata;
    }

    if (nodes.size() >= kPlanarizationGridProjectionNodeThreshold) {
      ogdf::PlanarizationLayout layout;
      layout.setCrossMin(createBoundedSubgraphPlanarizer());
      layout.pageRatio(kPlanarizationPageRatio);
      layout.call(attributes);
      applyPlanarGridSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "planarization_grid";
      metadata.actualAlgorithm =
        "DjangoErdPlanarizationGridProjection(PlanarizationLayout boundedCrossMin, gridSnap)";
      metadata.strategy = "bounded_projection";
      metadata.strategyReason =
        nodeThresholdReason(
          kPlanarizationGridProjectionNodeThreshold,
          "PlanarizationGridLayout's MixedModel grid layouter exceeded 60s, so ERD mode keeps bounded planarization and projects it onto a grid");
      return metadata;
    }

    ogdf::PlanarizationGridLayout layout;
    layout.setCrossMin(createBoundedSubgraphPlanarizer());
    layout.pageRatio(kPlanarizationPageRatio);
    layout.separation(kPlanarizationGridSeparation);
    layout.call(attributes);
    metadata.actualAlgorithm =
      "PlanarizationGridLayout(boundedCrossMin=PlanarSubgraphFast(runs=1)+VariableEmbeddingInserter(removeReinsert=None),pageRatio=1.25,separation=96)";
    metadata.strategy = "bounded";
    metadata.strategyReason =
      "grid planarization uses bounded crossing minimization and a fixed grid separation for 60s layout";
    return metadata;
  }

  if (mode == "ortho") {
    if (nodes.size() >= kTopologySurrogateNodeThreshold) {
      runSugiyamaLayout("hierarchical", attributes);
      applyOrthogonalSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "ortho";
      metadata.actualAlgorithm =
        "DjangoErdOrthogonalSurrogate(SugiyamaLayout + MedianHeuristic, orthogonalGridRouting)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        nodeThresholdReason(
          kTopologySurrogateNodeThreshold,
          "orthogonal layout uses a bounded Sugiyama base snapped to orthogonal routing");
      return metadata;
    }

    ogdf::PlanarizationLayout layout;
    layout.setCrossMin(createBoundedSubgraphPlanarizer());
    layout.setPlanarLayouter(new ogdf::OrthoLayout());
    layout.pageRatio(kPlanarizationPageRatio);
    layout.call(attributes);
    metadata.actualAlgorithm =
      "PlanarizationLayout + OrthoLayout(boundedCrossMin=PlanarSubgraphFast(runs=1)+VariableEmbeddingInserter(removeReinsert=None),pageRatio=1.0)";
    metadata.strategy = "bounded";
    metadata.strategyReason =
      "orthogonal planarization uses bounded crossing minimization for 60s layout";
    return metadata;
  }

  if (mode == "planar_draw") {
    runSugiyamaLayout("hierarchical_barycenter", attributes);
    applyStraightLineSurrogateGeometry(nodes, edges, attributes);
    metadata.actualMode = "planar_draw";
    metadata.actualAlgorithm =
      "DjangoErdPlanarDrawSurrogate(SugiyamaLayout + BarycenterHeuristic, straightLinePlanarStyle)";
    metadata.strategy = "surrogate";
    metadata.strategyReason =
      "PlanarDrawLayout requires a planar simple graph; ERD input is normalized through a safe straight-line surrogate";
    return metadata;
  }

  if (mode == "planar_straight") {
    runFastMultipoleLayout(attributes, 160, 4, true);
    applyStraightLineSurrogateGeometry(nodes, edges, attributes);
    metadata.actualMode = "planar_straight";
    metadata.actualAlgorithm =
      "DjangoErdPlanarStraightSurrogate(FastMultipoleEmbedder, straightLineNormalize)";
    metadata.strategy = "surrogate";
    metadata.strategyReason =
      "PlanarStraightLayout requires a planar simple graph; ERD input is normalized through a safe straight-line surrogate";
    return metadata;
  }

  if (mode == "schnyder") {
    ogdf::CircularLayout layout;
    layout.minDistCircle(140.0);
    layout.minDistCC(160.0);
    layout.minDistLevel(120.0);
    layout.minDistSibling(60.0);
    layout.call(attributes);
    applySchnyderSurrogateGeometry(nodes, edges, attributes);
    metadata.actualMode = "schnyder";
    metadata.actualAlgorithm =
      "DjangoErdSchnyderSurrogate(CircularLayout, triangularStraightLineProjection)";
    metadata.strategy = "surrogate";
    metadata.strategyReason =
      "SchnyderLayout requires a planar simple graph; ERD input is projected into a safe triangular straight-line style";
    return metadata;
  }

  if (mode == "upward_layer_based" || mode == "upward_planarization") {
    {
      const bool useAppClusters = hasMeaningfulClusters(nodes);
      const bool useStructuralFallback = !useAppClusters && nodes.size() >= 80;
      if (useAppClusters || useStructuralFallback) {
        std::vector<NodeRecord> clusteredNodes = nodes;
        if (useStructuralFallback) {
          std::size_t bccBridgeCount = 0;
          std::size_t bccComponentCount = 0;
          std::size_t bccLargestClusterSize = 0;
          std::vector<std::string> labels =
            assignBiconnectedClusterLabels(nodes, edges, bccBridgeCount, bccComponentCount, bccLargestClusterSize);
          const bool bccDegenerate =
            bccComponentCount < 2
            || bccComponentCount > nodes.size() / 2
            || bccBridgeCount == 0
            || bccLargestClusterSize * 2 > nodes.size();
          if (bccDegenerate) {
            // Hub-excluded BCC: pull dominant hubs out and run BCC on the
            // residual subgraph. Use this when standard BCC is degenerate
            // (typically because a giant biconnected blob exists).
            std::size_t hubCount = 0;
            std::size_t residualBridges = 0;
            std::size_t residualBcc = 0;
            std::vector<std::string> hubLabels = assignHubExcludedBccLabels(
              nodes, edges, hubCount, residualBridges, residualBcc);
            const bool hubMethodWorked =
              hubCount > 0 && residualBridges > 0 && residualBcc >= 4;
            if (hubMethodWorked) {
              labels = std::move(hubLabels);
            } else {
              std::size_t louvL1Count = 0;
              std::size_t louvL2Count = 0;
              std::size_t louvIters = 0;
              std::vector<std::string> louvLabels =
                assignTwoLevelLouvainLabels(nodes, edges, louvL1Count, louvL2Count, louvIters);
              const std::size_t louvCommCount = louvL1Count;
              const bool louvWorked =
                louvCommCount >= 4
                && louvCommCount * 2 < nodes.size();
              if (louvWorked) {
                labels = std::move(louvLabels);
              } else {
                const std::size_t targetClusters =
                  std::max<std::size_t>(8, std::min<std::size_t>(32, nodes.size() / 50));
                labels = assignStructuralClusterLabels(nodes, edges, targetClusters);
              }
            }
          }
          for (std::size_t i = 0; i < clusteredNodes.size(); ++i) {
            clusteredNodes[i].appLabel = labels[i];
          }
        }
        ClusterRunOptions opts;
        opts.innerMode = "sugiyama";
        opts.innerLayerDistance = 60.0;
        opts.innerNodeDistance = 28.0;
        opts.metaMode = "fmmm";
        opts.metaUnitEdgeLength = 80.0;
        opts.interClusterPadding = 20.0;
        std::size_t clusterCount = 0;
        std::size_t interEdges = 0;
        runClusteredByAppLayout(opts, clusteredNodes, edges, attributes, clusterCount, interEdges); for (const auto& cn : clusteredNodes) metadata.clusterByModelId[cn.modelId] = cn.appLabel;
        applyUpwardSurrogateGeometry(nodes, edges, attributes, mode == "upward_layer_based");
        const std::string clusterSource = useAppClusters ? "appLabel" : "graphStructure";
        metadata.actualMode = mode;
        metadata.actualAlgorithm =
          "ClusteredLayout(source=" + clusterSource
          + ", inner=SugiyamaLayout + BarycenterHeuristic, meta=FMMM(weighted), upwardProjection, clusters="
          + std::to_string(clusterCount) + ", interClusterEdges=" + std::to_string(interEdges) + ")";
        metadata.strategy = "clustered";
        metadata.strategyReason =
          "per-cluster Sugiyama with upward projection; FMMM weighted meta-layout pulls coupled clusters tight";
        return metadata;
      }
    }

    runSugiyamaLayout("hierarchical_barycenter", attributes);
    applyUpwardSurrogateGeometry(nodes, edges, attributes, mode == "upward_layer_based");
    metadata.actualMode = mode;
    metadata.actualAlgorithm = mode == "upward_layer_based"
      ? "DjangoErdLayerBasedUPRSurrogate(SugiyamaLayout + BarycenterHeuristic, upwardProjection)"
      : "DjangoErdUpwardPlanarizationSurrogate(SugiyamaLayout + BarycenterHeuristic, upwardProjection)";
    metadata.strategy = "surrogate";
    metadata.strategyReason =
      "UpwardPlanarizationLayout is unstable on cyclic or disconnected ERD graphs, so ERD mode uses a bounded Sugiyama base with upward projection";
    return metadata;
  }

  if (mode == "visibility") {
    runSugiyamaLayout("hierarchical", attributes);
    applyVisibilitySurrogateGeometry(nodes, edges, attributes);
    metadata.actualMode = "visibility";
    metadata.actualAlgorithm =
      "DjangoErdVisibilitySurrogate(SugiyamaLayout + MedianHeuristic, visibilityGridRouting)";
    metadata.strategy = "surrogate";
    metadata.strategyReason =
      "VisibilityLayout is unstable on cyclic or disconnected ERD graphs, so ERD mode uses a bounded Sugiyama base with grid visibility routing";
    return metadata;
  }

  if (mode == "cluster_planarization" || mode == "cluster_ortho") {
    applyClusterSurrogateGeometry(nodes, edges, attributes, mode == "cluster_ortho");
    metadata.actualMode = mode;
    metadata.actualAlgorithm = mode == "cluster_ortho"
      ? "DjangoErdClusterOrthoSurrogate(app-prefix clusters, orthogonalGridRouting)"
      : "DjangoErdClusterPlanarizationSurrogate(app-prefix clusters, packedClusterLayout)";
    metadata.strategy = "cluster_surrogate";
    metadata.strategyReason =
      "the extension provides ERD app-prefix clusters, while OGDF cluster layouts require an explicit ClusterGraph";
    return metadata;
  }

  if (mode == "uml_ortho") {
    if (nodes.size() >= kTopologySurrogateNodeThreshold) {
      runSugiyamaLayout("hierarchical_barycenter", attributes);
      applyOrthogonalSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "uml_ortho";
      metadata.actualAlgorithm =
        "DjangoErdUmlOrthoSurrogate(SugiyamaLayout + BarycenterHeuristic, umlOrthogonalProjection)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        nodeThresholdReason(
          kTopologySurrogateNodeThreshold,
          "UML orthogonal layout uses a bounded Sugiyama base snapped to orthogonal routing");
      return metadata;
    }

    ogdf::PlanarizationLayoutUML layout;
    layout.setPlanarLayouter(new ogdf::OrthoLayoutUML());
    layout.call(attributes);
    metadata.actualAlgorithm = "PlanarizationLayoutUML + OrthoLayoutUML";
    return metadata;
  }

  if (mode == "uml_planarization") {
    if (nodes.size() >= kTopologySurrogateNodeThreshold) {
      runSugiyamaLayout("hierarchical_barycenter", attributes);
      applyUmlPlanarSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "uml_planarization";
      metadata.actualAlgorithm =
        "DjangoErdUmlPlanarizationSurrogate(SugiyamaLayout + BarycenterHeuristic, umlPlanarProjection)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        nodeThresholdReason(
          kTopologySurrogateNodeThreshold,
          "UML planarization uses a bounded Sugiyama base plus planar skewing");
      return metadata;
    }

    ogdf::PlanarizationLayoutUML layout;
    layout.call(attributes);
    return metadata;
  }

  if (mode == "tree" || mode == "radial_tree") {
    runProjectedTreeLayout(mode, nodes, edges, attributes);
    metadata.actualAlgorithm = mode == "radial_tree"
      ? "DjangoErdProjectedRadialForestLayout"
      : "DjangoErdProjectedLayeredForestLayout";
    metadata.strategy = "projected_forest";
    metadata.strategyReason =
      "input graph can be cyclic or disconnected, so a spanning forest is laid out";
    return metadata;
  }

  throw std::runtime_error("unsupported mode: " + mode);
}

void updateBounds(Bounds& bounds, double x, double y, bool& hasPoint) {
  if (!hasPoint) {
    bounds.minX = x;
    bounds.minY = y;
    hasPoint = true;
    return;
  }

  bounds.minX = std::min(bounds.minX, x);
  bounds.minY = std::min(bounds.minY, y);
}

Bounds measureBounds(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes) {
  Bounds bounds;
  bool hasPoint = false;

  for (const NodeRecord& node : nodes) {
    const double width = sanitizeNodeWidth(node, attributes);
    const double height = sanitizeNodeHeight(node, attributes);
    const double centerX = sanitizeNodeCenterX(node, attributes);
    const double centerY = sanitizeNodeCenterY(node, attributes);
    updateBounds(
      bounds,
      centerX - width / 2.0,
      centerY - height / 2.0,
      hasPoint);
  }

  for (const std::vector<RoutePoint>& route : routes) {
    for (const RoutePoint& point : route) {
      updateBounds(bounds, point.x, point.y, hasPoint);
    }
  }

  if (!hasPoint) {
    bounds.minX = 0.0;
    bounds.minY = 0.0;
  }

  return bounds;
}

std::vector<std::vector<RoutePoint>> routeAllEdges(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  bool avoidLaneOverlaps) {
  std::vector<std::vector<RoutePoint>> routes;
  routes.reserve(edges.size());
  RouteOccupancy occupancy;
  RouteOccupancy* occupancyPtr = avoidLaneOverlaps ? &occupancy : nullptr;
  const Rect graphBounds = graphNodeBounds(nodes, attributes);
  constexpr double obstacleMargin = kRoutingObstacleMargin;

  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    const LineIntent line = makeLineIntent(edges[edgeIndex], edgeIndex, attributes);
    const std::vector<NodeObstacle> obstacles =
      makeNodeObstacles(nodes, attributes, obstacleMargin, line.sourceHandle, line.targetHandle);
    routes.push_back(routeObstacleAwareLine(line, graphBounds, obstacles, occupancyPtr));
    if (occupancyPtr != nullptr) {
      recordRouteOccupancy(routes.back(), line, *occupancyPtr);
    }
  }

  return routes;
}

RoutePoint straightPortOnRect(const Rect& rect, const Rect& target) {
  const double centerX = rectCenterX(rect);
  const double centerY = rectCenterY(rect);
  double dx = rectCenterX(target) - centerX;
  double dy = rectCenterY(target) - centerY;
  if (std::abs(dx) < 0.01 && std::abs(dy) < 0.01) {
    dx = 1.0;
    dy = 0.0;
  }

  const double halfWidth = std::max(1.0, rectWidth(rect) / 2.0);
  const double halfHeight = std::max(1.0, rectHeight(rect) / 2.0);
  const double scaleX = std::abs(dx) < 0.01
    ? std::numeric_limits<double>::infinity()
    : halfWidth / std::abs(dx);
  const double scaleY = std::abs(dy) < 0.01
    ? std::numeric_limits<double>::infinity()
    : halfHeight / std::abs(dy);
  const double scale = std::min(scaleX, scaleY);
  return {
    std::round((centerX + dx * scale) * 100.0) / 100.0,
    std::round((centerY + dy * scale) * 100.0) / 100.0,
  };
}

int sideOfPortOnRect(const RoutePoint& point, const Rect& rect) {
  const double tol =
    std::max(1.0, 0.01 * std::max(rectWidth(rect), rectHeight(rect)));
  const double dTop = std::abs(point.y - rect.top);
  const double dRight = std::abs(point.x - rect.right);
  const double dBottom = std::abs(point.y - rect.bottom);
  const double dLeft = std::abs(point.x - rect.left);
  const double minD = std::min({dTop, dRight, dBottom, dLeft});
  if (minD > tol) {
    return -1;
  }
  if (dTop == minD) return 0;
  if (dRight == minD) return 1;
  if (dBottom == minD) return 2;
  return 3;
}

RoutePoint slidePortOnRectSide(
  const RoutePoint& point,
  const Rect& rect,
  int side) {
  switch (side) {
    case 0:
      return {clampToSpan(point.x, rect.left, rect.right), rect.top};
    case 1:
      return {rect.right, clampToSpan(point.y, rect.top, rect.bottom)};
    case 2:
      return {clampToSpan(point.x, rect.left, rect.right), rect.bottom};
    case 3:
      return {rect.left, clampToSpan(point.y, rect.top, rect.bottom)};
    default: {
      const double cx = rectCenterX(rect);
      const double cy = rectCenterY(rect);
      const double dx = point.x - cx;
      const double dy = point.y - cy;
      if (std::abs(dx) >= std::abs(dy)) {
        return {
          dx < 0.0 ? rect.left : rect.right,
          clampToSpan(point.y, rect.top, rect.bottom),
        };
      }
      return {
        clampToSpan(point.x, rect.left, rect.right),
        dy < 0.0 ? rect.top : rect.bottom,
      };
    }
  }
}

std::vector<RoutePoint> routeStraightLine(const LineIntent& line) {
  return compressRoutePoints({
    straightPortOnRect(line.sourceRect, line.targetRect),
    straightPortOnRect(line.targetRect, line.sourceRect),
  });
}

std::vector<std::vector<RoutePoint>> routeAllEdgesStraight(
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  std::vector<std::vector<RoutePoint>> routes;
  routes.reserve(edges.size());

  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    routes.push_back(routeStraightLine(makeLineIntent(edges[edgeIndex], edgeIndex, attributes)));
  }

  // Apply lane offsets to parallel edges so visually overlapping straight
  // segments fan out. Edges sharing the same {source, target} pair (regardless
  // of direction) get evenly spaced perpendicular offsets.
  std::map<std::pair<std::string, std::string>, std::vector<std::size_t>> byPair;
  for (std::size_t i = 0; i < edges.size(); ++i) {
    const std::string& s = edges[i].sourceModelId;
    const std::string& t = edges[i].targetModelId;
    if (s.empty() || t.empty() || s == t) continue;
    auto key = s < t ? std::make_pair(s, t) : std::make_pair(t, s);
    byPair[key].push_back(i);
  }

  constexpr double kLaneSpacing = 12.0;
  for (const auto& [_, indices] : byPair) {
    if (indices.size() < 2) continue;
    const double centerOffset = (static_cast<double>(indices.size()) - 1.0) / 2.0;
    for (std::size_t k = 0; k < indices.size(); ++k) {
      const std::size_t edgeIdx = indices[k];
      auto& route = routes[edgeIdx];
      if (route.size() != 2) continue;
      const double dx = route[1].x - route[0].x;
      const double dy = route[1].y - route[0].y;
      const double len = std::sqrt(dx * dx + dy * dy);
      if (len < 1e-3) continue;
      // Perpendicular unit vector (right-hand rule).
      const double nx = -dy / len;
      const double ny = dx / len;
      const double offset = (static_cast<double>(k) - centerOffset) * kLaneSpacing;
      const EdgeRecord& edge = edges[edgeIdx];
      const Rect sourceRect = handleRect(edge.sourceHandle, attributes);
      const Rect targetRect = handleRect(edge.targetHandle, attributes);
      const int sourceSide = sideOfPortOnRect(route[0], sourceRect);
      const int targetSide = sideOfPortOnRect(route[1], targetRect);
      const RoutePoint sourcePort = slidePortOnRectSide(
        {route[0].x + nx * offset, route[0].y + ny * offset},
        sourceRect,
        sourceSide);
      const RoutePoint targetPort = slidePortOnRectSide(
        {route[1].x + nx * offset, route[1].y + ny * offset},
        targetRect,
        targetSide);
      route[0].x = std::round(sourcePort.x * 100.0) / 100.0;
      route[0].y = std::round(sourcePort.y * 100.0) / 100.0;
      route[1].x = std::round(targetPort.x * 100.0) / 100.0;
      route[1].y = std::round(targetPort.y * 100.0) / 100.0;
    }
  }

  // Obstacle-aware lateral nudge: for any edge whose straight line passes
  // through a non-endpoint node bbox, try a small perpendicular shift to
  // clear the obstacle. Keeps edges 2-point straight while sliding ports
  // along the node boundary, so endpoints stay visually attached.
  struct ObstacleBox {
    double minX, minY, maxX, maxY;
    ogdf::node handle;
  };
  std::vector<ObstacleBox> obstacles;
  const ogdf::Graph& G = attributes.constGraph();
  for (ogdf::node v : G.nodes) {
    const double cx = attributes.x(v);
    const double cy = attributes.y(v);
    const double hw = attributes.width(v) / 2.0;
    const double hh = attributes.height(v) / 2.0;
    obstacles.push_back({cx - hw, cy - hh, cx + hw, cy + hh, v});
  }

  auto segmentsCross = [](double ax, double ay, double bx, double by,
                          double cx, double cy, double dx2, double dy2) -> bool {
    const double d1x = bx - ax, d1y = by - ay;
    const double d2x = dx2 - cx, d2y = dy2 - cy;
    const double denom = d1x * d2y - d1y * d2x;
    if (std::abs(denom) < 1e-9) return false;
    const double t = ((cx - ax) * d2y - (cy - ay) * d2x) / denom;
    const double s = ((cx - ax) * d1y - (cy - ay) * d1x) / denom;
    return t > 1e-9 && t < 1.0 - 1e-9 && s > 1e-9 && s < 1.0 - 1e-9;
  };

  auto lineHitsBox = [&](double sx, double sy, double tx, double ty,
                          const ObstacleBox& b, double margin) -> bool {
    const double bx0 = b.minX - margin;
    const double bx1 = b.maxX + margin;
    const double by0 = b.minY - margin;
    const double by1 = b.maxY + margin;
    if (std::max(sx, tx) < bx0 || std::min(sx, tx) > bx1) return false;
    if (std::max(sy, ty) < by0 || std::min(sy, ty) > by1) return false;
    if (sx > bx0 && sx < bx1 && sy > by0 && sy < by1) return true;
    if (tx > bx0 && tx < bx1 && ty > by0 && ty < by1) return true;
    return segmentsCross(sx, sy, tx, ty, bx0, by0, bx1, by0)
        || segmentsCross(sx, sy, tx, ty, bx1, by0, bx1, by1)
        || segmentsCross(sx, sy, tx, ty, bx1, by1, bx0, by1)
        || segmentsCross(sx, sy, tx, ty, bx0, by1, bx0, by0);
  };

  for (std::size_t i = 0; i < edges.size(); ++i) {
    auto& route = routes[i];
    if (route.size() != 2) continue;
    const ogdf::node srcH = edges[i].sourceHandle;
    const ogdf::node tgtH = edges[i].targetHandle;

    auto countHits = [&](double sx, double sy, double tx, double ty) -> int {
      int hits = 0;
      for (const auto& b : obstacles) {
        if (b.handle == srcH || b.handle == tgtH) continue;
        if (lineHitsBox(sx, sy, tx, ty, b, /*margin=*/0.0)) ++hits;
      }
      return hits;
    };

    const int baseHits = countHits(route[0].x, route[0].y, route[1].x, route[1].y);
    if (baseHits == 0) continue;

    const double dx = route[1].x - route[0].x;
    const double dy = route[1].y - route[0].y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3) continue;
    const double nx = -dy / len;
    const double ny = dx / len;

    const Rect srcRect = handleRect(srcH, attributes);
    const Rect tgtRect = handleRect(tgtH, attributes);
    const int srcSide = sideOfPortOnRect(route[0], srcRect);
    const int tgtSide = sideOfPortOnRect(route[1], tgtRect);

    const double tries[] = {-12, 12, -24, 24, -36, 36};
    int bestHits = baseHits;
    double bestOff = 0.0;
    for (double off : tries) {
      RoutePoint a = slidePortOnRectSide(
        {route[0].x + nx * off, route[0].y + ny * off},
        srcRect,
        srcSide);
      RoutePoint b = slidePortOnRectSide(
        {route[1].x + nx * off, route[1].y + ny * off},
        tgtRect,
        tgtSide);
      const int hits = countHits(a.x, a.y, b.x, b.y);
      if (hits < bestHits) {
        bestHits = hits;
        bestOff = off;
        if (hits == 0) break;
      }
    }
    if (bestOff != 0.0) {
      RoutePoint a = slidePortOnRectSide(
        {route[0].x + nx * bestOff, route[0].y + ny * bestOff},
        srcRect,
        srcSide);
      RoutePoint b = slidePortOnRectSide(
        {route[1].x + nx * bestOff, route[1].y + ny * bestOff},
        tgtRect,
        tgtSide);
      route[0].x = std::round(a.x * 100.0) / 100.0;
      route[0].y = std::round(a.y * 100.0) / 100.0;
      route[1].x = std::round(b.x * 100.0) / 100.0;
      route[1].y = std::round(b.y * 100.0) / 100.0;
    }
  }

  return routes;
}

std::vector<RoutePoint> routeStraightWithDetour(
  const LineIntent& line,
  const std::vector<NodeObstacle>& obstacles,
  int maxDetours) {
  constexpr double kDetourMargin = 24.0;
  const RoutePoint sourcePort = straightPortOnRect(line.sourceRect, line.targetRect);
  const RoutePoint targetPort = straightPortOnRect(line.targetRect, line.sourceRect);
  std::vector<RoutePoint> path = {sourcePort, targetPort};

  for (int iter = 0; iter < maxDetours; ++iter) {
    std::size_t blockedIndex = path.size();
    const NodeObstacle* blocker = nullptr;

    for (std::size_t segIndex = 0; segIndex + 1 < path.size(); ++segIndex) {
      for (const NodeObstacle& obstacle : obstacles) {
        if (segmentIntersectsRect(path[segIndex], path[segIndex + 1], obstacle.rect)) {
          blockedIndex = segIndex;
          blocker = &obstacle;
          break;
        }
      }
      if (blocker != nullptr) {
        break;
      }
    }

    if (blocker == nullptr) {
      break;
    }

    const RoutePoint segStart = path[blockedIndex];
    const RoutePoint segEnd = path[blockedIndex + 1];
    const Rect& rect = blocker->rect;
    const double dx = segEnd.x - segStart.x;
    const double dy = segEnd.y - segStart.y;
    const bool horizontal = std::abs(dx) >= std::abs(dy);

    RoutePoint optA;
    RoutePoint optB;
    if (horizontal) {
      const double midX = std::clamp(
        rectCenterX(rect),
        std::min(segStart.x, segEnd.x),
        std::max(segStart.x, segEnd.x));
      optA = {midX, rect.top - kDetourMargin};
      optB = {midX, rect.bottom + kDetourMargin};
    } else {
      const double midY = std::clamp(
        rectCenterY(rect),
        std::min(segStart.y, segEnd.y),
        std::max(segStart.y, segEnd.y));
      optA = {rect.left - kDetourMargin, midY};
      optB = {rect.right + kDetourMargin, midY};
    }

    const double costA = std::hypot(optA.x - segStart.x, optA.y - segStart.y)
      + std::hypot(segEnd.x - optA.x, segEnd.y - optA.y);
    const double costB = std::hypot(optB.x - segStart.x, optB.y - segStart.y)
      + std::hypot(segEnd.x - optB.x, segEnd.y - optB.y);
    const RoutePoint chosen = (costA <= costB) ? optA : optB;
    path.insert(path.begin() + static_cast<long>(blockedIndex + 1), chosen);
  }

  return compressRoutePoints(std::move(path));
}

std::vector<std::vector<RoutePoint>> routeAllEdgesStraightSmart(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  std::vector<std::vector<RoutePoint>> routes;
  routes.reserve(edges.size());

  constexpr int kMaxDetoursPerEdge = 24;

  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    const EdgeRecord& edge = edges[edgeIndex];
    const LineIntent line = makeLineIntent(edge, edgeIndex, attributes);
    const std::vector<NodeObstacle> obstacles = makeNodeObstacles(
      nodes, attributes, 0.0, edge.sourceHandle, edge.targetHandle);
    routes.push_back(routeStraightWithDetour(line, obstacles, kMaxDetoursPerEdge));
  }

  return routes;
}

std::vector<std::vector<RoutePoint>> routeAllEdgesCrossAware(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  std::vector<std::vector<RoutePoint>> routes(edges.size());

  std::unordered_map<std::string, std::size_t> idToIdx;
  idToIdx.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    idToIdx[nodes[i].modelId] = i;
  }

  // Layout bounds with padding.
  double minX = std::numeric_limits<double>::infinity();
  double maxX = -std::numeric_limits<double>::infinity();
  double minY = minX;
  double maxY = maxX;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const double cx = attributes.x(nodes[i].handle);
    const double cy = attributes.y(nodes[i].handle);
    const double hw = nodes[i].width / 2.0;
    const double hh = nodes[i].height / 2.0;
    if (cx - hw < minX) minX = cx - hw;
    if (cx + hw > maxX) maxX = cx + hw;
    if (cy - hh < minY) minY = cy - hh;
    if (cy + hh > maxY) maxY = cy + hh;
  }
  const double padX = (maxX - minX) * 0.05 + 100.0;
  const double padY = (maxY - minY) * 0.05 + 100.0;
  minX -= padX; maxX += padX;
  minY -= padY; maxY += padY;

  constexpr int GW = 250;
  constexpr int GH = 200;
  const double rangeX = maxX - minX;
  const double rangeY = maxY - minY;
  const double cellW = rangeX > 0 ? rangeX / GW : 1.0;
  const double cellH = rangeY > 0 ? rangeY / GH : 1.0;

  auto worldToCell = [&](double x, double y) {
    int gx = static_cast<int>((x - minX) / cellW);
    int gy = static_cast<int>((y - minY) / cellH);
    if (gx < 0) gx = 0; else if (gx >= GW) gx = GW - 1;
    if (gy < 0) gy = 0; else if (gy >= GH) gy = GH - 1;
    return std::make_pair(gx, gy);
  };
  auto cellToWorld = [&](int gx, int gy) {
    return std::make_pair(minX + (gx + 0.5) * cellW,
                          minY + (gy + 0.5) * cellH);
  };

  // Mark cells inside any node's bbox. Routing into these cells is
  // forbidden unless the edge endpoint is THIS node (allowed near src/tgt).
  std::vector<std::vector<bool>> nodeOccupied(GW, std::vector<bool>(GH, false));
  std::vector<std::vector<int>> ownerNode(GW, std::vector<int>(GH, -1));
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const double cx = attributes.x(nodes[i].handle);
    const double cy = attributes.y(nodes[i].handle);
    const double hw = nodes[i].width / 2.0;
    const double hh = nodes[i].height / 2.0;
    auto [gx0, gy0] = worldToCell(cx - hw, cy - hh);
    auto [gx1, gy1] = worldToCell(cx + hw, cy + hh);
    for (int x = gx0; x <= gx1; ++x) {
      for (int y = gy0; y <= gy1; ++y) {
        nodeOccupied[x][y] = true;
        ownerNode[x][y] = static_cast<int>(i);
      }
    }
  }

  std::vector<std::vector<int>> density(GW, std::vector<int>(GH, 0));

  // Order edges by length desc.
  std::vector<std::pair<double, std::size_t>> edgeByLen;
  edgeByLen.reserve(edges.size());
  for (std::size_t e = 0; e < edges.size(); ++e) {
    auto sIt = idToIdx.find(edges[e].sourceModelId);
    auto tIt = idToIdx.find(edges[e].targetModelId);
    if (sIt == idToIdx.end() || tIt == idToIdx.end()) {
      edgeByLen.emplace_back(0.0, e);
      continue;
    }
    const std::size_t a = sIt->second;
    const std::size_t b = tIt->second;
    const double dx = attributes.x(nodes[b].handle) - attributes.x(nodes[a].handle);
    const double dy = attributes.y(nodes[b].handle) - attributes.y(nodes[a].handle);
    edgeByLen.emplace_back(std::hypot(dx, dy), e);
  }
  std::sort(edgeByLen.begin(), edgeByLen.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

  struct OpenEntry {
    double f;
    int gx, gy;
    bool operator<(const OpenEntry& o) const { return f > o.f; }  // min-heap
  };

  auto cellKey = [](int x, int y) { return y * GW + x; };

  std::size_t fallbacks = 0;
  for (const auto& kv : edgeByLen) {
    const std::size_t e = kv.second;
    auto sIt = idToIdx.find(edges[e].sourceModelId);
    auto tIt = idToIdx.find(edges[e].targetModelId);
    if (sIt == idToIdx.end() || tIt == idToIdx.end()
        || sIt->second == tIt->second) {
      routes[e] = {};
      continue;
    }
    const std::size_t srcN = sIt->second;
    const std::size_t tgtN = tIt->second;
    const double sx = attributes.x(nodes[srcN].handle);
    const double sy = attributes.y(nodes[srcN].handle);
    const double tx = attributes.x(nodes[tgtN].handle);
    const double ty = attributes.y(nodes[tgtN].handle);
    auto [sgx, sgy] = worldToCell(sx, sy);
    auto [tgx, tgy] = worldToCell(tx, ty);

    std::priority_queue<OpenEntry> open;
    std::unordered_map<int, double> gScore;
    std::unordered_map<int, int> parent;

    gScore[cellKey(sgx, sgy)] = 0.0;
    open.push({std::hypot(static_cast<double>(tgx - sgx),
                          static_cast<double>(tgy - sgy)), sgx, sgy});

    bool found = false;
    while (!open.empty()) {
      const auto cur = open.top();
      open.pop();
      const int gx = cur.gx;
      const int gy = cur.gy;
      if (gx == tgx && gy == tgy) { found = true; break; }
      const int curKey = cellKey(gx, gy);
      const double curG = gScore[curKey];
      // Check if entry is stale (better path found earlier).
      const double h = std::hypot(static_cast<double>(tgx - gx),
                                   static_cast<double>(tgy - gy));
      if (cur.f > curG + h + 0.001) continue;

      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) continue;
          const int nx = gx + dx;
          const int ny = gy + dy;
          if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) continue;

          // Forbid cells inside non-endpoint nodes.
          if (nodeOccupied[nx][ny]) {
            const int owner = ownerNode[nx][ny];
            if (owner != static_cast<int>(srcN)
                && owner != static_cast<int>(tgtN)) continue;
          }

          const double stepCost = (dx != 0 && dy != 0) ? 1.41421356 : 1.0;
          const double densityCost = static_cast<double>(density[nx][ny]) * 0.5;
          const double tentativeG = curG + stepCost + densityCost;

          const int nKey = cellKey(nx, ny);
          auto gIt = gScore.find(nKey);
          if (gIt == gScore.end() || tentativeG < gIt->second) {
            gScore[nKey] = tentativeG;
            parent[nKey] = curKey;
            const double hN = std::hypot(static_cast<double>(tgx - nx),
                                          static_cast<double>(tgy - ny));
            open.push({tentativeG + hN, nx, ny});
          }
        }
      }
    }

    std::vector<std::pair<int, int>> cellPath;
    if (found) {
      int curKey = cellKey(tgx, tgy);
      const int srcKey = cellKey(sgx, sgy);
      while (curKey != srcKey) {
        const int gx = curKey % GW;
        const int gy = curKey / GW;
        cellPath.emplace_back(gx, gy);
        auto pIt = parent.find(curKey);
        if (pIt == parent.end()) break;
        curKey = pIt->second;
      }
      cellPath.emplace_back(sgx, sgy);
      std::reverse(cellPath.begin(), cellPath.end());
    } else {
      ++fallbacks;
      cellPath = {{sgx, sgy}, {tgx, tgy}};
    }

    // Build polyline: exact src endpoint, intermediate cell centres, exact tgt endpoint.
    std::vector<RoutePoint> route;
    route.push_back({sx, sy});
    for (std::size_t i = 1; i + 1 < cellPath.size(); ++i) {
      const auto [gx, gy] = cellPath[i];
      const auto [wx, wy] = cellToWorld(gx, gy);
      route.push_back({wx, wy});
    }
    route.push_back({tx, ty});

    // Simplify near-collinear waypoints. Remove waypoint b if its
    // perpendicular distance from line a→c is < cellW × 0.4.
    if (route.size() >= 3) {
      std::vector<RoutePoint> simplified;
      simplified.push_back(route.front());
      for (std::size_t i = 1; i + 1 < route.size(); ++i) {
        const auto& a = simplified.back();
        const auto& b = route[i];
        const auto& c = route[i + 1];
        const double crossV = (b.x - a.x) * (c.y - a.y)
                             - (b.y - a.y) * (c.x - a.x);
        const double segLen = std::hypot(c.x - a.x, c.y - a.y);
        const double dist = (segLen > 1e-3)
            ? std::abs(crossV) / segLen : 0.0;
        if (dist > cellW * 0.4) {
          simplified.push_back(b);
        }
      }
      simplified.push_back(route.back());
      route = std::move(simplified);
    }

    routes[e] = route;

    // Update density along path so subsequent edges avoid this corridor.
    for (std::size_t i = 1; i < route.size(); ++i) {
      auto [g0x, g0y] = worldToCell(route[i - 1].x, route[i - 1].y);
      auto [g1x, g1y] = worldToCell(route[i].x, route[i].y);
      int dx = std::abs(g1x - g0x);
      int dy = std::abs(g1y - g0y);
      int sx2 = g0x < g1x ? 1 : -1;
      int sy2 = g0y < g1y ? 1 : -1;
      int err = dx - dy;
      int x = g0x, y = g0y;
      while (true) {
        density[x][y] += 1;
        if (x == g1x && y == g1y) break;
        const int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x += sx2; }
        if (e2 < dx) { err += dx; y += sy2; }
      }
    }
  }

  std::fprintf(stderr,
    "[cross-aware-routing] Routed %zu edges via A* on %dx%d grid (%zu fallbacks).\n",
    edges.size(), GW, GH, fallbacks);

  return routes;
}

int axisForNeighbor(double dx, double dy) {
  if (std::abs(dx) >= std::abs(dy)) {
    return dx >= 0.0 ? 0 : 1;
  }
  return dy >= 0.0 ? 3 : 2;
}

void placeAxisGroup(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::size_t>& group,
  int axis,
  double hubX,
  double hubY,
  double axisDistance,
  double slotGapX,
  double slotGapY,
  ogdf::GraphAttributes& attributes) {
  if (group.empty()) {
    return;
  }

  const double middle = (static_cast<double>(group.size()) - 1.0) / 2.0;
  for (std::size_t index = 0; index < group.size(); ++index) {
    const NodeRecord& node = nodes[group[index]];
    const double offset = static_cast<double>(index) - middle;

    if (axis == 0) {
      attributes.x(node.handle) = hubX + axisDistance;
      attributes.y(node.handle) = hubY + offset * slotGapY;
    } else if (axis == 1) {
      attributes.x(node.handle) = hubX - axisDistance;
      attributes.y(node.handle) = hubY + offset * slotGapY;
    } else if (axis == 2) {
      attributes.x(node.handle) = hubX + offset * slotGapX;
      attributes.y(node.handle) = hubY - axisDistance;
    } else {
      attributes.x(node.handle) = hubX + offset * slotGapX;
      attributes.y(node.handle) = hubY + axisDistance;
    }
  }
}

void refineStraightHubAxisLayout(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 3 || edges.empty()) {
    return;
  }

  const std::vector<std::vector<std::size_t>> adjacency = buildUndirectedAdjacency(nodes, edges);
  std::vector<std::vector<std::size_t>> components = collectConnectedComponents(nodes, edges);

  for (const std::vector<std::size_t>& component : components) {
    if (component.size() <= 3) {
      continue;
    }

    const std::size_t hubIndex = *std::max_element(
      component.begin(),
      component.end(),
      [&](std::size_t left, std::size_t right) {
        if (adjacency[left].size() != adjacency[right].size()) {
          return adjacency[left].size() < adjacency[right].size();
        }
        return nodes[left].modelId > nodes[right].modelId;
      });
    const std::size_t hubDegree = adjacency[hubIndex].size();
    const std::size_t degreeThreshold = std::max<std::size_t>(
      4,
      static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(component.size())))));
    if (hubDegree < degreeThreshold) {
      continue;
    }

    double centerX = 0.0;
    double centerY = 0.0;
    double averageWidth = 0.0;
    double averageHeight = 0.0;
    for (std::size_t nodeIndex : component) {
      centerX += sanitizeNodeCenterX(nodes[nodeIndex], attributes);
      centerY += sanitizeNodeCenterY(nodes[nodeIndex], attributes);
      averageWidth += sanitizeNodeWidth(nodes[nodeIndex], attributes);
      averageHeight += sanitizeNodeHeight(nodes[nodeIndex], attributes);
    }
    centerX /= static_cast<double>(component.size());
    centerY /= static_cast<double>(component.size());
    averageWidth /= static_cast<double>(component.size());
    averageHeight /= static_cast<double>(component.size());

    const NodeRecord& hub = nodes[hubIndex];
    const double hubX = centerX;
    const double hubY = centerY;
    attributes.x(hub.handle) = hubX;
    attributes.y(hub.handle) = hubY;

    std::vector<bool> inComponent(nodes.size(), false);
    for (std::size_t nodeIndex : component) {
      inComponent[nodeIndex] = true;
    }

    std::vector<std::size_t> axisGroups[4];
    for (std::size_t neighbor : adjacency[hubIndex]) {
      if (!inComponent[neighbor]) {
        continue;
      }
      const double dx = sanitizeNodeCenterX(nodes[neighbor], attributes)
        - sanitizeNodeCenterX(hub, attributes);
      const double dy = sanitizeNodeCenterY(nodes[neighbor], attributes)
        - sanitizeNodeCenterY(hub, attributes);
      axisGroups[axisForNeighbor(dx, dy)].push_back(neighbor);
    }

    for (int axis = 0; axis < 4; ++axis) {
      std::sort(
        axisGroups[axis].begin(),
        axisGroups[axis].end(),
        [&](std::size_t left, std::size_t right) {
          const double leftPrimary = axis < 2
            ? sanitizeNodeCenterY(nodes[left], attributes)
            : sanitizeNodeCenterX(nodes[left], attributes);
          const double rightPrimary = axis < 2
            ? sanitizeNodeCenterY(nodes[right], attributes)
            : sanitizeNodeCenterX(nodes[right], attributes);
          if (std::abs(leftPrimary - rightPrimary) > 0.01) {
            return leftPrimary < rightPrimary;
          }
          return adjacency[left].size() > adjacency[right].size();
        });
    }

    const double slotGapX = std::max(averageWidth + 96.0, 180.0);
    const double slotGapY = std::max(averageHeight + 78.0, 150.0);
    const double axisDistance = std::max(
      360.0,
      std::max(sanitizeNodeWidth(hub, attributes), sanitizeNodeHeight(hub, attributes)) + 260.0);
    for (int axis = 0; axis < 4; ++axis) {
      placeAxisGroup(
        nodes,
        axisGroups[axis],
        axis,
        hubX,
        hubY,
        axisDistance,
        slotGapX,
        slotGapY,
        attributes);
    }
  }

  enforceNodeSeparationStrong(nodes, attributes);
}

double capShiftVector(double& dx, double& dy, double limit) {
  const double length = std::hypot(dx, dy);
  if (length <= limit || length <= 0.01) {
    return length;
  }

  const double scale = limit / length;
  dx *= scale;
  dy *= scale;
  return limit;
}

std::size_t applyNodeShifts(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  std::vector<double>& shiftX,
  std::vector<double>& shiftY,
  double limit) {
  std::size_t moved = 0;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    double dx = shiftX[index];
    double dy = shiftY[index];
    const double length = capShiftVector(dx, dy, limit);
    if (length <= 0.05) {
      continue;
    }
    attributes.x(nodes[index].handle) = sanitizeNodeCenterX(nodes[index], attributes) + dx;
    attributes.y(nodes[index].handle) = sanitizeNodeCenterY(nodes[index], attributes) + dy;
    moved += 1;
  }
  return moved;
}

std::size_t repelNodesFromStraightEdgeCorridors(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  if (nodes.empty() || edges.empty()) {
    return 0;
  }

  std::vector<double> shiftX(nodes.size(), 0.0);
  std::vector<double> shiftY(nodes.size(), 0.0);
  constexpr double corridorClearance = 72.0;

  for (const EdgeRecord& edge : edges) {
    const double sourceX = attributes.x(edge.sourceHandle);
    const double sourceY = attributes.y(edge.sourceHandle);
    const double targetX = attributes.x(edge.targetHandle);
    const double targetY = attributes.y(edge.targetHandle);
    const double edgeX = targetX - sourceX;
    const double edgeY = targetY - sourceY;
    const double lengthSquared = edgeX * edgeX + edgeY * edgeY;
    if (lengthSquared <= 1.0) {
      continue;
    }
    const double length = std::sqrt(lengthSquared);

    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
      const NodeRecord& node = nodes[nodeIndex];
      if (node.handle == edge.sourceHandle || node.handle == edge.targetHandle) {
        continue;
      }

      const double centerX = sanitizeNodeCenterX(node, attributes);
      const double centerY = sanitizeNodeCenterY(node, attributes);
      const double projection = (
        (centerX - sourceX) * edgeX + (centerY - sourceY) * edgeY) / lengthSquared;
      if (projection <= 0.02 || projection >= 0.98) {
        continue;
      }

      const double closestX = sourceX + edgeX * projection;
      const double closestY = sourceY + edgeY * projection;
      double awayX = centerX - closestX;
      double awayY = centerY - closestY;
      double distance = std::hypot(awayX, awayY);
      if (distance <= 0.01) {
        awayX = -edgeY / length;
        awayY = edgeX / length;
        distance = 1.0;
      } else {
        awayX /= distance;
        awayY /= distance;
      }

      const double nodeRadius =
        std::hypot(sanitizeNodeWidth(node, attributes), sanitizeNodeHeight(node, attributes)) / 2.0;
      const double clearance = nodeRadius + corridorClearance;
      if (distance >= clearance) {
        continue;
      }

      const double strength = std::min(84.0, (clearance - distance) * 0.18);
      shiftX[nodeIndex] += awayX * strength;
      shiftY[nodeIndex] += awayY * strength;
    }
  }

  return applyNodeShifts(nodes, attributes, shiftX, shiftY, 120.0);
}

std::size_t nudgeNodesFromRouteIntersections(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.empty() || edges.empty()) {
    return 0;
  }

  std::vector<double> shiftX(nodes.size(), 0.0);
  std::vector<double> shiftY(nodes.size(), 0.0);
  constexpr double clearance = 34.0;

  for (std::size_t edgeIndex = 0; edgeIndex < routes.size() && edgeIndex < edges.size(); ++edgeIndex) {
    const EdgeRecord& edge = edges[edgeIndex];
    const std::vector<RoutePoint>& route = routes[edgeIndex];
    for (std::size_t pointIndex = 1; pointIndex < route.size(); ++pointIndex) {
      const RoutePoint& start = route[pointIndex - 1];
      const RoutePoint& end = route[pointIndex];
      const bool vertical = std::abs(start.x - end.x) < 0.01;
      const bool horizontal = std::abs(start.y - end.y) < 0.01;
      if (!vertical && !horizontal) {
        continue;
      }

      for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        const NodeRecord& node = nodes[nodeIndex];
        if (node.handle == edge.sourceHandle || node.handle == edge.targetHandle) {
          continue;
        }

        if (!segmentIntersectsRect(start, end, nodeRect(node, attributes, clearance))) {
          continue;
        }

        if (vertical) {
          const double centerX = sanitizeNodeCenterX(node, attributes);
          const double halfWidth = sanitizeNodeWidth(node, attributes) / 2.0;
          const double direction = centerX >= start.x ? 1.0 : -1.0;
          const double needed = halfWidth + clearance - std::abs(centerX - start.x);
          shiftX[nodeIndex] += direction * std::min(96.0, std::max(18.0, needed * 0.65));
        } else {
          const double centerY = sanitizeNodeCenterY(node, attributes);
          const double halfHeight = sanitizeNodeHeight(node, attributes) / 2.0;
          const double direction = centerY >= start.y ? 1.0 : -1.0;
          const double needed = halfHeight + clearance - std::abs(centerY - start.y);
          shiftY[nodeIndex] += direction * std::min(96.0, std::max(18.0, needed * 0.65));
        }
      }
    }
  }

  return applyNodeShifts(nodes, attributes, shiftX, shiftY, 140.0);
}

void refineConstrainedForceLayout(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  for (int pass = 0; pass < 2; ++pass) {
    for (int iteration = 0; iteration < 3; ++iteration) {
      const std::size_t moved = repelNodesFromStraightEdgeCorridors(nodes, edges, attributes);
      enforceNodeSeparationStrong(nodes, attributes);
      if (moved == 0) {
        break;
      }
    }

    const std::vector<std::vector<RoutePoint>> routes =
      routeAllEdges(nodes, edges, attributes, true);
    const LayoutQualityMetrics quality =
      measureLayoutQuality(nodes, edges, routes, attributes, nullptr);
    if (quality.edgeNodeIntersections == 0) {
      break;
    }

    const std::size_t nudged = nudgeNodesFromRouteIntersections(nodes, edges, routes, attributes);
    enforceNodeSeparationStrong(nodes, attributes);
    compactDistantConnectedNodes(nodes, edges, attributes);
    if (nudged == 0) {
      break;
    }
  }
}

std::vector<EdgeCrossingRecord> detectRouteCrossings(
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  std::vector<std::vector<std::string>>& crossingIdsByEdge,
  std::size_t& totalCrossings) {
  crossingIdsByEdge.assign(edges.size(), {});
  totalCrossings = 0;
  std::vector<EdgeCrossingRecord> crossings;

  for (std::size_t leftIndex = 0; leftIndex < edges.size(); ++leftIndex) {
    if (leftIndex >= routes.size() || routes[leftIndex].size() < 2) {
      continue;
    }

    for (std::size_t rightIndex = leftIndex + 1; rightIndex < edges.size(); ++rightIndex) {
      if (
        rightIndex >= routes.size()
        || routes[rightIndex].size() < 2
        || sharesEndpoint(edges[leftIndex], edges[rightIndex])) {
        continue;
      }

      std::size_t pairCrossingIndex = 0;
      for (std::size_t leftPoint = 1; leftPoint < routes[leftIndex].size(); ++leftPoint) {
        for (std::size_t rightPoint = 1; rightPoint < routes[rightIndex].size(); ++rightPoint) {
          RoutePoint intersection;
          if (!properSegmentIntersection(
              routes[leftIndex][leftPoint - 1],
              routes[leftIndex][leftPoint],
              routes[rightIndex][rightPoint - 1],
              routes[rightIndex][rightPoint],
              intersection)) {
            continue;
          }

          const std::size_t crossingIndex = pairCrossingIndex++;
          totalCrossings += 1;
          if (crossings.size() >= kMaxReportedCrossings) {
            continue;
          }

          const std::string crossingId =
            "cross:" + edges[leftIndex].edgeId + ":" + edges[rightIndex].edgeId + ":"
            + std::to_string(crossingIndex);
          crossingIdsByEdge[leftIndex].push_back(crossingId);
          crossingIdsByEdge[rightIndex].push_back(crossingId);
          crossings.push_back({
            crossingId,
            edges[leftIndex].edgeId,
            intersection,
            edges[rightIndex].edgeId,
          });
        }
      }
    }
  }

  for (std::vector<std::string>& edgeCrossingIds : crossingIdsByEdge) {
    std::sort(edgeCrossingIds.begin(), edgeCrossingIds.end());
  }
  std::sort(
    crossings.begin(),
    crossings.end(),
    [](const EdgeCrossingRecord& left, const EdgeCrossingRecord& right) {
      return left.id < right.id;
    });

  return crossings;
}

}  // namespace djerd
