// Minimal OGDF placement probe without the production post-processing stack.

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/basic/Layout.h>
#include <ogdf/energybased/StressMinimization.h>
#include <ogdf/layered/BarycenterHeuristic.h>
#include <ogdf/layered/OptimalHierarchyLayout.h>
#include <ogdf/layered/OptimalRanking.h>
#include <ogdf/layered/SugiyamaLayout.h>
#include <ogdf/planarity/PlanarSubgraphFast.h>
#include <ogdf/planarity/PlanRep.h>
#include <ogdf/planarity/PlanarizationLayout.h>
#include <ogdf/planarity/PlanarizerMixedInsertion.h>
#include <ogdf/planarity/RemoveReinsertType.h>
#include <ogdf/planarity/SimpleEmbedder.h>
#include <ogdf/planarity/SubgraphPlanarizer.h>
#include <ogdf/planarity/VariableEmbeddingInserter.h>
#include <ogdf/orthogonal/OrthoLayout.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
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
  std::size_t result = 0;
  for (std::size_t left = 0; left < edges.size(); ++left) {
    for (std::size_t right = left + 1; right < edges.size(); ++right) {
      const auto first = edges[left];
      const auto second = edges[right];
      if (
          first.first == second.first || first.first == second.second
          || first.second == second.first || first.second == second.second) {
        continue;
      }
      result += properCross(
        positions[first.first], positions[first.second],
        positions[second.first], positions[second.second]);
    }
  }
  return result;
}

ogdf::SubgraphPlanarizer* boundedPlanarizer() {
  auto* planarizer = new ogdf::SubgraphPlanarizer();
  auto* subgraph = new ogdf::PlanarSubgraphFast<int>();
  auto* inserter = new ogdf::VariableEmbeddingInserter();
  subgraph->runs(1);
  subgraph->maxThreads(1);
  inserter->removeReinsert(ogdf::RemoveReinsertType::None);
  planarizer->setSubgraph(subgraph);
  planarizer->setInserter(inserter);
  planarizer->permutations(1);
  planarizer->maxThreads(1);
  return planarizer;
}

ogdf::SubgraphPlanarizer* highQualityPlanarizer() {
  auto* planarizer = new ogdf::SubgraphPlanarizer();
  auto* subgraph = new ogdf::PlanarSubgraphFast<int>();
  auto* inserter = new ogdf::VariableEmbeddingInserter();
  subgraph->runs(32);
  subgraph->maxThreads(1);
  inserter->removeReinsert(ogdf::RemoveReinsertType::IncInserted);
  planarizer->setSubgraph(subgraph);
  planarizer->setInserter(inserter);
  planarizer->permutations(16);
  planarizer->maxThreads(1);
  return planarizer;
}

ogdf::PlanarizerMixedInsertion* mixedInsertionPlanarizer(
    ogdf::PlanarizerMixedInsertion::NodeSelectionMethod selection,
    int runs) {
  auto* planarizer = new ogdf::PlanarizerMixedInsertion();
  auto* subgraph = new ogdf::PlanarSubgraphFast<int>();
  subgraph->runs(runs);
  subgraph->maxThreads(1);
  planarizer->setSubgraph(subgraph);
  planarizer->nodeSelectionMethod(selection);
  return planarizer;
}

bool isMixedInsertionMode(const std::string& mode) {
  return mode.rfind("mixed-", 0) == 0;
}

ogdf::PlanarizerMixedInsertion::NodeSelectionMethod mixedSelection(
    const std::string& mode) {
  using Selection = ogdf::PlanarizerMixedInsertion::NodeSelectionMethod;
  if (mode.find("higher-nonplanar") != std::string::npos) {
    return Selection::HigherNonPlanarDegree;
  }
  if (mode.find("lower-nonplanar") != std::string::npos) {
    return Selection::LowerNonPlanarDegree;
  }
  if (mode.find("lower") != std::string::npos) {
    return Selection::LowerDegree;
  }
  if (mode.find("both") != std::string::npos) {
    return Selection::BothEndpoints;
  }
  return Selection::HigherDegree;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string nodesPath = stringArg(argv, argc, "--nodes");
    const std::string edgesPath = stringArg(argv, argc, "--edges");
    const std::string mode = stringArg(argv, argc, "--mode");

    std::vector<NodeRow> nodes;
    std::unordered_map<std::string, std::size_t> indexById;
    {
      std::ifstream stream(nodesPath);
      if (!stream) throw std::runtime_error("cannot read " + nodesPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const auto fields = splitTabs(line);
        if (fields.size() < 3) throw std::runtime_error("bad node row");
        NodeRow row{fields[0], std::stod(fields[1]), std::stod(fields[2])};
        indexById[row.id] = nodes.size();
        nodes.push_back(std::move(row));
      }
    }
    std::set<std::pair<std::size_t, std::size_t>> uniqueEdges;
    std::map<std::pair<std::size_t, std::size_t>, int> inputCosts;
    {
      std::ifstream stream(edgesPath);
      if (!stream) throw std::runtime_error("cannot read " + edgesPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const auto fields = splitTabs(line);
        if (fields.size() < 3) throw std::runtime_error("bad edge row");
        const auto source = indexById.find(fields[1]);
        const auto target = indexById.find(fields[2]);
        if (source == indexById.end() || target == indexById.end()) {
          throw std::runtime_error("edge references unknown node");
        }
        if (source->second != target->second) {
          const auto pair = std::minmax(source->second, target->second);
          uniqueEdges.insert(pair);
          const int cost = fields.size() >= 6
            ? std::max(1, std::stoi(fields[5]))
            : 1;
          inputCosts[pair] = std::max(inputCosts[pair], cost);
        }
      }
    }

    ogdf::Graph graph;
    ogdf::GraphAttributes attributes(
      graph,
      ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
    std::vector<ogdf::node> handles;
    handles.reserve(nodes.size());
    for (const NodeRow& row : nodes) {
      const ogdf::node handle = graph.newNode();
      handles.push_back(handle);
      attributes.width(handle) = std::max(1.0, row.width);
      attributes.height(handle) = std::max(1.0, row.height);
    }
    ogdf::EdgeArray<int> edgeCosts(graph, 1);
    for (const auto pair : uniqueEdges) {
      const ogdf::edge edge = graph.newEdge(handles[pair.first], handles[pair.second]);
      edgeCosts[edge] = inputCosts[pair];
    }

    int topologicalCrossings = -1;
    if (mode == "planarization" || mode == "planarization-high") {
      ogdf::PlanarizationLayout layout;
      layout.setCrossMin(
        mode == "planarization-high"
          ? highQualityPlanarizer()
          : boundedPlanarizer());
      layout.pageRatio(1.0);
      layout.call(attributes);
      topologicalCrossings = layout.numberOfCrossings();
    } else if (isMixedInsertionMode(mode)) {
      ogdf::PlanarizationLayout layout;
      layout.setCrossMin(mixedInsertionPlanarizer(
        mixedSelection(mode),
        mode.find("-high") == std::string::npos ? 1 : 32));
      layout.pageRatio(1.0);
      layout.call(attributes);
      topologicalCrossings = layout.numberOfCrossings();
    } else if (
        mode == "planarization-weighted"
        || mode == "planarization-weighted-high") {
      std::unique_ptr<ogdf::SubgraphPlanarizer> planarizer(
        mode == "planarization-weighted-high"
          ? highQualityPlanarizer()
          : boundedPlanarizer());
      ogdf::PlanRep representation(attributes);
      topologicalCrossings = 0;
      for (int component = 0; component < representation.numberOfCCs(); ++component) {
        int weightedCrossings = 0;
        planarizer->call(
          representation,
          component,
          weightedCrossings,
          &edgeCosts);
        for (ogdf::node node : representation.nodes) {
          if (representation.isDummy(node)) ++topologicalCrossings;
        }
        ogdf::SimpleEmbedder embedder;
        ogdf::adjEntry external = nullptr;
        embedder.call(representation, external);
        ogdf::Layout drawing(representation);
        ogdf::OrthoLayout layouter;
        layouter.call(representation, external, drawing);
        for (int index = representation.startNode();
             index < representation.stopNode();
             ++index) {
          const ogdf::node original = representation.v(index);
          const ogdf::node copy = representation.copy(original);
          attributes.x(original) = drawing.x(copy);
          attributes.y(original) = drawing.y(copy);
        }
      }
    } else if (mode == "sugiyama") {
      ogdf::SugiyamaLayout layout;
      layout.setRanking(new ogdf::OptimalRanking());
      layout.setCrossMin(new ogdf::BarycenterHeuristic());
      auto* hierarchy = new ogdf::OptimalHierarchyLayout();
      hierarchy->layerDistance(140.0);
      hierarchy->nodeDistance(64.0);
      layout.setLayout(hierarchy);
      layout.runs(1);
      layout.fails(1);
      layout.transpose(true);
      layout.arrangeCCs(true);
      layout.call(attributes);
    } else if (mode == "stress") {
      ogdf::StressMinimization layout;
      layout.setIterations(200);
      layout.setEdgeCosts(140.0);
      layout.call(attributes);
    } else {
      throw std::runtime_error("unknown mode: " + mode);
    }

    std::vector<std::pair<double, double>> positions(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      positions[index] = {attributes.x(handles[index]), attributes.y(handles[index])};
    }
    std::size_t bends = 0;
    for (ogdf::edge edge : graph.edges) bends += attributes.bends(edge).size();
    const std::vector<std::pair<std::size_t, std::size_t>> relationships(
      uniqueEdges.begin(), uniqueEdges.end());
    const std::size_t crossings = crossingCount(relationships, positions);
    std::cout << std::fixed << std::setprecision(9);
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      std::cout << nodes[index].id << '\t'
                << positions[index].first << '\t'
                << positions[index].second << '\n';
    }
    std::cerr << "ogdf-general mode=" << mode
              << " nodes=" << nodes.size()
              << " edges=" << relationships.size()
              << " bends=" << bends
              << " topologicalCrossings=" << topologicalCrossings
              << " directCrossings=" << crossings;
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
