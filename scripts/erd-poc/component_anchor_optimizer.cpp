// Low-memory optimizer for tolerant real-node component bundles.
//
// A template contains one non-core component plus every real core anchor it
// touches.  Candidate moves deform that whole semantic component toward its
// planar template and may move shared anchors slightly.  No proxy node, bend,
// edge deletion, or relationship aggregation is emitted.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/resource.h>

namespace {

struct Point { double x = 0.0; double y = 0.0; };
struct Node { std::string id; double width = 1.0; double height = 1.0; };
struct Edge { std::size_t source = 0; std::size_t target = 0; std::size_t weight = 1; };
struct TemplateMember { std::size_t node = 0; Point point; bool anchor = false; };
struct TemplateGroup { std::vector<TemplateMember> members; };

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> result;
  std::size_t start = 0;
  while (true) {
    const std::size_t next = line.find('\t', start);
    if (next == std::string::npos) {
      result.push_back(line.substr(start));
      return result;
    }
    result.push_back(line.substr(start, next - start));
    start = next + 1;
  }
}

std::string stringArg(char** argv, int argc, const std::string& name) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return argv[index + 1];
  }
  throw std::runtime_error("missing argument " + name);
}

int intArg(char** argv, int argc, const std::string& name, int fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return std::stoi(argv[index + 1]);
  }
  return fallback;
}

long double orientation(const Point& a, const Point& b, const Point& c) {
  return static_cast<long double>(b.x - a.x) * static_cast<long double>(c.y - a.y)
    - static_cast<long double>(b.y - a.y) * static_cast<long double>(c.x - a.x);
}

bool properCross(const Point& a, const Point& b, const Point& c, const Point& d) {
  const long double first = orientation(a, b, c);
  const long double second = orientation(a, b, d);
  const long double third = orientation(c, d, a);
  const long double fourth = orientation(c, d, b);
  return first != 0.0L && second != 0.0L && third != 0.0L && fourth != 0.0L
    && ((first < 0.0L) != (second < 0.0L))
    && ((third < 0.0L) != (fourth < 0.0L));
}

std::size_t crossingCount(
    const std::vector<Edge>& edges,
    const std::vector<Point>& positions) {
  std::size_t result = 0;
  for (std::size_t left = 0; left < edges.size(); ++left) {
    const Edge& first = edges[left];
    for (std::size_t right = left + 1; right < edges.size(); ++right) {
      const Edge& second = edges[right];
      if (first.source == second.source || first.source == second.target
          || first.target == second.source || first.target == second.target) {
        continue;
      }
      if (properCross(
            positions[first.source], positions[first.target],
            positions[second.source], positions[second.target])) {
        result += first.weight * second.weight;
      }
    }
  }
  return result;
}

std::size_t overlapCount(
    const std::vector<Node>& nodes,
    const std::vector<Point>& positions,
    double margin) {
  std::size_t result = 0;
  for (std::size_t left = 0; left < nodes.size(); ++left) {
    for (std::size_t right = left + 1; right < nodes.size(); ++right) {
      if (std::abs(positions[left].x - positions[right].x)
            < (nodes[left].width + nodes[right].width) * 0.5 + margin
          && std::abs(positions[left].y - positions[right].y)
            < (nodes[left].height + nodes[right].height) * 0.5 + margin) {
        ++result;
      }
    }
  }
  return result;
}

std::vector<Point> alignTemplate(
    const TemplateGroup& group,
    const std::vector<Point>& positions,
    bool reflect) {
  Point templateCenter;
  Point currentCenter;
  std::size_t anchorCount = 0;
  for (const TemplateMember& member : group.members) {
    if (!member.anchor) continue;
    Point point = member.point;
    if (reflect) point.x = -point.x;
    templateCenter.x += point.x;
    templateCenter.y += point.y;
    currentCenter.x += positions[member.node].x;
    currentCenter.y += positions[member.node].y;
    ++anchorCount;
  }
  if (anchorCount == 0) throw std::runtime_error("template has no anchors");
  templateCenter.x /= static_cast<double>(anchorCount);
  templateCenter.y /= static_cast<double>(anchorCount);
  currentCenter.x /= static_cast<double>(anchorCount);
  currentCenter.y /= static_cast<double>(anchorCount);
  long double real = 0.0L;
  long double imaginary = 0.0L;
  long double norm = 0.0L;
  for (const TemplateMember& member : group.members) {
    if (!member.anchor) continue;
    Point point = member.point;
    if (reflect) point.x = -point.x;
    const long double qx = point.x - templateCenter.x;
    const long double qy = point.y - templateCenter.y;
    const long double px = positions[member.node].x - currentCenter.x;
    const long double py = positions[member.node].y - currentCenter.y;
    real += qx * px + qy * py;
    imaginary += qx * py - qy * px;
    norm += qx * qx + qy * qy;
  }
  const long double magnitude = std::hypot(real, imaginary);
  const double scale = norm > 1e-12L
    ? static_cast<double>(magnitude / norm)
    : 1.0;
  const double cosine = magnitude > 1e-12L
    ? static_cast<double>(real / magnitude)
    : 1.0;
  const double sine = magnitude > 1e-12L
    ? static_cast<double>(imaginary / magnitude)
    : 0.0;
  std::vector<Point> result;
  result.reserve(group.members.size());
  for (const TemplateMember& member : group.members) {
    Point point = member.point;
    if (reflect) point.x = -point.x;
    const double x = (point.x - templateCenter.x) * scale;
    const double y = (point.y - templateCenter.y) * scale;
    result.push_back({
      currentCenter.x + x * cosine - y * sine,
      currentCenter.y + x * sine + y * cosine,
    });
  }
  return result;
}

void writePositions(
    const std::string& path,
    const std::vector<Node>& nodes,
    const std::vector<Point>& positions) {
  std::ofstream stream(path);
  if (!stream) throw std::runtime_error("cannot write " + path);
  stream << std::fixed << std::setprecision(9);
  for (std::size_t node = 0; node < nodes.size(); ++node) {
    stream << nodes[node].id << '\t'
           << positions[node].x << '\t' << positions[node].y << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string nodesPath = stringArg(argv, argc, "--nodes");
    const std::string edgesPath = stringArg(argv, argc, "--edges");
    const std::string positionsPath = stringArg(argv, argc, "--positions");
    const std::string templatesPath = stringArg(argv, argc, "--templates");
    const std::string outputPath = stringArg(argv, argc, "--out");
    const int rounds = std::max(1, intArg(argv, argc, "--rounds", 8));
    const double margin = 18.0;

    std::vector<Node> nodes;
    std::unordered_map<std::string, std::size_t> indexById;
    {
      std::ifstream stream(nodesPath);
      if (!stream) throw std::runtime_error("cannot read " + nodesPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const auto fields = splitTabs(line);
        if (fields.size() < 3) throw std::runtime_error("bad node row");
        indexById[fields[0]] = nodes.size();
        nodes.push_back({fields[0], std::stod(fields[1]), std::stod(fields[2])});
      }
    }
    std::vector<Edge> edges;
    {
      std::ifstream stream(edgesPath);
      if (!stream) throw std::runtime_error("cannot read " + edgesPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const auto fields = splitTabs(line);
        const auto source = indexById.find(fields[1]);
        const auto target = indexById.find(fields[2]);
        if (source == indexById.end() || target == indexById.end()) continue;
        std::size_t weight = 1;
        if (fields.size() >= 5
            && !fields[4].empty()
            && std::all_of(fields[4].begin(), fields[4].end(), [](unsigned char value) {
              return value >= '0' && value <= '9';
            })) {
          weight = std::max<std::size_t>(1, std::stoull(fields[4]));
        }
        edges.push_back({source->second, target->second, weight});
      }
    }
    std::vector<Point> positions(nodes.size());
    std::vector<bool> positioned(nodes.size(), false);
    {
      std::ifstream stream(positionsPath);
      if (!stream) throw std::runtime_error("cannot read " + positionsPath);
      std::string line;
      while (std::getline(stream, line)) {
        const auto fields = splitTabs(line);
        if (fields.size() < 3) continue;
        const auto found = indexById.find(fields[0]);
        if (found == indexById.end()) continue;
        positions[found->second] = {std::stod(fields[1]), std::stod(fields[2])};
        positioned[found->second] = true;
      }
    }
    if (std::find(positioned.begin(), positioned.end(), false) != positioned.end()) {
      throw std::runtime_error("positions do not cover all nodes");
    }
    std::vector<TemplateGroup> groups;
    {
      std::ifstream stream(templatesPath);
      if (!stream) throw std::runtime_error("cannot read " + templatesPath);
      std::string line;
      while (std::getline(stream, line)) {
        const auto fields = splitTabs(line);
        if (fields.size() < 5) continue;
        const std::size_t groupIndex = std::stoull(fields[0]);
        const auto node = indexById.find(fields[1]);
        if (node == indexById.end()) continue;
        if (groups.size() <= groupIndex) groups.resize(groupIndex + 1);
        groups[groupIndex].members.push_back({
          node->second,
          {std::stod(fields[2]), std::stod(fields[3])},
          fields[4] == "1",
        });
      }
    }

    std::size_t current = crossingCount(edges, positions);
    std::size_t currentOverlaps = overlapCount(nodes, positions, margin);
    std::cerr << "component-anchor-start groups=" << groups.size()
              << " cross=" << current
              << " overlaps=" << currentOverlaps << '\n';
    const std::vector<double> interpolations{0.2, 0.4, 0.6, 0.8, 1.0};
    const std::vector<double> localScales{0.85, 1.0, 1.2};
    for (int round = 0; round < rounds; ++round) {
      std::size_t accepted = 0;
      for (std::size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        const TemplateGroup& group = groups[groupIndex];
        std::vector<Point> bestPositions = positions;
        std::size_t best = current;
        std::size_t bestOverlaps = currentOverlaps;
        for (const bool reflect : {false, true}) {
          const std::vector<Point> aligned = alignTemplate(group, positions, reflect);
          Point alignedCenter;
          for (const Point& point : aligned) {
            alignedCenter.x += point.x;
            alignedCenter.y += point.y;
          }
          alignedCenter.x /= static_cast<double>(aligned.size());
          alignedCenter.y /= static_cast<double>(aligned.size());
          for (const double localScale : localScales) {
            for (const double interpolation : interpolations) {
              std::vector<Point> candidate = positions;
              for (std::size_t index = 0; index < group.members.size(); ++index) {
                const std::size_t node = group.members[index].node;
                const Point scaled{
                  alignedCenter.x + (aligned[index].x - alignedCenter.x) * localScale,
                  alignedCenter.y + (aligned[index].y - alignedCenter.y) * localScale,
                };
                candidate[node] = {
                  positions[node].x + (scaled.x - positions[node].x) * interpolation,
                  positions[node].y + (scaled.y - positions[node].y) * interpolation,
                };
              }
              const std::size_t candidateOverlaps = overlapCount(nodes, candidate, margin);
              if (candidateOverlaps > bestOverlaps) continue;
              const std::size_t candidateCross = crossingCount(edges, candidate);
              if (candidateCross < best
                  || (candidateCross == best && candidateOverlaps < bestOverlaps)) {
                best = candidateCross;
                bestOverlaps = candidateOverlaps;
                bestPositions = std::move(candidate);
              }
            }
          }
        }
        if (best >= current || bestOverlaps > currentOverlaps) continue;
        std::cerr << "component-anchor round=" << (round + 1)
                  << " group=" << groupIndex
                  << " nodes=" << group.members.size()
                  << " cross=" << current << "->" << best
                  << " overlaps=" << currentOverlaps << "->" << bestOverlaps << '\n';
        positions = std::move(bestPositions);
        current = best;
        currentOverlaps = bestOverlaps;
        ++accepted;
        writePositions(outputPath, nodes, positions);
      }
      std::cerr << "component-anchor-round-done round=" << (round + 1)
                << " accepted=" << accepted
                << " cross=" << current << '\n';
      if (accepted == 0) break;
    }
    writePositions(outputPath, nodes, positions);
    rusage usage{};
    std::cerr << "component-anchor-done cross=" << current
              << " overlaps=" << currentOverlaps;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
      std::cerr << " peakMiB="
                << static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
      std::cerr << " peakMiB="
                << static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
    }
    std::cerr << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
