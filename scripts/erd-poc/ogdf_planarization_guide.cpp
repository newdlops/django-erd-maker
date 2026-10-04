// Emit the raw OGDF planarization geometry before django-erd post-processing.
//
// This is a research helper, not a renderer.  Its bends describe a target
// topological embedding for the straight-node optimizer; production still
// renders every original relationship as its own direct segment.

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/basic/basic.h>
#include <ogdf/energybased/DavidsonHarelLayout.h>
#include <ogdf/geometric/CrossingMinimalPosition.h>
#include <ogdf/planarity/PlanarSubgraphFast.h>
#include <ogdf/planarity/PlanarizationLayout.h>
#include <ogdf/planarity/RemoveReinsertType.h>
#include <ogdf/planarity/SubgraphPlanarizer.h>
#include <ogdf/planarity/VariableEmbeddingInserter.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <unistd.h>

namespace {

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

struct NodeRow {
  std::string id;
  double width = 1.0;
  double height = 1.0;
  ogdf::node handle = nullptr;
};

struct EdgeRow {
  std::string id;
  std::string source;
  std::string target;
  ogdf::edge handle = nullptr;
};

long double crossProduct(
    const ogdf::DPoint& a,
    const ogdf::DPoint& b,
    const ogdf::DPoint& c) {
  return static_cast<long double>(b.m_x - a.m_x)
      * static_cast<long double>(c.m_y - a.m_y)
    - static_cast<long double>(b.m_y - a.m_y)
      * static_cast<long double>(c.m_x - a.m_x);
}

bool properCross(
    const ogdf::DPoint& a,
    const ogdf::DPoint& b,
    const ogdf::DPoint& c,
    const ogdf::DPoint& d) {
  const long double first = crossProduct(a, b, c);
  const long double second = crossProduct(a, b, d);
  const long double third = crossProduct(c, d, a);
  const long double fourth = crossProduct(c, d, b);
  return first != 0.0L && second != 0.0L
    && third != 0.0L && fourth != 0.0L
    && ((first < 0.0L) != (second < 0.0L))
    && ((third < 0.0L) != (fourth < 0.0L));
}

ogdf::DPoint position(
    const ogdf::GraphAttributes& attributes,
    const ogdf::node node) {
  return ogdf::DPoint(attributes.x(node), attributes.y(node));
}

bool shareEndpoint(const ogdf::edge left, const ogdf::edge right) {
  return left->source() == right->source()
    || left->source() == right->target()
    || left->target() == right->source()
    || left->target() == right->target();
}

bool edgesCross(
    const ogdf::GraphAttributes& attributes,
    const ogdf::edge left,
    const ogdf::edge right) {
  if (shareEndpoint(left, right)) return false;
  return properCross(
    position(attributes, left->source()),
    position(attributes, left->target()),
    position(attributes, right->source()),
    position(attributes, right->target()));
}

long long crossingCount(
    const ogdf::GraphAttributes& attributes,
    const std::vector<EdgeRow>& edges) {
  long long count = 0;
  for (std::size_t left = 0; left < edges.size(); ++left) {
    for (std::size_t right = left + 1; right < edges.size(); ++right) {
      count += edgesCross(
        attributes, edges[left].handle, edges[right].handle);
    }
  }
  return count;
}

long long incidentCrossingCount(
    const ogdf::GraphAttributes& attributes,
    const std::vector<EdgeRow>& edges,
    const ogdf::node moved) {
  long long count = 0;
  for (std::size_t left = 0; left < edges.size(); ++left) {
    const bool leftIncident = edges[left].handle->isIncident(moved);
    for (std::size_t right = left + 1; right < edges.size(); ++right) {
      if (!leftIncident && !edges[right].handle->isIncident(moved)) continue;
      count += edgesCross(
        attributes, edges[left].handle, edges[right].handle);
    }
  }
  return count;
}

std::vector<std::pair<long long, ogdf::node>> crossingScores(
    const ogdf::GraphAttributes& attributes,
    const std::vector<NodeRow>& nodes,
    const std::vector<EdgeRow>& edges) {
  std::vector<std::pair<long long, ogdf::node>> scores;
  scores.reserve(nodes.size());
  for (const NodeRow& row : nodes) {
    scores.emplace_back(0, row.handle);
  }
  for (std::size_t left = 0; left < edges.size(); ++left) {
    for (std::size_t right = left + 1; right < edges.size(); ++right) {
      if (!edgesCross(attributes, edges[left].handle, edges[right].handle)) {
        continue;
      }
      const ogdf::edge first = edges[left].handle;
      const ogdf::edge second = edges[right].handle;
      ++scores[static_cast<std::size_t>(first->source()->index())].first;
      ++scores[static_cast<std::size_t>(first->target()->index())].first;
      ++scores[static_cast<std::size_t>(second->source()->index())].first;
      ++scores[static_cast<std::size_t>(second->target()->index())].first;
    }
  }
  std::stable_sort(
    scores.begin(), scores.end(),
    [](const auto& left, const auto& right) {
      if (left.first != right.first) return left.first > right.first;
      return left.second->index() < right.second->index();
    });
  return scores;
}

bool separatedFromOtherNodes(
    const ogdf::GraphAttributes& attributes,
    const ogdf::DPoint& candidate,
    const ogdf::node moved,
    const std::vector<NodeRow>& nodes,
    const double minimumDistance) {
  if (minimumDistance <= 0.0) return true;
  const double minimumSquared = minimumDistance * minimumDistance;
  for (const NodeRow& row : nodes) {
    if (row.handle == moved) continue;
    const double dx = candidate.m_x - attributes.x(row.handle);
    const double dy = candidate.m_y - attributes.y(row.handle);
    if (dx * dx + dy * dy < minimumSquared) return false;
  }
  return true;
}

long long perturbToGeneralPosition(
    ogdf::GraphAttributes& attributes,
    const std::vector<NodeRow>& nodes,
    const std::vector<EdgeRow>& edges,
    const double amplitude,
    const int trials,
    const int seed) {
  std::vector<ogdf::DPoint> original;
  original.reserve(nodes.size());
  for (const NodeRow& row : nodes) {
    original.push_back(position(attributes, row.handle));
  }
  std::vector<ogdf::DPoint> best = original;
  long long bestCrossings = std::numeric_limits<long long>::max();
  std::mt19937_64 generator(static_cast<std::uint64_t>(seed));
  std::uniform_real_distribution<double> jitter(-amplitude, amplitude);
  for (int trial = 0; trial < trials; ++trial) {
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      attributes.x(nodes[index].handle) = original[index].m_x + jitter(generator);
      attributes.y(nodes[index].handle) = original[index].m_y + jitter(generator);
    }
    const long long crossings = crossingCount(attributes, edges);
    if (crossings < bestCrossings) {
      bestCrossings = crossings;
      for (std::size_t index = 0; index < nodes.size(); ++index) {
        best[index] = position(attributes, nodes[index].handle);
      }
    }
  }
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    attributes.x(nodes[index].handle) = best[index].m_x;
    attributes.y(nodes[index].handle) = best[index].m_y;
  }
  return bestCrossings;
}

int integerArg(char** argv, int argc, const std::string& name, int fallback) {
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

std::string optionalStringArg(
    char** argv,
    int argc,
    const std::string& name,
    const std::string& fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return argv[index + 1];
  }
  return fallback;
}

std::string stringArg(char** argv, int argc, const std::string& name) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return argv[index + 1];
  }
  throw std::runtime_error("missing argument: " + name);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string nodesPath = stringArg(argv, argc, "--nodes");
    const std::string edgesPath = stringArg(argv, argc, "--edges");
    const int seed = integerArg(argv, argc, "--seed", 42);
    std::string algorithm = "planarization";
    for (int index = 1; index + 1 < argc; ++index) {
      if (std::string(argv[index]) == "--algorithm") {
        algorithm = argv[index + 1];
      }
    }
    int optimizerLock = -1;
    if (algorithm == "geometric") {
      optimizerLock = open(
        "/private/tmp/django-erd-geometric-optimizer.lock",
        O_CREAT | O_RDWR,
        0600);
      if (optimizerLock < 0
          || flock(optimizerLock, LOCK_EX | LOCK_NB) != 0) {
        if (optimizerLock >= 0) close(optimizerLock);
        throw std::runtime_error(
          "another geometric optimizer is already running");
      }
    }
    const int runs = std::max(1, integerArg(argv, argc, "--runs", 1));
    const int permutations = std::max(
      1, integerArg(argv, argc, "--permutations", 1));
    const int threads = std::max(1, integerArg(argv, argc, "--threads", 1));
    const int removeReinsert = integerArg(
      argv, argc, "--remove-reinsert", 0);

    ogdf::setSeed(seed);
    ogdf::Graph graph;
    ogdf::GraphAttributes attributes(
      graph,
      ogdf::GraphAttributes::nodeGraphics
        | ogdf::GraphAttributes::edgeGraphics);
    std::vector<NodeRow> nodes;
    std::vector<EdgeRow> edges;
    std::unordered_map<std::string, ogdf::node> nodeById;
    std::unordered_map<std::uint64_t, ogdf::edge> geometricEdgeByPair;

    {
      std::ifstream stream(nodesPath);
      if (!stream) throw std::runtime_error("cannot read " + nodesPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const auto fields = splitTabs(line);
        if (fields.size() < 5) throw std::runtime_error("bad node row");
        NodeRow row;
        row.id = fields[0];
        row.width = std::max(1.0, std::stod(fields[1]));
        row.height = std::max(1.0, std::stod(fields[2]));
        row.handle = graph.newNode();
        attributes.width(row.handle) = row.width;
        attributes.height(row.handle) = row.height;
        attributes.x(row.handle) = std::stod(fields[3]) + row.width * 0.5;
        attributes.y(row.handle) = std::stod(fields[4]) + row.height * 0.5;
        nodeById.emplace(row.id, row.handle);
        nodes.push_back(std::move(row));
      }
    }
    {
      std::ifstream stream(edgesPath);
      if (!stream) throw std::runtime_error("cannot read " + edgesPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const auto fields = splitTabs(line);
        if (fields.size() < 5) throw std::runtime_error("bad edge row");
        const auto source = nodeById.find(fields[1]);
        const auto target = nodeById.find(fields[2]);
        if (source == nodeById.end() || target == nodeById.end()) {
          throw std::runtime_error("edge references unknown node");
        }
        if (source->second == target->second) continue;
        EdgeRow row;
        row.id = fields[0];
        row.source = fields[1];
        row.target = fields[2];
        if (algorithm == "geometric") {
          const std::uint32_t sourceIndex = static_cast<std::uint32_t>(
            source->second->index());
          const std::uint32_t targetIndex = static_cast<std::uint32_t>(
            target->second->index());
          const std::uint32_t low = std::min(sourceIndex, targetIndex);
          const std::uint32_t high = std::max(sourceIndex, targetIndex);
          const std::uint64_t key =
            (static_cast<std::uint64_t>(low) << 32) | high;
          const auto existing = geometricEdgeByPair.find(key);
          if (existing == geometricEdgeByPair.end()) {
            row.handle = graph.newEdge(source->second, target->second);
            geometricEdgeByPair.emplace(key, row.handle);
          } else {
            row.handle = existing->second;
          }
        } else {
          row.handle = graph.newEdge(source->second, target->second);
        }
        edges.push_back(std::move(row));
      }
    }

    if (algorithm == "geometric") {
      // CGAL's arrangement memory grows super-linearly with the product of a
      // moved node's degree and sampled background edges. Keep every research
      // invocation bounded; use checkpoints instead of one unbounded pass.
      const int passes = std::clamp(
        integerArg(argv, argc, "--passes", 1), 1, 2);
      const int nodeLimit = std::clamp(
        integerArg(argv, argc, "--node-limit", 8), 1, 8);
      const int pointSamples = std::clamp(
        integerArg(argv, argc, "--point-samples", 32), 1, 64);
      const int edgeSamples = std::clamp(
        integerArg(argv, argc, "--edge-samples", 16), 4, 16);
      const int neighborhood = std::clamp(
        integerArg(argv, argc, "--neighborhood", 4), 2, 8);
      const int maxArrangementWork = std::clamp(
        integerArg(argv, argc, "--max-arrangement-work", 256), 64, 256);
      const double extent = std::max(
        0.01, doubleArg(argv, argc, "--extent", 0.35));
      const double minimumDistance = std::max(
        0.0, doubleArg(argv, argc, "--min-distance", 0.0));
      const double jitterRatio = std::max(
        0.0, doubleArg(argv, argc, "--jitter-ratio", 1e-9));
      const int jitterTrials = std::clamp(
        integerArg(argv, argc, "--jitter-trials", 4), 1, 4);
      const std::string geometricMode = optionalStringArg(
        argv, argc, "--geometric-mode", "weighted");

      std::unique_ptr<ogdf::CrossingMinimalPositionFast> positioner;
      std::unique_ptr<ogdf::CrossingMinimalPositionPrecise> precisePositioner;
      if (geometricMode == "weighted") {
        positioner = std::make_unique<
          ogdf::CrossingMinimalPositionApxWeighted>();
      } else if (geometricMode == "approx") {
        positioner = std::make_unique<ogdf::CrossingMinimalPositionApx>();
      } else if (geometricMode == "exact") {
        throw std::runtime_error(
          "exact geometric mode is disabled: unbounded arrangement memory");
      } else if (geometricMode == "precise") {
        throw std::runtime_error(
          "precise geometric mode is disabled: unbounded arrangement memory");
      } else {
        throw std::runtime_error(
          "unknown --geometric-mode: " + geometricMode);
      }
      if (geometricMode != "exact" && geometricMode != "precise") {
        positioner->setSampleSize(
          static_cast<unsigned>(edgeSamples),
          static_cast<unsigned>(pointSamples));
        positioner->setNeighboorhoodThreshold(
          static_cast<unsigned>(neighborhood));
      }

      double xMin = std::numeric_limits<double>::infinity();
      double yMin = std::numeric_limits<double>::infinity();
      double xMax = -std::numeric_limits<double>::infinity();
      double yMax = -std::numeric_limits<double>::infinity();
      for (const NodeRow& row : nodes) {
        xMin = std::min(xMin, attributes.x(row.handle));
        yMin = std::min(yMin, attributes.y(row.handle));
        xMax = std::max(xMax, attributes.x(row.handle));
        yMax = std::max(yMax, attributes.y(row.handle));
      }
      const double xSpan = std::max(1.0, xMax - xMin);
      const double ySpan = std::max(1.0, yMax - yMin);
      const long long beforeJitter = crossingCount(attributes, edges);
      const double jitterAmplitude = std::max(xSpan, ySpan) * jitterRatio;
      const long long afterJitter = perturbToGeneralPosition(
        attributes,
        nodes,
        edges,
        jitterAmplitude,
        jitterTrials,
        seed);
      const double boundXMin = xMin - xSpan * extent;
      const double boundYMin = yMin - ySpan * extent;
      const double boundXMax = xMax + xSpan * extent;
      const double boundYMax = yMax + ySpan * extent;
      if (precisePositioner) {
        precisePositioner->setBoundingBox(
          boundXMin, boundYMin, boundXMax, boundYMax);
      } else {
        positioner->setBoundingBox(
          boundXMin, boundYMin, boundXMax, boundYMax);
      }

      long long tracked = afterJitter;
      std::cerr << "geometric start crossings=" << tracked
                << " beforeJitter=" << beforeJitter
                << " jitter=" << jitterAmplitude
                << " mode=" << geometricMode
                << " passes=" << passes
                << " nodeLimit=" << nodeLimit
                << " edgeSampleCap=" << edgeSamples
                << " arrangementWorkCap=" << maxArrangementWork << '\n';
      for (int pass = 0; pass < passes; ++pass) {
        const auto scores = crossingScores(attributes, nodes, edges);
        int accepted = 0;
        int evaluated = 0;
        long long gain = 0;
        for (const auto& scored : scores) {
          const ogdf::node moved = scored.second;
          if (scored.first <= 0 || moved->degree() <= 1) continue;
          if (evaluated >= nodeLimit) break;
          ++evaluated;
          const int safeEdgeSamples = std::max(
            4,
            std::min(
              edgeSamples,
              maxArrangementWork / std::max(1, moved->degree())));
          positioner->setSampleSize(
            static_cast<unsigned>(safeEdgeSamples),
            static_cast<unsigned>(pointSamples));
          const long long before = incidentCrossingCount(
            attributes, edges, moved);
          const ogdf::DPoint oldPosition = position(attributes, moved);
          const ogdf::DPoint candidate = precisePositioner
            ? precisePositioner->call(attributes, moved)
            : positioner->call(attributes, moved);
          if (!std::isfinite(candidate.m_x)
              || !std::isfinite(candidate.m_y)
              || !separatedFromOtherNodes(
                attributes, candidate, moved, nodes, minimumDistance)) {
            continue;
          }
          attributes.x(moved) = candidate.m_x;
          attributes.y(moved) = candidate.m_y;
          const long long after = incidentCrossingCount(
            attributes, edges, moved);
          if (after < before) {
            tracked -= before - after;
            gain += before - after;
            ++accepted;
          } else {
            attributes.x(moved) = oldPosition.m_x;
            attributes.y(moved) = oldPosition.m_y;
          }
        }
        const long long verified = crossingCount(attributes, edges);
        if (verified != tracked) {
          throw std::runtime_error(
            "crossing count drift: tracked=" + std::to_string(tracked)
            + " verified=" + std::to_string(verified));
        }
        std::cerr << "geometric pass=" << (pass + 1)
                  << " evaluated=" << evaluated
                  << " accepted=" << accepted
                  << " gain=" << gain
                  << " crossings=" << tracked << '\n';
        if (accepted == 0) break;
      }
    } else if (algorithm == "davidson") {
      ogdf::DavidsonHarelLayout layout;
      layout.fixSettings(ogdf::DavidsonHarelLayout::SettingsParameter::Planar);
      layout.setIterationNumberAsFactor(false);
      layout.setNumberOfIterations(
        std::max(1, integerArg(argv, argc, "--iterations", 1000)));
      layout.setStartTemperature(
        std::max(1, integerArg(argv, argc, "--temperature", 240)));
      layout.setRepulsionWeight(900.0);
      layout.setAttractionWeight(250.0);
      layout.setNodeOverlapWeight(1450.0);
      layout.setPlanarityWeight(
        static_cast<double>(std::max(
          1, integerArg(argv, argc, "--planarity-weight", 10000))));
      layout.setPreferredEdgeLength(140.0);
      layout.call(attributes);
    } else if (algorithm == "planarization") {
      auto* planarizer = new ogdf::SubgraphPlanarizer();
      auto* subgraph = new ogdf::PlanarSubgraphFast<int>();
      auto* inserter = new ogdf::VariableEmbeddingInserter();
      subgraph->runs(runs);
      subgraph->maxThreads(static_cast<unsigned>(threads));
      inserter->removeReinsert(
        removeReinsert
          ? ogdf::RemoveReinsertType::IncInserted
          : ogdf::RemoveReinsertType::None);
      planarizer->setSubgraph(subgraph);
      planarizer->setInserter(inserter);
      planarizer->permutations(permutations);
      planarizer->maxThreads(static_cast<unsigned>(threads));

      ogdf::PlanarizationLayout layout;
      layout.setCrossMin(planarizer);
      layout.pageRatio(1.0);
      layout.call(attributes);
    } else {
      throw std::runtime_error("unknown --algorithm: " + algorithm);
    }

    std::cout << std::fixed << std::setprecision(9);
    for (const NodeRow& row : nodes) {
      std::cout << "N\t" << row.id << '\t'
                << attributes.x(row.handle) << '\t'
                << attributes.y(row.handle) << '\n';
    }
    for (const EdgeRow& row : edges) {
      std::cout << "E\t" << row.id << '\t'
                << row.source << '\t' << row.target;
      const ogdf::node source = row.handle->source();
      const ogdf::node target = row.handle->target();
      std::cout << '\t' << attributes.x(source) << '\t'
                << attributes.y(source);
      for (const ogdf::DPoint& bend : attributes.bends(row.handle)) {
        std::cout << '\t' << bend.m_x << '\t' << bend.m_y;
      }
      std::cout << '\t' << attributes.x(target) << '\t'
                << attributes.y(target) << '\n';
    }
    std::cerr << "guide nodes=" << nodes.size()
              << " edges=" << edges.size()
              << " runs=" << runs
              << " permutations=" << permutations
              << " seed=" << seed
              << " algorithm=" << algorithm;
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
    if (optimizerLock >= 0) {
      flock(optimizerLock, LOCK_UN);
      close(optimizerLock);
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
