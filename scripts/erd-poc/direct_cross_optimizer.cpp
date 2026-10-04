// Low-memory straight-line crossing optimizer for real ERD model nodes.
//
// Every input relationship remains an independent source-to-target segment.
// Only model centers move; no proxy, bend, hidden edge, or aggregation is
// created.  Candidate state is evaluated one node at a time, so memory stays
// O(nodes + edges) while compute can continue for as many rounds as useful.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/resource.h>

namespace {

constexpr double kTau = 6.283185307179586476925286766559;

struct Point {
  double x = 0.0;
  double y = 0.0;
};

struct NodeRow {
  std::string id;
  double width = 1.0;
  double height = 1.0;
};

struct Edge {
  std::size_t source = 0;
  std::size_t target = 0;
  std::size_t weight = 1;
};

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t next = line.find('\t', start);
    if (next == std::string::npos) {
      fields.push_back(line.substr(start));
      return fields;
    }
    fields.push_back(line.substr(start, next - start));
    start = next + 1;
  }
}

std::string stringArg(
    char** argv,
    int argc,
    const std::string& name,
    const std::string& fallback = "") {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return argv[index + 1];
  }
  if (!fallback.empty()) return fallback;
  throw std::runtime_error("missing argument: " + name);
}

int intArg(char** argv, int argc, const std::string& name, int fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return std::stoi(argv[index + 1]);
  }
  return fallback;
}

double doubleArg(
    char** argv,
    int argc,
    const std::string& name,
    double fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return std::stod(argv[index + 1]);
  }
  return fallback;
}

long double orientation(const Point& a, const Point& b, const Point& c) {
  return static_cast<long double>(b.x - a.x)
      * static_cast<long double>(c.y - a.y)
    - static_cast<long double>(b.y - a.y)
      * static_cast<long double>(c.x - a.x);
}

bool properCross(const Point& a, const Point& b, const Point& c, const Point& d) {
  const long double first = orientation(a, b, c);
  const long double second = orientation(a, b, d);
  const long double third = orientation(c, d, a);
  const long double fourth = orientation(c, d, b);
  return first != 0.0L && second != 0.0L
    && third != 0.0L && fourth != 0.0L
    && ((first < 0.0L) != (second < 0.0L))
    && ((third < 0.0L) != (fourth < 0.0L));
}

double distance(const Point& left, const Point& right) {
  return std::hypot(left.x - right.x, left.y - right.y);
}

std::size_t fullCrossingCount(
    const std::vector<Edge>& edges,
    const std::vector<Point>& positions) {
  std::size_t count = 0;
  for (std::size_t left = 0; left < edges.size(); ++left) {
    const Edge& first = edges[left];
    for (std::size_t right = left + 1; right < edges.size(); ++right) {
      const Edge& second = edges[right];
      if (
          first.source == second.source || first.source == second.target
          || first.target == second.source || first.target == second.target) {
        continue;
      }
      if (properCross(
            positions[first.source], positions[first.target],
            positions[second.source], positions[second.target])) {
        count += first.weight * second.weight;
      }
    }
  }
  return count;
}

std::size_t localCrossingCount(
    std::size_t node,
    const Point& candidate,
    const std::vector<Edge>& edges,
    const std::vector<std::vector<std::size_t>>& incidentEdges,
    const std::vector<Point>& positions) {
  std::size_t count = 0;
  for (const std::size_t incidentIndex : incidentEdges[node]) {
    const Edge& first = edges[incidentIndex];
    const Point firstSource = first.source == node ? candidate : positions[first.source];
    const Point firstTarget = first.target == node ? candidate : positions[first.target];
    for (std::size_t otherIndex = 0; otherIndex < edges.size(); ++otherIndex) {
      if (otherIndex == incidentIndex) continue;
      const Edge& second = edges[otherIndex];
      if (
          first.source == second.source || first.source == second.target
          || first.target == second.source || first.target == second.target) {
        continue;
      }
      if (properCross(
            firstSource, firstTarget,
            positions[second.source], positions[second.target])) {
        count += first.weight * second.weight;
      }
    }
  }
  return count;
}

Point substitutedPoint(
    std::size_t node,
    std::size_t firstNode,
    const Point& firstPoint,
    std::size_t secondNode,
    const Point& secondPoint,
    const std::vector<Point>& positions) {
  if (node == firstNode) return firstPoint;
  if (node == secondNode) return secondPoint;
  return positions[node];
}

std::size_t pairLocalCrossingCount(
    std::size_t firstNode,
    const Point& firstPoint,
    std::size_t secondNode,
    const Point& secondPoint,
    const std::vector<Edge>& edges,
    const std::vector<std::vector<std::size_t>>& incidentEdges,
    const std::vector<Point>& positions) {
  std::vector<unsigned char> affected(edges.size(), 0);
  std::vector<std::size_t> affectedIndices;
  affectedIndices.reserve(
    incidentEdges[firstNode].size() + incidentEdges[secondNode].size());
  for (const std::size_t edgeIndex : incidentEdges[firstNode]) {
    if (affected[edgeIndex]) continue;
    affected[edgeIndex] = 1;
    affectedIndices.push_back(edgeIndex);
  }
  for (const std::size_t edgeIndex : incidentEdges[secondNode]) {
    if (affected[edgeIndex]) continue;
    affected[edgeIndex] = 1;
    affectedIndices.push_back(edgeIndex);
  }

  std::size_t count = 0;
  for (const std::size_t affectedIndex : affectedIndices) {
    const Edge& first = edges[affectedIndex];
    const Point source = substitutedPoint(
      first.source, firstNode, firstPoint, secondNode, secondPoint, positions);
    const Point target = substitutedPoint(
      first.target, firstNode, firstPoint, secondNode, secondPoint, positions);
    for (std::size_t otherIndex = 0; otherIndex < edges.size(); ++otherIndex) {
      if (
          otherIndex == affectedIndex
          || (affected[otherIndex] && otherIndex < affectedIndex)) {
        continue;
      }
      const Edge& second = edges[otherIndex];
      if (
          first.source == second.source || first.source == second.target
          || first.target == second.source || first.target == second.target) {
        continue;
      }
      if (properCross(
            source,
            target,
            substitutedPoint(
              second.source, firstNode, firstPoint, secondNode, secondPoint, positions),
            substitutedPoint(
              second.target, firstNode, firstPoint, secondNode, secondPoint, positions))) {
        count += first.weight * second.weight;
      }
    }
  }
  return count;
}

bool collisionFree(
    std::size_t node,
    const Point& candidate,
    const std::vector<NodeRow>& nodes,
    const std::vector<Point>& positions,
    double margin) {
  for (std::size_t other = 0; other < nodes.size(); ++other) {
    if (other == node) continue;
    const double halfWidth = (nodes[node].width + nodes[other].width) * 0.5 + margin;
    const double halfHeight = (nodes[node].height + nodes[other].height) * 0.5 + margin;
    if (
        std::abs(candidate.x - positions[other].x) < halfWidth
        && std::abs(candidate.y - positions[other].y) < halfHeight) {
      return false;
    }
  }
  return true;
}

bool collisionFreePair(
    std::size_t firstNode,
    const Point& firstPoint,
    std::size_t secondNode,
    const Point& secondPoint,
    const std::vector<NodeRow>& nodes,
    const std::vector<Point>& positions,
    double margin) {
  const double pairHalfWidth =
    (nodes[firstNode].width + nodes[secondNode].width) * 0.5 + margin;
  const double pairHalfHeight =
    (nodes[firstNode].height + nodes[secondNode].height) * 0.5 + margin;
  if (
      std::abs(firstPoint.x - secondPoint.x) < pairHalfWidth
      && std::abs(firstPoint.y - secondPoint.y) < pairHalfHeight) {
    return false;
  }
  for (std::size_t other = 0; other < nodes.size(); ++other) {
    if (other == firstNode || other == secondNode) continue;
    const double firstHalfWidth =
      (nodes[firstNode].width + nodes[other].width) * 0.5 + margin;
    const double firstHalfHeight =
      (nodes[firstNode].height + nodes[other].height) * 0.5 + margin;
    if (
        std::abs(firstPoint.x - positions[other].x) < firstHalfWidth
        && std::abs(firstPoint.y - positions[other].y) < firstHalfHeight) {
      return false;
    }
    const double secondHalfWidth =
      (nodes[secondNode].width + nodes[other].width) * 0.5 + margin;
    const double secondHalfHeight =
      (nodes[secondNode].height + nodes[other].height) * 0.5 + margin;
    if (
        std::abs(secondPoint.x - positions[other].x) < secondHalfWidth
        && std::abs(secondPoint.y - positions[other].y) < secondHalfHeight) {
      return false;
    }
  }
  return true;
}

double incidentLength(
    std::size_t node,
    const Point& candidate,
    const std::vector<std::vector<std::size_t>>& adjacency,
    const std::vector<Point>& positions) {
  double result = 0.0;
  for (const std::size_t neighbor : adjacency[node]) {
    result += distance(candidate, positions[neighbor]);
  }
  return result;
}

void appendPolar(
    std::vector<Point>& candidates,
    const Point& base,
    double radius,
    int samples,
    double phase) {
  for (int sample = 0; sample < samples; ++sample) {
    const double angle = phase + kTau * sample / std::max(1, samples);
    candidates.push_back({
      base.x + std::cos(angle) * radius,
      base.y + std::sin(angle) * radius,
    });
  }
}

std::vector<Point> nodeCandidates(
    std::size_t node,
    const std::vector<NodeRow>& nodes,
    const std::vector<Edge>& edges,
    const std::vector<std::vector<std::size_t>>& incidentEdges,
    const std::vector<std::vector<std::size_t>>& adjacency,
    const std::vector<Point>& positions,
    std::mt19937_64& random,
    int angularSamples,
    int randomCandidates,
    int crossingCandidates) {
  Point low = positions.front();
  Point high = positions.front();
  for (const Point& point : positions) {
    low.x = std::min(low.x, point.x);
    low.y = std::min(low.y, point.y);
    high.x = std::max(high.x, point.x);
    high.y = std::max(high.y, point.y);
  }
  const Point span{
    std::max(1.0, high.x - low.x),
    std::max(1.0, high.y - low.y),
  };
  const double diagonal = std::hypot(span.x, span.y);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  std::normal_distribution<double> normal(0.0, 1.0);
  const double phase = unit(random) * kTau;
  std::vector<Point> candidates;
  candidates.reserve(static_cast<std::size_t>(
    4 + (adjacency[node].size() + 1) * angularSamples * 5
    + randomCandidates * 3 + crossingCandidates * 3));
  candidates.push_back(positions[node]);

  Point neighborCenter = positions[node];
  if (!adjacency[node].empty()) {
    neighborCenter = {0.0, 0.0};
    for (const std::size_t neighbor : adjacency[node]) {
      neighborCenter.x += positions[neighbor].x;
      neighborCenter.y += positions[neighbor].y;
    }
    neighborCenter.x /= adjacency[node].size();
    neighborCenter.y /= adjacency[node].size();
  }
  candidates.push_back(neighborCenter);
  const std::vector<double> multipliers{1.0, 1.6, 2.6, 4.2, 7.0};
  for (const std::size_t neighbor : adjacency[node]) {
    const double clearance = 0.5 * std::hypot(
      nodes[node].width + nodes[neighbor].width,
      nodes[node].height + nodes[neighbor].height) + diagonal * 0.003;
    for (const double multiplier : multipliers) {
      appendPolar(
        candidates, positions[neighbor], clearance * multiplier,
        angularSamples, phase);
    }
  }
  const double centerClearance =
    std::max(nodes[node].width, nodes[node].height) + diagonal * 0.006;
  for (const double multiplier : multipliers) {
    appendPolar(
      candidates, neighborCenter, centerClearance * multiplier,
      angularSamples, phase * 0.5);
  }

  const std::vector<double> jitterScales{0.01, 0.025, 0.06, 0.14, 0.3};
  for (int sample = 0; sample < randomCandidates; ++sample) {
    const double scale = jitterScales[static_cast<std::size_t>(sample) % jitterScales.size()];
    candidates.push_back({
      neighborCenter.x + normal(random) * span.x * scale,
      neighborCenter.y + normal(random) * span.y * scale,
    });
    candidates.push_back({
      low.x - span.x * 0.05 + unit(random) * span.x * 1.1,
      low.y - span.y * 0.05 + unit(random) * span.y * 1.1,
    });
  }

  // Current crossing edges define useful arrangement boundaries. Sampling
  // just outside their endpoints and midpoint lets a node cross several such
  // boundaries in one accepted move without retaining the pair list.
  int appended = 0;
  for (const std::size_t incidentIndex : incidentEdges[node]) {
    if (appended >= crossingCandidates) break;
    const Edge& first = edges[incidentIndex];
    for (std::size_t otherIndex = 0;
         otherIndex < edges.size() && appended < crossingCandidates;
         ++otherIndex) {
      const Edge& second = edges[otherIndex];
      if (
          incidentIndex == otherIndex
          || first.source == second.source || first.source == second.target
          || first.target == second.source || first.target == second.target) {
        continue;
      }
      if (!properCross(
            positions[first.source], positions[first.target],
            positions[second.source], positions[second.target])) {
        continue;
      }
      const Point midpoint{
        (positions[second.source].x + positions[second.target].x) * 0.5,
        (positions[second.source].y + positions[second.target].y) * 0.5,
      };
      const double clearance = std::max(nodes[node].width, nodes[node].height)
        + diagonal * 0.004;
      const double angle = phase + appended * 2.39996322972865332;
      const Point offset{std::cos(angle) * clearance, std::sin(angle) * clearance};
      candidates.push_back({
        positions[second.source].x + offset.x,
        positions[second.source].y + offset.y,
      });
      candidates.push_back({
        positions[second.target].x - offset.x,
        positions[second.target].y - offset.y,
      });
      candidates.push_back({midpoint.x + offset.x, midpoint.y + offset.y});
      ++appended;
    }
  }
  return candidates;
}

void writePositions(
    const std::string& path,
    const std::vector<NodeRow>& nodes,
    const std::vector<Point>& positions) {
  std::ofstream stream(path);
  if (!stream) throw std::runtime_error("cannot write " + path);
  stream << std::fixed << std::setprecision(9);
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    stream << nodes[index].id << '\t'
           << positions[index].x << '\t'
           << positions[index].y << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string nodesPath = stringArg(argv, argc, "--nodes");
    const std::string edgesPath = stringArg(argv, argc, "--edges");
    const std::string positionsPath = stringArg(argv, argc, "--positions");
    const std::string outputPath = stringArg(argv, argc, "--out");
    const int rounds = std::max(0, intArg(argv, argc, "--rounds", 20));
    const int nodeLimit = std::max(0, intArg(argv, argc, "--node-limit", 100));
    const int angularSamples = std::max(2, intArg(argv, argc, "--angular-samples", 12));
    const int randomCandidates = std::max(0, intArg(argv, argc, "--random-candidates", 30));
    const int crossingCandidates = std::max(0, intArg(argv, argc, "--crossing-candidates", 40));
    const int swapNodeLimit = std::max(0, intArg(argv, argc, "--swap-node-limit", 0));
    const int swapCandidates = std::max(0, intArg(argv, argc, "--swap-candidates", 0));
    const int annealSteps = std::max(0, intArg(argv, argc, "--anneal-steps", 0));
    const int annealCycles = std::max(1, intArg(argv, argc, "--anneal-cycles", 4));
    const int annealReportEvery = std::max(
      1, intArg(argv, argc, "--anneal-report-every", 100000));
    const double annealStartTemperature = std::max(
      0.001, doubleArg(argv, argc, "--anneal-start-temperature", 40.0));
    const double annealEndTemperature = std::max(
      0.0001, doubleArg(argv, argc, "--anneal-end-temperature", 0.05));
    const int seed = intArg(argv, argc, "--seed", 42);
    const double margin = std::max(0.0, doubleArg(argv, argc, "--margin", 18.0));

    std::vector<NodeRow> nodes;
    std::unordered_map<std::string, std::size_t> indexById;
    {
      std::ifstream stream(nodesPath);
      if (!stream) throw std::runtime_error("cannot read " + nodesPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() < 3) throw std::runtime_error("bad node row");
        NodeRow row;
        row.id = fields[0];
        row.width = std::max(1.0, std::stod(fields[1]));
        row.height = std::max(1.0, std::stod(fields[2]));
        indexById[row.id] = nodes.size();
        nodes.push_back(std::move(row));
      }
    }

    std::vector<Edge> edges;
    std::set<std::pair<std::size_t, std::size_t>> seen;
    {
      std::ifstream stream(edgesPath);
      if (!stream) throw std::runtime_error("cannot read " + edgesPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() < 3) throw std::runtime_error("bad edge row");
        const auto source = indexById.find(fields[1]);
        const auto target = indexById.find(fields[2]);
        if (source == indexById.end() || target == indexById.end()) {
          throw std::runtime_error("edge references unknown node");
        }
        if (source->second == target->second) continue;
        const auto pair = std::minmax(source->second, target->second);
        if (seen.insert(pair).second) {
          std::size_t weight = 1;
          if (fields.size() >= 5
              && !fields[4].empty()
              && std::all_of(
                fields[4].begin(), fields[4].end(),
                [](unsigned char value) { return value >= '0' && value <= '9'; })) {
            weight = std::max<std::size_t>(1, std::stoull(fields[4]));
          }
          edges.push_back({source->second, target->second, weight});
        }
      }
    }

    std::vector<Point> positions(nodes.size());
    std::vector<bool> positioned(nodes.size(), false);
    {
      std::ifstream stream(positionsPath);
      if (!stream) throw std::runtime_error("cannot read " + positionsPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() < 3) continue;
        const auto found = indexById.find(fields[0]);
        if (found == indexById.end()) continue;
        positions[found->second] = {std::stod(fields[1]), std::stod(fields[2])};
        positioned[found->second] = true;
      }
    }
    if (std::find(positioned.begin(), positioned.end(), false) != positioned.end()) {
      throw std::runtime_error("positions file does not cover every node");
    }

    std::vector<std::vector<std::size_t>> adjacency(nodes.size());
    std::vector<std::vector<std::size_t>> incidentEdges(nodes.size());
    for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
      const Edge& edge = edges[edgeIndex];
      adjacency[edge.source].push_back(edge.target);
      adjacency[edge.target].push_back(edge.source);
      incidentEdges[edge.source].push_back(edgeIndex);
      incidentEdges[edge.target].push_back(edgeIndex);
    }

    std::mt19937_64 random(static_cast<std::uint64_t>(seed));
    std::size_t tracked = fullCrossingCount(edges, positions);
    std::cerr << "start nodes=" << nodes.size()
              << " edges=" << edges.size()
              << " cross=" << tracked << '\n';
    for (int round = 0; round < rounds; ++round) {
      std::vector<std::pair<std::size_t, std::size_t>> pressure;
      pressure.reserve(nodes.size());
      for (std::size_t node = 0; node < nodes.size(); ++node) {
        pressure.emplace_back(
          localCrossingCount(node, positions[node], edges, incidentEdges, positions),
          node);
      }
      std::sort(pressure.begin(), pressure.end(), [&](const auto& left, const auto& right) {
        if (left.first != right.first) return left.first > right.first;
        return nodes[left.second].id < nodes[right.second].id;
      });
      if (nodeLimit > 0 && pressure.size() > static_cast<std::size_t>(nodeLimit)) {
        pressure.resize(static_cast<std::size_t>(nodeLimit));
      }

      std::size_t accepted = 0;
      std::size_t gain = 0;
      for (const auto& item : pressure) {
        const std::size_t node = item.second;
        const std::size_t current = localCrossingCount(
          node, positions[node], edges, incidentEdges, positions);
        const std::vector<Point> candidates = nodeCandidates(
          node, nodes, edges, incidentEdges, adjacency, positions, random,
          angularSamples, randomCandidates, crossingCandidates);
        std::size_t bestCount = current;
        double bestLength = incidentLength(node, positions[node], adjacency, positions);
        Point best = positions[node];
        for (std::size_t candidateIndex = 1;
             candidateIndex < candidates.size();
             ++candidateIndex) {
          const Point& candidate = candidates[candidateIndex];
          if (!std::isfinite(candidate.x) || !std::isfinite(candidate.y)) continue;
          if (!collisionFree(node, candidate, nodes, positions, margin)) continue;
          const std::size_t count = localCrossingCount(
            node, candidate, edges, incidentEdges, positions);
          if (count > bestCount) continue;
          const double length = incidentLength(node, candidate, adjacency, positions);
          if (count < bestCount || length < bestLength) {
            bestCount = count;
            bestLength = length;
            best = candidate;
          }
        }
        if (bestCount >= current) continue;
        positions[node] = best;
        const std::size_t delta = current - bestCount;
        tracked -= delta;
        gain += delta;
        ++accepted;
      }

      std::size_t acceptedSwaps = 0;
      std::size_t swapGain = 0;
      const std::size_t swapLimit = std::min<std::size_t>(
        pressure.size(), static_cast<std::size_t>(swapNodeLimit));
      std::uniform_int_distribution<std::size_t> randomNode(0, nodes.size() - 1);
      for (std::size_t pressureIndex = 0; pressureIndex < swapLimit; ++pressureIndex) {
        const std::size_t firstNode = pressure[pressureIndex].second;
        std::vector<std::size_t> candidates;
        candidates.reserve(static_cast<std::size_t>(swapCandidates));
        const std::size_t pressureCandidateCount = std::min<std::size_t>(
          pressure.size(), static_cast<std::size_t>((swapCandidates + 1) / 2));
        for (std::size_t index = 0; index < pressureCandidateCount; ++index) {
          if (pressure[index].second != firstNode) {
            candidates.push_back(pressure[index].second);
          }
        }
        while (candidates.size() < static_cast<std::size_t>(swapCandidates)) {
          const std::size_t candidate = randomNode(random);
          if (candidate != firstNode) candidates.push_back(candidate);
        }
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        std::size_t bestGain = 0;
        std::size_t bestSecond = firstNode;
        for (const std::size_t secondNode : candidates) {
          const Point firstCandidate = positions[secondNode];
          const Point secondCandidate = positions[firstNode];
          if (!collisionFreePair(
                firstNode, firstCandidate,
                secondNode, secondCandidate,
                nodes, positions, margin)) {
            continue;
          }
          const std::size_t current = pairLocalCrossingCount(
            firstNode, positions[firstNode],
            secondNode, positions[secondNode],
            edges, incidentEdges, positions);
          const std::size_t swapped = pairLocalCrossingCount(
            firstNode, firstCandidate,
            secondNode, secondCandidate,
            edges, incidentEdges, positions);
          if (swapped < current && current - swapped > bestGain) {
            bestGain = current - swapped;
            bestSecond = secondNode;
          }
        }
        if (bestGain == 0 || bestSecond == firstNode) continue;
        std::swap(positions[firstNode], positions[bestSecond]);
        tracked -= bestGain;
        swapGain += bestGain;
        ++acceptedSwaps;
      }
      const std::size_t verified = fullCrossingCount(edges, positions);
      if (verified != tracked) {
        throw std::runtime_error(
          "incremental crossing drift tracked=" + std::to_string(tracked)
          + " verified=" + std::to_string(verified));
      }
      writePositions(outputPath, nodes, positions);
      std::cerr << "round=" << (round + 1)
                << " accepted=" << accepted
                << " gain=" << gain
                << " swaps=" << acceptedSwaps
                << " swapGain=" << swapGain
                << " cross=" << verified << '\n';
      if (accepted == 0 && acceptedSwaps == 0) break;
    }
    if (annealSteps > 0) {
      Point low = positions.front();
      Point high = positions.front();
      for (const Point& point : positions) {
        low.x = std::min(low.x, point.x);
        low.y = std::min(low.y, point.y);
        high.x = std::max(high.x, point.x);
        high.y = std::max(high.y, point.y);
      }
      const Point span{
        std::max(1.0, high.x - low.x),
        std::max(1.0, high.y - low.y),
      };
      std::uniform_real_distribution<double> unit(0.0, 1.0);
      std::uniform_int_distribution<std::size_t> randomNode(0, nodes.size() - 1);
      std::vector<Point> bestPositions = positions;
      std::size_t current = tracked;
      std::size_t best = tracked;
      std::size_t accepted = 0;
      std::size_t uphill = 0;
      const int cycleLength = std::max(1, annealSteps / annealCycles);
      for (int step = 1; step <= annealSteps; ++step) {
        const double progress = static_cast<double>((step - 1) % cycleLength)
          / static_cast<double>(std::max(1, cycleLength - 1));
        const double temperature = annealStartTemperature * std::pow(
          annealEndTemperature / annealStartTemperature, progress);
        const std::size_t firstNode = randomNode(random);
        const bool trySwap = unit(random) < 0.24;
        std::size_t beforeLocal = 0;
        std::size_t afterLocal = 0;
        std::size_t secondNode = firstNode;
        Point candidate = positions[firstNode];
        if (trySwap) {
          secondNode = randomNode(random);
          if (secondNode == firstNode) continue;
          if (!collisionFreePair(
                firstNode, positions[secondNode],
                secondNode, positions[firstNode],
                nodes, positions, margin)) {
            continue;
          }
          beforeLocal = pairLocalCrossingCount(
            firstNode, positions[firstNode],
            secondNode, positions[secondNode],
            edges, incidentEdges, positions);
          afterLocal = pairLocalCrossingCount(
            firstNode, positions[secondNode],
            secondNode, positions[firstNode],
            edges, incidentEdges, positions);
        } else {
          const int mode = static_cast<int>(unit(random) * 4.0);
          if (mode == 0 || adjacency[firstNode].empty()) {
            candidate = {
              low.x - span.x * 0.12 + unit(random) * span.x * 1.24,
              low.y - span.y * 0.12 + unit(random) * span.y * 1.24,
            };
          } else if (mode == 1) {
            const std::size_t neighbor = adjacency[firstNode][
              static_cast<std::size_t>(unit(random) * adjacency[firstNode].size())
                % adjacency[firstNode].size()];
            const double radius = std::hypot(span.x, span.y)
              * (0.004 + unit(random) * 0.18);
            const double angle = unit(random) * kTau;
            candidate = {
              positions[neighbor].x + std::cos(angle) * radius,
              positions[neighbor].y + std::sin(angle) * radius,
            };
          } else if (mode == 2 && adjacency[firstNode].size() >= 2) {
            const std::size_t left = adjacency[firstNode][
              static_cast<std::size_t>(unit(random) * adjacency[firstNode].size())
                % adjacency[firstNode].size()];
            const std::size_t right = adjacency[firstNode][
              static_cast<std::size_t>(unit(random) * adjacency[firstNode].size())
                % adjacency[firstNode].size()];
            candidate = {
              (positions[left].x + positions[right].x) * 0.5
                + (unit(random) - 0.5) * span.x * 0.08,
              (positions[left].y + positions[right].y) * 0.5
                + (unit(random) - 0.5) * span.y * 0.08,
            };
          } else {
            candidate = {
              positions[firstNode].x + (unit(random) - 0.5) * span.x * 0.16,
              positions[firstNode].y + (unit(random) - 0.5) * span.y * 0.16,
            };
          }
          if (!collisionFree(firstNode, candidate, nodes, positions, margin)) continue;
          beforeLocal = localCrossingCount(
            firstNode, positions[firstNode], edges, incidentEdges, positions);
          afterLocal = localCrossingCount(
            firstNode, candidate, edges, incidentEdges, positions);
        }
        const long long delta = static_cast<long long>(afterLocal)
          - static_cast<long long>(beforeLocal);
        const bool accept = delta <= 0
          || unit(random) < std::exp(-static_cast<double>(delta) / temperature);
        if (!accept) continue;
        if (trySwap) {
          std::swap(positions[firstNode], positions[secondNode]);
        } else {
          positions[firstNode] = candidate;
        }
        current = static_cast<std::size_t>(
          static_cast<long long>(current) + delta);
        ++accepted;
        if (delta > 0) ++uphill;
        if (current < best) {
          best = current;
          bestPositions = positions;
          writePositions(outputPath, nodes, bestPositions);
        }
        if (step % annealReportEvery == 0) {
          std::cerr << "anneal step=" << step
                    << " current=" << current
                    << " best=" << best
                    << " temperature=" << temperature
                    << " accepted=" << accepted
                    << " uphill=" << uphill << '\n';
        }
        if (step % cycleLength == 0 && step < annealSteps) {
          positions = bestPositions;
          current = best;
        }
      }
      positions = std::move(bestPositions);
      tracked = best;
      const std::size_t verified = fullCrossingCount(edges, positions);
      if (verified != tracked) {
        throw std::runtime_error(
          "anneal crossing drift tracked=" + std::to_string(tracked)
          + " verified=" + std::to_string(verified));
      }
      std::cerr << "anneal-done best=" << best
                << " accepted=" << accepted
                << " uphill=" << uphill << '\n';
    }
    writePositions(outputPath, nodes, positions);
    std::cerr << "done cross=" << tracked << " out=" << outputPath;
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
      const double peakMib = static_cast<double>(usage.ru_maxrss)
        / (1024.0 * 1024.0);
#else
      const double peakMib = static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
      std::cerr << " peakMiB=" << peakMib;
    }
    std::cerr << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
