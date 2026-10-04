// Low-memory clusterGraph coordinate probe.
//
// This executable deliberately stops before production route/carrier passes.
// It emits only real model centers; a separate exact scorer reconstructs every
// original relationship as an independent straight segment.

#include "clusterGraph.h"
#include "types.h"

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/resource.h>

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

std::string argument(char** argv, int argc, const std::string& name) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return argv[index + 1];
  }
  throw std::runtime_error("missing argument " + name);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string nodesPath = argument(argv, argc, "--nodes");
    const std::string edgesPath = argument(argv, argc, "--edges");
    const std::string labelsPath = argument(argv, argc, "--labels");
    ogdf::Graph graph;
    ogdf::GraphAttributes attributes(
      graph,
      ogdf::GraphAttributes::nodeGraphics
        | ogdf::GraphAttributes::edgeGraphics);
    std::vector<djerd::NodeRecord> nodes;
    std::unordered_map<std::string, std::size_t> indexById;
    {
      std::ifstream stream(nodesPath);
      if (!stream) throw std::runtime_error("cannot read " + nodesPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() < 3) throw std::runtime_error("bad node row");
        djerd::NodeRecord node;
        node.modelId = fields[0];
        node.width = std::max(1.0, std::stod(fields[1]));
        node.height = std::max(1.0, std::stod(fields[2]));
        node.x = fields.size() >= 5 ? std::stod(fields[3]) : 0.0;
        node.y = fields.size() >= 5 ? std::stod(fields[4]) : 0.0;
        node.handle = graph.newNode();
        attributes.width(node.handle) = node.width;
        attributes.height(node.handle) = node.height;
        attributes.x(node.handle) = node.x;
        attributes.y(node.handle) = node.y;
        indexById[node.modelId] = nodes.size();
        nodes.push_back(std::move(node));
      }
    }
    std::vector<djerd::EdgeRecord> edges;
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
        djerd::EdgeRecord edge;
        edge.edgeId = fields[0];
        edge.sourceModelId = fields[1];
        edge.targetModelId = fields[2];
        edge.kind = fields.size() >= 4 ? fields[3] : "structural";
        edge.provenance = fields.size() >= 5 ? fields[4] : "structural";
        edge.sourceHandle = nodes[source->second].handle;
        edge.targetHandle = nodes[target->second].handle;
        edge.handle = graph.newEdge(edge.sourceHandle, edge.targetHandle);
        edges.push_back(std::move(edge));
      }
    }
    std::unordered_map<std::string, std::string> labelById;
    {
      std::ifstream stream(labelsPath);
      if (!stream) throw std::runtime_error("cannot read " + labelsPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() >= 2) labelById[fields[0]] = fields[1];
      }
    }
    std::vector<std::string> labels;
    labels.reserve(nodes.size());
    for (const djerd::NodeRecord& node : nodes) {
      const auto found = labelById.find(node.modelId);
      labels.push_back(found == labelById.end() ? std::string{} : found->second);
    }
    const djerd::ClusterGraphResult result = djerd::runClusterGraphLayout(
      nodes, edges, labels, attributes, false);
    std::cout << std::fixed << std::setprecision(9);
    for (const djerd::NodeRecord& node : nodes) {
      std::cout << node.modelId << '\t'
                << attributes.x(node.handle) << '\t'
                << attributes.y(node.handle) << '\n';
    }
    std::cerr << "cluster-position nodes=" << nodes.size()
              << " edges=" << edges.size()
              << " clusters=" << result.clusters.size()
              << " pruned=" << result.prunedNodes.size()
              << " core=" << result.coreNodeCount;
    rusage usage{};
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
