// Fresh-coordinate candidate generator. Replayed TSV runs are diagnostics,
// never an uncached source-analysis benchmark. No saved model positions,
// hidden relationships, bends or crossing-pair matrices are used.
#include "straightVisualOptimization.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace djerd;
namespace {
std::vector<std::string> split(const std::string& row) {
  std::vector<std::string> fields; std::istringstream input(row); std::string field;
  while (std::getline(input, field, '\t')) fields.push_back(field);
  return fields;
}
std::string arg(int argc, char** argv, const std::string& key, const std::string& fallback = "") {
  for (int i = 1; i + 1 < argc; ++i) if (argv[i] == key) return argv[i + 1];
  if (!fallback.empty()) return fallback;
  throw std::invalid_argument("missing " + key);
}
void printScore(const StraightVisualScore& score) {
  std::cout << "{\"visual\":" << score.visual() << ",\"crossings\":" << score.edgeCrossings
    << ",\"edgeNode\":" << score.edgeNodeIntersections << ",\"overlaps\":" << score.nodeOverlaps
    << ",\"invalidRoutes\":" << score.invalidRoutes << '}';
}
}  // namespace
int main(int argc, char** argv) {
  try {
    std::vector<StraightVisualNode> nodes;
    std::vector<std::string> ids, edgeIds;
    std::unordered_map<std::string, std::size_t> index;
    std::string line;
    std::ifstream input(arg(argc, argv, "--nodes"));
    if (!input) throw std::runtime_error("cannot read cards");
    while (std::getline(input, line)) {
      if (line.empty()) continue;
      const auto row = split(line);
      if (row.size() < 3 || !index.emplace(row[0], nodes.size()).second) throw std::runtime_error("invalid card id");
      ids.push_back(row[0]); nodes.push_back({std::stod(row[1]), std::stod(row[2]), 0, 0});
    }
    input.close(); input.open(arg(argc, argv, "--positions"));
    if (!input) throw std::runtime_error("cannot read coordinates");
    std::unordered_set<std::string> seen;
    while (std::getline(input, line)) {
      if (line.empty()) continue;
      const auto row = split(line);
      if (row.size() < 3 || !index.count(row[0]) || !seen.insert(row[0]).second) throw std::runtime_error("invalid coordinate id");
      auto& node = nodes[index.at(row[0])]; node.x = std::stod(row[1]); node.y = std::stod(row[2]);
    }
    if (seen.size() != nodes.size()) throw std::runtime_error("incomplete coordinates");
    input.close(); input.open(arg(argc, argv, "--edges"));
    if (!input) throw std::runtime_error("cannot read relationships");
    std::vector<StraightVisualEdge> edges;
    seen.clear();
    while (std::getline(input, line)) {
      if (line.empty()) continue;
      const auto row = split(line);
      if (row.size() < 3 || !index.count(row[1]) || !index.count(row[2]) || !seen.insert(row[0]).second) throw std::runtime_error("invalid relationship id");
      const auto source = index.at(row[1]), target = index.at(row[2]);
      if (source == target) continue;
      edges.push_back({source, target}); edgeIds.push_back(row[0]);
    }
    const auto out = arg(argc, argv, "--out");
    StraightVisualPlacementOptions options;
    options.budgetMs = std::stod(arg(argc, argv, "--budget-ms", "30000"));
    options.maxRounds = std::stoi(arg(argc, argv, "--rounds", "100"));
    options.angularSamples = std::stoi(arg(argc, argv, "--angular", "8"));
    options.nodeLimit = std::stoi(arg(argc, argv, "--node-limit", "0"));
    options.swapLimit = std::stoi(arg(argc, argv, "--swap-limit", "80"));
    options.seed = std::stoull(arg(argc, argv, "--seed", "42"));
    std::vector<std::vector<std::size_t>> adjacent(nodes.size());
    std::vector<std::vector<std::size_t>> incident(nodes.size());
    for (std::size_t i = 0; i < edges.size(); ++i) {
      const auto& edge = edges[i];
      adjacent[edge.source].push_back(edge.target); adjacent[edge.target].push_back(edge.source);
      incident[edge.source].push_back(i); incident[edge.target].push_back(i);
    }
    for (auto& neighbors : adjacent) {
      std::sort(neighbors.begin(), neighbors.end()); neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
    for (auto& neighbors : adjacent) std::sort(neighbors.begin(), neighbors.end(), [&](auto a, auto b) {
      return adjacent[a].size() != adjacent[b].size() ? adjacent[a].size() > adjacent[b].size() : ids[a] < ids[b];
    });
    std::vector<std::vector<std::size_t>> groups;
    const auto groupsPath = arg(argc, argv, "--groups", "-");
    if (groupsPath != "-") {
      std::vector<std::string> keys(nodes.size());
      for (std::size_t i = 0; i < nodes.size(); ++i) keys[i] = ids[i];
      std::ifstream groupInput(groupsPath);
      if (!groupInput) throw std::runtime_error("cannot read graph communities");
      while (std::getline(groupInput, line)) {
        if (line.empty()) continue;
        const auto row = split(line);
        if (row.size() < 2 || !index.count(row[0])) throw std::runtime_error("invalid community node");
        keys[index.at(row[0])] = row[1];
      }
      // Reattach only graph-derived pendant trees to their fresh core group.
      const auto none = std::numeric_limits<std::size_t>::max();
      std::vector<std::size_t> degree(nodes.size()), parent(nodes.size(), none), peeled;
      std::vector<bool> active(nodes.size(), true);
      std::queue<std::size_t> queue;
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        degree[i] = adjacent[i].size(); if (degree[i] == 1) queue.push(i);
      }
      while (!queue.empty()) {
        const auto node = queue.front(); queue.pop();
        if (!active[node] || degree[node] != 1) continue;
        for (const auto neighbor : adjacent[node]) {
          if (!active[neighbor]) continue;
          parent[node] = neighbor; active[node] = false; peeled.push_back(node);
          if (--degree[neighbor] == 1) queue.push(neighbor);
          break;
        }
      }
      for (auto i = peeled.rbegin(); i != peeled.rend(); ++i) keys[*i] = keys[parent[*i]];
      std::map<std::string, std::vector<std::size_t>> byKey;
      for (std::size_t i = 0; i < keys.size(); ++i) byKey[keys[i]].push_back(i);
      for (const auto& item : byKey) if (item.second.size() > 1) groups.push_back(item.second);
    }
    const auto result = optimizeStraightVisualPlacement(nodes, edges, ids, groups, options);
    std::ofstream output(out);
    if (!output) throw std::runtime_error("cannot write coordinates");
    output << std::setprecision(17);
    for (std::size_t i = 0; i < nodes.size(); ++i) output << ids[i] << '\t' << result.nodes[i].x << '\t' << result.nodes[i].y << '\n';
    std::ofstream routeOutput(out + ".routes.tsv"); routeOutput << std::setprecision(17);
    if (!routeOutput) throw std::runtime_error("cannot write complete routes");
    for (std::size_t i = 0; i < edges.size(); ++i) {
      const auto& line = result.routes[i];
      routeOutput << edgeIds[i] << '\t' << line.sourceX << '\t' << line.sourceY << '\t' << line.targetX << '\t' << line.targetY << '\n';
    }
    std::cout << "{\"diagnosticOnly\":true,\"nodes\":" << nodes.size() << ",\"edges\":" << edges.size() << ",\"before\":";
    printScore(result.before); std::cout << ",\"after\":"; printScore(result.after);
    std::cout << ",\"moves\":" << result.moves << ",\"evaluations\":" << result.evaluations << ",\"seconds\":" << result.elapsedMs / 1000
      << ",\"budgetHit\":" << (result.budgetHit ? "true" : "false") << "}\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
