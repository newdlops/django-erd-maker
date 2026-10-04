// Low-memory structural planar-backbone probe.
//
// The probe never removes a model or changes a relationship in its output.
// A maximal planar subgraph is used only to choose coordinates; callers score
// every original relationship as a direct source-to-target segment.

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/planarlayout/FPPLayout.h>
#include <ogdf/planarity/BoyerMyrvold.h>
#include <ogdf/planarity/PlanarSubgraphFast.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/resource.h>

namespace {

struct NodeRow {
  std::string id;
  double width = 1.0;
  double height = 1.0;
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

std::string stringArg(char** argv, int argc, const std::string& name) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return argv[index + 1];
  }
  throw std::runtime_error("missing argument: " + name);
}

std::string stringArg(
    char** argv,
    int argc,
    const std::string& name,
    const std::string& fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return argv[index + 1];
  }
  return fallback;
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

long double orientation(
    const std::pair<double, double>& a,
    const std::pair<double, double>& b,
    const std::pair<double, double>& c) {
  return static_cast<long double>(b.first - a.first)
      * static_cast<long double>(c.second - a.second)
    - static_cast<long double>(b.second - a.second)
      * static_cast<long double>(c.first - a.first);
}

bool properCross(
    const std::pair<double, double>& a,
    const std::pair<double, double>& b,
    const std::pair<double, double>& c,
    const std::pair<double, double>& d) {
  const long double first = orientation(a, b, c);
  const long double second = orientation(a, b, d);
  const long double third = orientation(c, d, a);
  const long double fourth = orientation(c, d, b);
  return first != 0.0L && second != 0.0L
    && third != 0.0L && fourth != 0.0L
    && ((first < 0.0L) != (second < 0.0L))
    && ((third < 0.0L) != (fourth < 0.0L));
}

std::size_t crossingCount(
    const std::vector<std::pair<std::size_t, std::size_t>>& edges,
    const std::vector<std::pair<double, double>>& positions) {
  std::size_t count = 0;
  for (std::size_t left = 0; left < edges.size(); ++left) {
    for (std::size_t right = left + 1; right < edges.size(); ++right) {
      const auto& first = edges[left];
      const auto& second = edges[right];
      if (
          first.first == second.first || first.first == second.second
          || first.second == second.first || first.second == second.second) {
        continue;
      }
      count += properCross(
        positions[first.first], positions[first.second],
        positions[second.first], positions[second.second]);
    }
  }
  return count;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string nodesPath = stringArg(argv, argc, "--nodes");
    const std::string edgesPath = stringArg(argv, argc, "--edges");
    const int runs = std::clamp(intArg(argv, argc, "--runs", 0), 0, 64);
    const int seed = intArg(argv, argc, "--seed", 42);
    const std::string costMode = stringArg(
      argv, argc, "--cost-mode", "uniform");
    const double separation = std::max(
      0.0, doubleArg(argv, argc, "--separation", 48.0));
    const double componentGap = std::max(
      1.0, doubleArg(argv, argc, "--component-gap", 1000.0));
    ogdf::setSeed(seed);

    std::vector<NodeRow> nodes;
    std::unordered_map<std::string, std::size_t> nodeIndexById;
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
        nodeIndexById[row.id] = nodes.size();
        nodes.push_back(std::move(row));
      }
    }

    std::vector<std::pair<std::size_t, std::size_t>> relationships;
    std::set<std::pair<std::size_t, std::size_t>> uniquePairs;
    {
      std::ifstream stream(edgesPath);
      if (!stream) throw std::runtime_error("cannot read " + edgesPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() < 3) throw std::runtime_error("bad edge row");
        const auto source = nodeIndexById.find(fields[1]);
        const auto target = nodeIndexById.find(fields[2]);
        if (source == nodeIndexById.end() || target == nodeIndexById.end()) {
          throw std::runtime_error("edge references unknown node");
        }
        if (source->second == target->second) continue;
        relationships.emplace_back(source->second, target->second);
        uniquePairs.insert(std::minmax(source->second, target->second));
      }
    }

    ogdf::Graph backbone;
    ogdf::NodeArray<std::size_t> originalIndex(backbone);
    std::vector<ogdf::node> backboneNodes(nodes.size(), nullptr);
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      backboneNodes[index] = backbone.newNode();
      originalIndex[backboneNodes[index]] = index;
    }
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
    planarSubgraph.runs(runs);
    planarSubgraph.maxThreads(1);
    ogdf::List<ogdf::edge> deletedEdges;
    if (costMode == "uniform") {
      planarSubgraph.call(backbone, deletedEdges);
    } else {
      ogdf::EdgeArray<int> deletionCost(backbone, 1);
      for (ogdf::edge edge : backbone.edges) {
        const auto pair = endpoints[edge];
        const std::size_t sourceDegree = degree[pair.first];
        const std::size_t targetDegree = degree[pair.second];
        std::size_t cost = 1;
        if (costMode == "degree-sum") {
          cost += sourceDegree + targetDegree;
        } else if (costMode == "degree-product") {
          cost += sourceDegree * targetDegree;
        } else if (costMode == "hub") {
          const std::size_t maximum = std::max(sourceDegree, targetDegree);
          const std::size_t minimum = std::min(sourceDegree, targetDegree);
          cost += maximum * maximum + minimum;
        } else {
          throw std::runtime_error("unknown --cost-mode: " + costMode);
        }
        deletionCost[edge] = static_cast<int>(std::min<std::size_t>(
          cost, static_cast<std::size_t>(std::numeric_limits<int>::max())));
      }
      planarSubgraph.call(backbone, deletionCost, deletedEdges);
    }
    std::vector<std::pair<std::size_t, std::size_t>> deletedPairs;
    deletedPairs.reserve(deletedEdges.size());
    for (ogdf::edge edge : deletedEdges) deletedPairs.push_back(endpoints[edge]);
    for (ogdf::edge edge : deletedEdges) backbone.delEdge(edge);
    const std::size_t initiallyDeleted = deletedPairs.size();

    std::sort(deletedPairs.begin(), deletedPairs.end(), [&](const auto& left, const auto& right) {
      const std::size_t leftDegree = degree[left.first] + degree[left.second];
      const std::size_t rightDegree = degree[right.first] + degree[right.second];
      if (leftDegree != rightDegree) return leftDegree > rightDegree;
      return left < right;
    });
    ogdf::BoyerMyrvold planarityTest;
    std::size_t reinserted = 0;
    for (const auto& pair : deletedPairs) {
      const ogdf::edge candidate = backbone.newEdge(
        backboneNodes[pair.first], backboneNodes[pair.second]);
      if (planarityTest.isPlanar(backbone)) {
        ++reinserted;
      } else {
        backbone.delEdge(candidate);
      }
    }

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

    std::vector<std::pair<double, double>> positions(nodes.size());
    double cursorX = 0.0;
    for (const std::vector<ogdf::node>& component : components) {
      const int componentIndex = componentOf[component.front()];
      ogdf::Graph componentGraph;
      ogdf::GraphAttributes componentAttributes(
        componentGraph,
        ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
      std::unordered_map<ogdf::node, ogdf::node> copy;
      copy.reserve(component.size());
      for (ogdf::node source : component) {
        const std::size_t index = originalIndex[source];
        const ogdf::node target = componentGraph.newNode();
        copy[source] = target;
        componentAttributes.width(target) = nodes[index].width;
        componentAttributes.height(target) = nodes[index].height;
      }
      for (ogdf::edge edge : backbone.edges) {
        if (componentOf[edge->source()] != componentIndex) continue;
        componentGraph.newEdge(copy.at(edge->source()), copy.at(edge->target()));
      }

      ogdf::FPPLayout layout;
      layout.separation(separation);
      layout.call(componentAttributes);
      double minX = std::numeric_limits<double>::infinity();
      double maxX = -std::numeric_limits<double>::infinity();
      for (ogdf::node source : component) {
        const std::size_t index = originalIndex[source];
        const ogdf::node target = copy.at(source);
        minX = std::min(
          minX, componentAttributes.x(target) - nodes[index].width * 0.5);
        maxX = std::max(
          maxX, componentAttributes.x(target) + nodes[index].width * 0.5);
      }
      if (!std::isfinite(minX) || !std::isfinite(maxX)) minX = maxX = 0.0;
      for (ogdf::node source : component) {
        const std::size_t index = originalIndex[source];
        const ogdf::node target = copy.at(source);
        positions[index] = {
          componentAttributes.x(target) + cursorX - minX,
          componentAttributes.y(target),
        };
      }
      cursorX += std::max(1.0, maxX - minX) + componentGap;
    }

    const std::size_t crossings = crossingCount(relationships, positions);
    std::cout << std::fixed << std::setprecision(9);
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      std::cout << nodes[index].id << '\t'
                << positions[index].first << '\t'
                << positions[index].second << '\n';
    }
    std::cerr << "planar-backbone nodes=" << nodes.size()
              << " relationships=" << relationships.size()
              << " uniqueEdges=" << uniquePairs.size()
              << " initiallyDeleted=" << initiallyDeleted
              << " reinserted=" << reinserted
              << " remainingDeleted=" << (initiallyDeleted - reinserted)
              << " components=" << components.size()
              << " directCrossings=" << crossings
              << " runs=" << runs
              << " costMode=" << costMode;
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
