#include "crossingLowerBound.h"

#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <ogdf/basic/EdgeArray.h>
#include <ogdf/basic/SList.h>
#include <ogdf/planarity/BoyerMyrvold.h>
#include <ogdf/planarity/ExtractKuratowskis.h>

namespace djerd {
namespace {

using Certificate = CrossingLowerBoundCertificate;

struct CanonicalNode {
  std::string id;
};

struct CanonicalEdge {
  std::size_t index = 0;
  std::size_t source = 0;
  std::size_t target = 0;
  std::string id;
};

struct CanonicalGraph {
  std::vector<CanonicalNode> nodes;
  std::vector<CanonicalEdge> edges;
  std::vector<std::vector<std::pair<std::size_t, std::size_t>>> adjacency;
  bool inputWasSimple = true;
};

struct KernelEdge {
  std::size_t source = 0;
  std::size_t target = 0;
  std::vector<std::size_t> support;
};

struct SuppressedKernel {
  std::vector<KernelEdge> edges;
  std::vector<std::vector<std::pair<std::size_t, std::size_t>>> adjacency;
};

struct K3nCandidate {
  std::array<std::size_t, 3> left{};
  std::vector<std::size_t> right;
  std::vector<std::size_t> support;
  std::size_t contribution = 0;
  std::string signature;
};

struct KuratowskiCandidate {
  Certificate::Kind kind = Certificate::Kind::KuratowskiK33Subdivision;
  std::vector<std::size_t> branchVertices;
  std::vector<std::size_t> support;
  std::size_t residualCoreEdges = 0;
  std::string signature;
};

struct SubdivisionShape {
  bool valid = false;
  Certificate::Kind kind = Certificate::Kind::KuratowskiK33Subdivision;
  std::vector<std::size_t> branchVertices;
};

struct TripleOccurrence {
  std::array<std::size_t, 3> left{};
  std::size_t right = 0;
};

uint64_t fnv1a64(const std::string& value) {
  uint64_t hash = UINT64_C(1469598103934665603);
  for (const unsigned char ch : value) {
    hash ^= static_cast<uint64_t>(ch);
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

uint64_t splitmix64(uint64_t value) {
  value += UINT64_C(0x9e3779b97f4a7c15);
  value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

uint64_t orderHash(const std::string& value, uint64_t salt) {
  return splitmix64(fnv1a64(value) ^ salt);
}

std::string defaultNodeId(ogdf::node node) {
  return "node:" + std::to_string(node->index());
}

std::string defaultEdgeId(ogdf::edge edge) {
  return "edge:" + std::to_string(edge->index());
}

CanonicalGraph canonicalizeGraph(
    const ogdf::Graph& input,
    const ogdf::NodeArray<std::string>* nodeIds,
    const ogdf::EdgeArray<std::string>* edgeIds) {
  struct InputNode {
    ogdf::node handle = nullptr;
    std::string id;
  };
  std::vector<InputNode> inputNodes;
  inputNodes.reserve(input.numberOfNodes());
  for (ogdf::node node : input.nodes) {
    const std::string id = nodeIds && !(*nodeIds)[node].empty()
      ? (*nodeIds)[node]
      : defaultNodeId(node);
    inputNodes.push_back({node, id});
  }
  std::sort(inputNodes.begin(), inputNodes.end(), [](const auto& left, const auto& right) {
    if (left.id != right.id) return left.id < right.id;
    return left.handle->index() < right.handle->index();
  });
  for (std::size_t index = 1; index < inputNodes.size(); ++index) {
    if (inputNodes[index - 1].id == inputNodes[index].id) {
      throw std::invalid_argument("crossing lower bound requires unique node ids");
    }
  }

  CanonicalGraph result;
  result.nodes.reserve(inputNodes.size());
  ogdf::NodeArray<std::size_t> canonicalIndex(
    input,
    std::numeric_limits<std::size_t>::max());
  for (std::size_t index = 0; index < inputNodes.size(); ++index) {
    canonicalIndex[inputNodes[index].handle] = index;
    result.nodes.push_back({inputNodes[index].id});
  }

  struct PendingEdge {
    std::size_t source = 0;
    std::size_t target = 0;
    std::string id;
    int inputIndex = 0;
  };
  std::vector<PendingEdge> pending;
  pending.reserve(input.numberOfEdges());
  for (ogdf::edge edge : input.edges) {
    std::size_t source = canonicalIndex[edge->source()];
    std::size_t target = canonicalIndex[edge->target()];
    if (source == target) {
      result.inputWasSimple = false;
      continue;
    }
    if (source > target) std::swap(source, target);
    std::string id = edgeIds && !(*edgeIds)[edge].empty()
      ? (*edgeIds)[edge]
      : defaultEdgeId(edge);
    pending.push_back({source, target, std::move(id), edge->index()});
  }
  std::sort(pending.begin(), pending.end(), [](const auto& left, const auto& right) {
    return std::tie(left.source, left.target, left.id, left.inputIndex)
      < std::tie(right.source, right.target, right.id, right.inputIndex);
  });

  std::unordered_set<std::string> usedEdgeIds;
  for (std::size_t index = 0; index < pending.size();) {
    std::size_t end = index + 1;
    while (
        end < pending.size()
        && pending[end].source == pending[index].source
        && pending[end].target == pending[index].target) {
      ++end;
    }
    if (end != index + 1) result.inputWasSimple = false;
    PendingEdge representative = pending[index];
    if (!usedEdgeIds.insert(representative.id).second) {
      result.inputWasSimple = false;
      representative.id += "#" + std::to_string(representative.inputIndex);
      while (!usedEdgeIds.insert(representative.id).second) {
        representative.id += "_";
      }
    }
    const std::size_t canonicalEdgeIndex = result.edges.size();
    result.edges.push_back({
      canonicalEdgeIndex,
      representative.source,
      representative.target,
      std::move(representative.id),
    });
    index = end;
  }

  result.adjacency.assign(result.nodes.size(), {});
  for (const CanonicalEdge& edge : result.edges) {
    result.adjacency[edge.source].push_back({edge.target, edge.index});
    result.adjacency[edge.target].push_back({edge.source, edge.index});
  }
  for (auto& neighbors : result.adjacency) {
    std::sort(neighbors.begin(), neighbors.end(), [&](const auto& left, const auto& right) {
      if (result.nodes[left.first].id != result.nodes[right.first].id) {
        return result.nodes[left.first].id < result.nodes[right.first].id;
      }
      return left.second < right.second;
    });
  }
  return result;
}

std::vector<char> computeTwoCore(
    const CanonicalGraph& graph,
    const std::vector<char>& activeEdges) {
  std::vector<std::size_t> degree(graph.nodes.size(), 0);
  for (const CanonicalEdge& edge : graph.edges) {
    if (!activeEdges[edge.index]) continue;
    ++degree[edge.source];
    ++degree[edge.target];
  }
  std::deque<std::size_t> queue;
  std::vector<char> inCore(graph.nodes.size(), true);
  for (std::size_t node = 0; node < degree.size(); ++node) {
    if (degree[node] < 2) queue.push_back(node);
  }
  while (!queue.empty()) {
    const std::size_t node = queue.front();
    queue.pop_front();
    if (!inCore[node]) continue;
    inCore[node] = false;
    for (const auto [neighbor, edgeIndex] : graph.adjacency[node]) {
      if (!activeEdges[edgeIndex] || !inCore[neighbor]) continue;
      if (degree[neighbor] > 0) --degree[neighbor];
      if (degree[neighbor] == 1) queue.push_back(neighbor);
    }
  }
  return inCore;
}

std::size_t countCoreEdges(
    const CanonicalGraph& graph,
    const std::vector<char>& activeEdges) {
  const std::vector<char> inCore = computeTwoCore(graph, activeEdges);
  std::size_t count = 0;
  for (const CanonicalEdge& edge : graph.edges) {
    if (activeEdges[edge.index] && inCore[edge.source] && inCore[edge.target]) ++count;
  }
  return count;
}

SuppressedKernel buildSuppressedKernel(
    const CanonicalGraph& graph,
    const std::vector<char>& activeEdges) {
  const std::vector<char> inCore = computeTwoCore(graph, activeEdges);
  std::vector<std::size_t> degree(graph.nodes.size(), 0);
  for (const CanonicalEdge& edge : graph.edges) {
    if (
        activeEdges[edge.index]
        && inCore[edge.source]
        && inCore[edge.target]) {
      ++degree[edge.source];
      ++degree[edge.target];
    }
  }

  SuppressedKernel result;
  result.adjacency.assign(graph.nodes.size(), {});
  std::vector<char> visited(graph.edges.size(), false);
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> retainedByPair;

  for (std::size_t start = 0; start < graph.nodes.size(); ++start) {
    if (!inCore[start] || degree[start] == 2) continue;
    for (const auto [firstNeighbor, firstEdge] : graph.adjacency[start]) {
      if (
          visited[firstEdge]
          || !activeEdges[firstEdge]
          || !inCore[firstNeighbor]) {
        continue;
      }
      std::vector<std::size_t> support;
      std::size_t previous = start;
      std::size_t current = firstNeighbor;
      std::size_t edgeIndex = firstEdge;
      while (true) {
        if (visited[edgeIndex]) {
          support.clear();
          break;
        }
        visited[edgeIndex] = true;
        support.push_back(edgeIndex);
        if (degree[current] != 2) break;

        bool advanced = false;
        for (const auto [next, nextEdge] : graph.adjacency[current]) {
          if (
              next == previous
              || !activeEdges[nextEdge]
              || !inCore[next]) {
            continue;
          }
          previous = current;
          current = next;
          edgeIndex = nextEdge;
          advanced = true;
          break;
        }
        if (!advanced) {
          support.clear();
          break;
        }
      }
      if (support.empty() || current == start || degree[current] == 2) continue;
      const auto pair = std::minmax(start, current);
      if (retainedByPair.count(pair)) continue;
      const std::size_t kernelEdgeIndex = result.edges.size();
      retainedByPair[pair] = kernelEdgeIndex;
      result.edges.push_back({pair.first, pair.second, std::move(support)});
    }
  }

  std::sort(result.edges.begin(), result.edges.end(), [](const auto& left, const auto& right) {
    return std::tie(left.source, left.target) < std::tie(right.source, right.target);
  });
  for (std::size_t index = 0; index < result.edges.size(); ++index) {
    const KernelEdge& edge = result.edges[index];
    result.adjacency[edge.source].push_back({edge.target, index});
    result.adjacency[edge.target].push_back({edge.source, index});
  }
  for (auto& neighbors : result.adjacency) {
    std::sort(neighbors.begin(), neighbors.end(), [&](const auto& left, const auto& right) {
      if (graph.nodes[left.first].id != graph.nodes[right.first].id) {
        return graph.nodes[left.first].id < graph.nodes[right.first].id;
      }
      return left.second < right.second;
    });
  }
  return result;
}

std::string joinNodeIds(
    const CanonicalGraph& graph,
    const std::vector<std::size_t>& nodes) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    if (index > 0) stream << ',';
    stream << graph.nodes[nodes[index]].id;
  }
  return stream.str();
}

std::string supportSignature(const std::vector<std::size_t>& support) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < support.size(); ++index) {
    if (index > 0) stream << ',';
    stream << support[index];
  }
  return stream.str();
}

std::vector<K3nCandidate> findK3nCandidates(
    const CanonicalGraph& graph,
    const SuppressedKernel& kernel,
    const CrossingLowerBoundOptions& options,
    std::size_t& tripleOccurrences,
    bool& workLimitReached) {
  std::size_t estimated = 0;
  for (const auto& neighbors : kernel.adjacency) {
    const std::size_t degree = neighbors.size();
    if (degree < 3) continue;
    const std::size_t combinations = degree * (degree - 1) * (degree - 2) / 6;
    if (combinations > options.maxTripleOccurrences - std::min(
        estimated, options.maxTripleOccurrences)) {
      workLimitReached = true;
      return {};
    }
    estimated += combinations;
  }
  tripleOccurrences += estimated;

  std::vector<TripleOccurrence> occurrences;
  occurrences.reserve(estimated);
  for (std::size_t right = 0; right < kernel.adjacency.size(); ++right) {
    const auto& neighbors = kernel.adjacency[right];
    for (std::size_t first = 0; first < neighbors.size(); ++first) {
      for (std::size_t second = first + 1; second < neighbors.size(); ++second) {
        for (std::size_t third = second + 1; third < neighbors.size(); ++third) {
          std::array<std::size_t, 3> left{
            neighbors[first].first,
            neighbors[second].first,
            neighbors[third].first,
          };
          std::sort(left.begin(), left.end());
          occurrences.push_back({left, right});
        }
      }
    }
  }
  std::sort(occurrences.begin(), occurrences.end(), [](const auto& left, const auto& right) {
    return std::tie(left.left, left.right) < std::tie(right.left, right.right);
  });

  std::map<std::pair<std::size_t, std::size_t>, std::size_t> kernelEdgeByPair;
  for (std::size_t index = 0; index < kernel.edges.size(); ++index) {
    kernelEdgeByPair[{kernel.edges[index].source, kernel.edges[index].target}] = index;
  }

  std::vector<K3nCandidate> candidates;
  for (std::size_t begin = 0; begin < occurrences.size();) {
    std::size_t end = begin + 1;
    while (end < occurrences.size() && occurrences[end].left == occurrences[begin].left) ++end;
    const std::size_t rightCount = end - begin;
    if (rightCount >= options.minimumK3nRightSize) {
      K3nCandidate candidate;
      candidate.left = occurrences[begin].left;
      candidate.right.reserve(rightCount);
      for (std::size_t index = begin; index < end; ++index) {
        candidate.right.push_back(occurrences[index].right);
      }
      std::sort(candidate.right.begin(), candidate.right.end());
      bool complete = true;
      for (const std::size_t left : candidate.left) {
        for (const std::size_t right : candidate.right) {
          const auto pair = std::minmax(left, right);
          auto found = kernelEdgeByPair.find(pair);
          if (found == kernelEdgeByPair.end()) {
            complete = false;
            break;
          }
          const auto& path = kernel.edges[found->second].support;
          candidate.support.insert(candidate.support.end(), path.begin(), path.end());
        }
        if (!complete) break;
      }
      if (complete) {
        std::sort(candidate.support.begin(), candidate.support.end());
        if (std::adjacent_find(candidate.support.begin(), candidate.support.end())
            == candidate.support.end()) {
          const std::size_t n = candidate.right.size();
          candidate.contribution = (n / 2) * ((n - 1) / 2);
          const std::vector<std::size_t> leftVector(
            candidate.left.begin(), candidate.left.end());
          candidate.signature = "K3," + std::to_string(n)
            + "|L=" + joinNodeIds(graph, leftVector)
            + "|R=" + joinNodeIds(graph, candidate.right)
            + "|E=" + supportSignature(candidate.support);
          candidates.push_back(std::move(candidate));
        }
      }
    }
    begin = end;
  }
  std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
    if (left.contribution != right.contribution) {
      return left.contribution > right.contribution;
    }
    if (left.right.size() != right.right.size()) {
      return left.right.size() > right.right.size();
    }
    if (left.support.size() != right.support.size()) {
      return left.support.size() < right.support.size();
    }
    return left.signature < right.signature;
  });
  return candidates;
}

CertifiedSupportEdge supportEdgeRef(
    const CanonicalGraph& graph,
    std::size_t edgeIndex) {
  const CanonicalEdge& edge = graph.edges[edgeIndex];
  return {
    edge.index,
    edge.id,
    graph.nodes[edge.source].id,
    graph.nodes[edge.target].id,
  };
}

Certificate makeK3nCertificate(
    const CanonicalGraph& graph,
    const K3nCandidate& candidate) {
  Certificate certificate;
  certificate.kind = Certificate::Kind::K3nSubdivision;
  certificate.contribution = candidate.contribution;
  for (const std::size_t node : candidate.left) {
    certificate.leftVertices.push_back(graph.nodes[node].id);
  }
  for (const std::size_t node : candidate.right) {
    certificate.rightVertices.push_back(graph.nodes[node].id);
  }
  for (const std::size_t edgeIndex : candidate.support) {
    certificate.supportEdges.push_back(supportEdgeRef(graph, edgeIndex));
  }
  certificate.signature = candidate.signature;
  return certificate;
}

std::vector<char> activeWithout(
    const std::vector<char>& active,
    const std::vector<std::size_t>& removed) {
  std::vector<char> result = active;
  for (const std::size_t edgeIndex : removed) result[edgeIndex] = false;
  return result;
}

struct OgdfWorkGraph {
  ogdf::Graph graph;
  ogdf::EdgeArray<std::size_t> canonicalEdgeIndex;

  OgdfWorkGraph() : canonicalEdgeIndex(graph, std::numeric_limits<std::size_t>::max()) { }
};

OgdfWorkGraph buildOgdfWorkGraph(
    const CanonicalGraph& canonical,
    const std::vector<char>& activeEdges,
    uint64_t salt) {
  OgdfWorkGraph result;
  const std::vector<char> inCore = computeTwoCore(canonical, activeEdges);
  std::vector<std::size_t> nodeOrder;
  for (std::size_t node = 0; node < canonical.nodes.size(); ++node) {
    if (inCore[node]) nodeOrder.push_back(node);
  }
  std::sort(nodeOrder.begin(), nodeOrder.end(), [&](std::size_t left, std::size_t right) {
    const uint64_t leftHash = orderHash(canonical.nodes[left].id, salt);
    const uint64_t rightHash = orderHash(canonical.nodes[right].id, salt);
    if (leftHash != rightHash) return leftHash < rightHash;
    return canonical.nodes[left].id < canonical.nodes[right].id;
  });
  std::vector<ogdf::node> copiedNode(canonical.nodes.size(), nullptr);
  for (const std::size_t node : nodeOrder) copiedNode[node] = result.graph.newNode();

  std::vector<std::size_t> edgeOrder;
  for (const CanonicalEdge& edge : canonical.edges) {
    if (
        activeEdges[edge.index]
        && inCore[edge.source]
        && inCore[edge.target]) {
      edgeOrder.push_back(edge.index);
    }
  }
  std::sort(edgeOrder.begin(), edgeOrder.end(), [&](std::size_t left, std::size_t right) {
    const CanonicalEdge& leftEdge = canonical.edges[left];
    const CanonicalEdge& rightEdge = canonical.edges[right];
    const uint64_t leftHash = orderHash(leftEdge.id, salt ^ UINT64_C(0xd1b54a32d192ed03));
    const uint64_t rightHash = orderHash(rightEdge.id, salt ^ UINT64_C(0xd1b54a32d192ed03));
    if (leftHash != rightHash) return leftHash < rightHash;
    return leftEdge.id < rightEdge.id;
  });
  for (const std::size_t edgeIndex : edgeOrder) {
    const CanonicalEdge& edge = canonical.edges[edgeIndex];
    ogdf::edge copied = result.graph.newEdge(copiedNode[edge.source], copiedNode[edge.target]);
    result.canonicalEdgeIndex[copied] = edgeIndex;
  }
  return result;
}

bool isBipartiteCompleteK33(
    const std::vector<std::vector<std::size_t>>& kernelAdjacency,
    const std::vector<std::size_t>& branchVertices) {
  if (branchVertices.size() != 6) return false;
  std::unordered_map<std::size_t, int> color;
  std::deque<std::size_t> queue;
  color[branchVertices.front()] = 0;
  queue.push_back(branchVertices.front());
  while (!queue.empty()) {
    const std::size_t node = queue.front();
    queue.pop_front();
    for (const std::size_t neighbor : kernelAdjacency[node]) {
      auto found = color.find(neighbor);
      if (found == color.end()) {
        color[neighbor] = 1 - color[node];
        queue.push_back(neighbor);
      } else if (found->second == color[node]) {
        return false;
      }
    }
  }
  std::size_t zero = 0;
  std::size_t one = 0;
  for (const std::size_t node : branchVertices) {
    if (color[node] == 0) ++zero;
    else ++one;
    if (kernelAdjacency[node].size() != 3) return false;
  }
  return zero == 3 && one == 3;
}

SubdivisionShape analyzeKuratowskiSubdivision(
    const CanonicalGraph& graph,
    const std::vector<std::size_t>& support) {
  SubdivisionShape shape;
  if (support.size() < 9) return shape;
  std::vector<std::vector<std::pair<std::size_t, std::size_t>>> adjacency(
    graph.nodes.size());
  for (const std::size_t edgeIndex : support) {
    if (edgeIndex >= graph.edges.size()) return shape;
    const CanonicalEdge& edge = graph.edges[edgeIndex];
    adjacency[edge.source].push_back({edge.target, edgeIndex});
    adjacency[edge.target].push_back({edge.source, edgeIndex});
  }
  for (std::size_t node = 0; node < adjacency.size(); ++node) {
    if (!adjacency[node].empty() && adjacency[node].size() != 2) {
      shape.branchVertices.push_back(node);
    }
  }
  const bool maybeK5 = shape.branchVertices.size() == 5
    && std::all_of(shape.branchVertices.begin(), shape.branchVertices.end(),
      [&](std::size_t node) { return adjacency[node].size() == 4; });
  const bool maybeK33 = shape.branchVertices.size() == 6
    && std::all_of(shape.branchVertices.begin(), shape.branchVertices.end(),
      [&](std::size_t node) { return adjacency[node].size() == 3; });
  if (!maybeK5 && !maybeK33) return shape;

  std::vector<char> visited(graph.edges.size(), false);
  std::vector<std::vector<std::size_t>> kernelAdjacency(graph.nodes.size());
  std::set<std::pair<std::size_t, std::size_t>> kernelEdges;
  for (const std::size_t start : shape.branchVertices) {
    for (const auto [firstNeighbor, firstEdge] : adjacency[start]) {
      if (visited[firstEdge]) continue;
      visited[firstEdge] = true;
      std::size_t previous = start;
      std::size_t current = firstNeighbor;
      while (adjacency[current].size() == 2) {
        const auto& first = adjacency[current][0];
        const auto& second = adjacency[current][1];
        const auto next = first.first == previous ? second : first;
        if (visited[next.second]) return shape;
        visited[next.second] = true;
        previous = current;
        current = next.first;
      }
      if (current == start || adjacency[current].empty()) return shape;
      const auto pair = std::minmax(start, current);
      if (!kernelEdges.insert(pair).second) return shape;
      kernelAdjacency[start].push_back(current);
      kernelAdjacency[current].push_back(start);
    }
  }
  for (const std::size_t edgeIndex : support) {
    if (!visited[edgeIndex]) return shape;
  }

  if (maybeK5) {
    if (kernelEdges.size() != 10) return shape;
    for (const std::size_t node : shape.branchVertices) {
      if (kernelAdjacency[node].size() != 4) return shape;
    }
    shape.valid = true;
    shape.kind = Certificate::Kind::KuratowskiK5Subdivision;
  } else {
    if (kernelEdges.size() != 9
        || !isBipartiteCompleteK33(kernelAdjacency, shape.branchVertices)) {
      return shape;
    }
    shape.valid = true;
    shape.kind = Certificate::Kind::KuratowskiK33Subdivision;
  }
  std::sort(shape.branchVertices.begin(), shape.branchVertices.end());
  return shape;
}

std::string kuratowskiSignature(
    const CanonicalGraph& graph,
    Certificate::Kind kind,
    const std::vector<std::size_t>& branches,
    const std::vector<std::size_t>& support) {
  return std::string(kind == Certificate::Kind::KuratowskiK5Subdivision ? "K5" : "K3,3")
    + "|B=" + joinNodeIds(graph, branches)
    + "|E=" + supportSignature(support);
}

std::vector<KuratowskiCandidate> extractKuratowskiCandidates(
    const CanonicalGraph& graph,
    const std::vector<char>& activeEdges,
    const CrossingLowerBoundOptions& options,
    std::size_t round,
    std::size_t& planarityCalls,
    bool& residualPlanar,
    bool& workLimitReached) {
  std::map<std::string, KuratowskiCandidate> unique;
  bool sawNonPlanar = false;
  for (std::size_t ordering = 0;
       ordering < options.deterministicOrderingsPerRound;
       ++ordering) {
    if (planarityCalls >= options.maxPlanarityCalls) {
      workLimitReached = true;
      break;
    }
    const uint64_t salt = splitmix64(
      UINT64_C(0x243f6a8885a308d3)
      ^ (static_cast<uint64_t>(round + 1) * UINT64_C(0x9e3779b97f4a7c15))
      ^ (static_cast<uint64_t>(ordering + 1) * UINT64_C(0xbf58476d1ce4e5b9)));
    OgdfWorkGraph work = buildOgdfWorkGraph(graph, activeEdges, salt);
    ogdf::BoyerMyrvold boyerMyrvold;
    ogdf::SList<ogdf::KuratowskiWrapper> wrappers;
    const bool planar = boyerMyrvold.planarEmbed(
      work.graph,
      wrappers,
      static_cast<int>(options.kuratowskiCandidatesPerOrdering),
      false,
      false,
      false,
      true);
    ++planarityCalls;
    if (planar) {
      residualPlanar = true;
      return {};
    }
    sawNonPlanar = true;

    for (ogdf::SListConstIterator<ogdf::KuratowskiWrapper> it = wrappers.begin();
         it.valid();
         ++it) {
      std::vector<std::size_t> support;
      for (ogdf::SListConstIterator<ogdf::edge> edgeIt = (*it).edgeList.begin();
           edgeIt.valid();
           ++edgeIt) {
        const std::size_t edgeIndex = work.canonicalEdgeIndex[*edgeIt];
        if (edgeIndex < graph.edges.size()) support.push_back(edgeIndex);
      }
      std::sort(support.begin(), support.end());
      support.erase(std::unique(support.begin(), support.end()), support.end());
      const SubdivisionShape shape = analyzeKuratowskiSubdivision(graph, support);
      if (!shape.valid) continue;
      KuratowskiCandidate candidate;
      candidate.kind = shape.kind;
      candidate.branchVertices = shape.branchVertices;
      candidate.support = std::move(support);
      candidate.signature = kuratowskiSignature(
        graph, candidate.kind, candidate.branchVertices, candidate.support);
      unique.emplace(candidate.signature, std::move(candidate));
    }
    bool hasNineEdgeCertificate = false;
    for (const auto& entry : unique) {
      if (entry.second.support.size() == 9) {
        hasNineEdgeCertificate = true;
        break;
      }
    }
    if (hasNineEdgeCertificate) break;
  }
  if (!sawNonPlanar) residualPlanar = true;

  std::vector<KuratowskiCandidate> candidates;
  candidates.reserve(unique.size());
  for (auto& entry : unique) {
    KuratowskiCandidate candidate = std::move(entry.second);
    candidate.residualCoreEdges = countCoreEdges(
      graph,
      activeWithout(activeEdges, candidate.support));
    candidates.push_back(std::move(candidate));
  }
  std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
    if (left.support.size() != right.support.size()) {
      return left.support.size() < right.support.size();
    }
    if (left.residualCoreEdges != right.residualCoreEdges) {
      return left.residualCoreEdges > right.residualCoreEdges;
    }
    return left.signature < right.signature;
  });
  return candidates;
}

Certificate makeKuratowskiCertificate(
    const CanonicalGraph& graph,
    const KuratowskiCandidate& candidate) {
  Certificate certificate;
  certificate.kind = candidate.kind;
  certificate.contribution = 1;
  for (const std::size_t node : candidate.branchVertices) {
    certificate.branchVertices.push_back(graph.nodes[node].id);
  }
  for (const std::size_t edgeIndex : candidate.support) {
    certificate.supportEdges.push_back(supportEdgeRef(graph, edgeIndex));
  }
  certificate.signature = candidate.signature;
  return certificate;
}

bool isResidualPlanar(
    const CanonicalGraph& graph,
    const std::vector<char>& activeEdges,
    std::size_t& planarityCalls,
    const CrossingLowerBoundOptions& options,
    bool& workLimitReached) {
  if (countCoreEdges(graph, activeEdges) == 0) return true;
  if (planarityCalls >= options.maxPlanarityCalls) {
    workLimitReached = true;
    return false;
  }
  OgdfWorkGraph work = buildOgdfWorkGraph(graph, activeEdges, 0);
  ogdf::BoyerMyrvold boyerMyrvold;
  ++planarityCalls;
  return boyerMyrvold.isPlanar(work.graph);
}

struct LocalCertificateGraph {
  std::vector<std::string> nodeIds;
  std::vector<std::pair<std::size_t, std::size_t>> edges;
  std::vector<std::vector<std::pair<std::size_t, std::size_t>>> adjacency;
  std::unordered_map<std::string, std::size_t> nodeById;
};

LocalCertificateGraph buildLocalCertificateGraph(const Certificate& certificate) {
  LocalCertificateGraph graph;
  for (const CertifiedSupportEdge& edge : certificate.supportEdges) {
    for (const std::string* id : {&edge.sourceId, &edge.targetId}) {
      if (!graph.nodeById.count(*id)) {
        const std::size_t index = graph.nodeIds.size();
        graph.nodeById[*id] = index;
        graph.nodeIds.push_back(*id);
      }
    }
    graph.edges.push_back({graph.nodeById[edge.sourceId], graph.nodeById[edge.targetId]});
  }
  graph.adjacency.assign(graph.nodeIds.size(), {});
  for (std::size_t index = 0; index < graph.edges.size(); ++index) {
    const auto [source, target] = graph.edges[index];
    graph.adjacency[source].push_back({target, index});
    graph.adjacency[target].push_back({source, index});
  }
  return graph;
}

bool traceCertificateKernel(
    const LocalCertificateGraph& graph,
    std::vector<std::size_t>& branches,
    std::set<std::pair<std::size_t, std::size_t>>& kernelEdges) {
  for (std::size_t node = 0; node < graph.nodeIds.size(); ++node) {
    if (!graph.adjacency[node].empty() && graph.adjacency[node].size() != 2) {
      branches.push_back(node);
    }
  }
  std::vector<char> visited(graph.edges.size(), false);
  for (const std::size_t start : branches) {
    for (const auto [firstNeighbor, firstEdge] : graph.adjacency[start]) {
      if (visited[firstEdge]) continue;
      visited[firstEdge] = true;
      std::size_t previous = start;
      std::size_t current = firstNeighbor;
      while (graph.adjacency[current].size() == 2) {
        const auto& first = graph.adjacency[current][0];
        const auto& second = graph.adjacency[current][1];
        const auto next = first.first == previous ? second : first;
        if (visited[next.second]) return false;
        visited[next.second] = true;
        previous = current;
        current = next.first;
      }
      if (current == start || graph.adjacency[current].empty()) return false;
      if (!kernelEdges.insert(std::minmax(start, current)).second) return false;
    }
  }
  return std::all_of(visited.begin(), visited.end(), [](char value) { return value; });
}

bool fail(std::string* reason, const std::string& message) {
  if (reason) *reason = message;
  return false;
}

bool verifyCertificateStructure(
    const Certificate& certificate,
    std::string* failureReason) {
  if (certificate.supportEdges.empty()) {
    return fail(failureReason, "certificate has no support edges");
  }
  LocalCertificateGraph graph = buildLocalCertificateGraph(certificate);
  if (graph.edges.size() != certificate.supportEdges.size()) {
    return fail(failureReason, "certificate support graph size mismatch");
  }
  std::set<std::pair<std::size_t, std::size_t>> uniqueEdges;
  for (const auto edge : graph.edges) {
    if (edge.first == edge.second || !uniqueEdges.insert(std::minmax(edge.first, edge.second)).second) {
      return fail(failureReason, "certificate support graph is not simple");
    }
  }
  std::vector<std::size_t> branches;
  std::set<std::pair<std::size_t, std::size_t>> kernelEdges;
  if (!traceCertificateKernel(graph, branches, kernelEdges)) {
    return fail(failureReason, "certificate paths do not form a subdivision");
  }

  if (certificate.kind == Certificate::Kind::K3nSubdivision) {
    if (certificate.leftVertices.size() != 3 || certificate.rightVertices.size() < 3) {
      return fail(failureReason, "invalid K3,n branch-set sizes");
    }
    std::set<std::string> left(
      certificate.leftVertices.begin(), certificate.leftVertices.end());
    std::set<std::string> right(
      certificate.rightVertices.begin(), certificate.rightVertices.end());
    if (left.size() != 3 || right.size() != certificate.rightVertices.size()) {
      return fail(failureReason, "duplicate K3,n branch vertex");
    }
    for (const std::string& id : left) {
      if (right.count(id) || !graph.nodeById.count(id)) {
        return fail(failureReason, "invalid K3,n branch partition");
      }
    }
    if (branches.size() != left.size() + right.size()) {
      return fail(failureReason, "K3,n has unexpected branch vertices");
    }
    std::set<std::pair<std::size_t, std::size_t>> expected;
    for (const std::string& leftId : left) {
      for (const std::string& rightId : right) {
        expected.insert(std::minmax(graph.nodeById[leftId], graph.nodeById[rightId]));
      }
    }
    if (kernelEdges != expected) {
      return fail(failureReason, "K3,n kernel is incomplete");
    }
    const std::size_t n = right.size();
    if (certificate.contribution != (n / 2) * ((n - 1) / 2)) {
      return fail(failureReason, "K3,n contribution formula mismatch");
    }
    return true;
  }

  if (certificate.contribution != 1) {
    return fail(failureReason, "Kuratowski contribution must equal one");
  }
  if (certificate.kind == Certificate::Kind::KuratowskiK5Subdivision) {
    if (branches.size() != 5 || kernelEdges.size() != 10) {
      return fail(failureReason, "invalid K5 subdivision");
    }
    return true;
  }
  if (branches.size() != 6 || kernelEdges.size() != 9) {
    return fail(failureReason, "invalid K3,3 subdivision");
  }
  std::vector<std::vector<std::size_t>> kernelAdjacency(graph.nodeIds.size());
  for (const auto [source, target] : kernelEdges) {
    kernelAdjacency[source].push_back(target);
    kernelAdjacency[target].push_back(source);
  }
  if (!isBipartiteCompleteK33(kernelAdjacency, branches)) {
    return fail(failureReason, "K3,3 kernel is not complete bipartite");
  }
  return true;
}

}  // namespace

const char* crossingLowerBoundCertificateKindName(Certificate::Kind kind) {
  switch (kind) {
    case Certificate::Kind::K3nSubdivision:
      return "k3n-subdivision";
    case Certificate::Kind::KuratowskiK33Subdivision:
      return "kuratowski-k33-subdivision";
    case Certificate::Kind::KuratowskiK5Subdivision:
      return "kuratowski-k5-subdivision";
  }
  return "unknown";
}

CrossingLowerBoundReport computeCertifiedCrossingLowerBound(
    const ogdf::Graph& input,
    const ogdf::NodeArray<std::string>* nodeIds,
    const ogdf::EdgeArray<std::string>* edgeIds,
    const CrossingLowerBoundOptions& options) {
  const CanonicalGraph graph = canonicalizeGraph(input, nodeIds, edgeIds);
  CrossingLowerBoundReport report;
  report.nodeCount = graph.nodes.size();
  report.edgeCount = graph.edges.size();
  report.inputWasSimple = graph.inputWasSimple;
  std::vector<char> activeEdges(graph.edges.size(), true);

  report.planar = isResidualPlanar(
    graph, activeEdges, report.planarityCalls, options, report.workLimitReached);
  if (report.planar) {
    report.residualPlanar = true;
    std::string failure;
    report.invariantsVerified = verifyCrossingLowerBoundReport(report, &failure);
    if (!report.invariantsVerified) {
      throw std::logic_error("crossing lower-bound verification failed: " + failure);
    }
    return report;
  }

  for (std::size_t round = 0; round < options.maxK3nRounds; ++round) {
    const SuppressedKernel kernel = buildSuppressedKernel(graph, activeEdges);
    std::vector<K3nCandidate> candidates = findK3nCandidates(
      graph,
      kernel,
      options,
      report.tripleOccurrences,
      report.workLimitReached);
    if (candidates.empty()) break;
    const K3nCandidate& winner = candidates.front();
    Certificate certificate = makeK3nCertificate(graph, winner);
    for (const std::size_t edgeIndex : winner.support) {
      if (!activeEdges[edgeIndex]) {
        throw std::logic_error("K3,n packing selected an already-used edge");
      }
      activeEdges[edgeIndex] = false;
    }
    report.k3nContribution += certificate.contribution;
    ++report.k3nCertificateCount;
    report.certificates.push_back(std::move(certificate));
  }

  for (std::size_t round = 0; round < options.maxKuratowskiRounds; ++round) {
    bool residualPlanar = false;
    std::vector<KuratowskiCandidate> candidates = extractKuratowskiCandidates(
      graph,
      activeEdges,
      options,
      round,
      report.planarityCalls,
      residualPlanar,
      report.workLimitReached);
    if (residualPlanar) {
      report.residualPlanar = true;
      break;
    }
    if (candidates.empty()) {
      report.workLimitReached = true;
      break;
    }
    const KuratowskiCandidate& winner = candidates.front();
    Certificate certificate = makeKuratowskiCertificate(graph, winner);
    for (const std::size_t edgeIndex : winner.support) {
      if (!activeEdges[edgeIndex]) {
        throw std::logic_error("Kuratowski packing selected an already-used edge");
      }
      activeEdges[edgeIndex] = false;
    }
    report.kuratowskiContribution += 1;
    ++report.kuratowskiCertificateCount;
    report.certificates.push_back(std::move(certificate));
  }

  if (!report.residualPlanar && !report.workLimitReached) {
    report.residualPlanar = isResidualPlanar(
      graph, activeEdges, report.planarityCalls, options, report.workLimitReached);
  }
  report.totalLowerBound = report.k3nContribution + report.kuratowskiContribution;
  std::string failure;
  report.invariantsVerified = verifyCrossingLowerBoundReport(report, &failure);
  if (!report.invariantsVerified) {
    throw std::logic_error("crossing lower-bound verification failed: " + failure);
  }
  return report;
}

bool verifyCrossingLowerBoundReport(
    const CrossingLowerBoundReport& report,
    std::string* failureReason) {
  std::unordered_set<std::size_t> usedCanonicalEdges;
  std::unordered_set<std::string> usedEdgeIds;
  std::size_t total = 0;
  std::size_t k3nContribution = 0;
  std::size_t kuratowskiContribution = 0;
  std::size_t k3nCount = 0;
  std::size_t kuratowskiCount = 0;
  for (const Certificate& certificate : report.certificates) {
    if (!verifyCertificateStructure(certificate, failureReason)) return false;
    for (const CertifiedSupportEdge& edge : certificate.supportEdges) {
      if (edge.canonicalIndex >= report.edgeCount) {
        return fail(failureReason, "certificate edge index is out of range");
      }
      if (!usedCanonicalEdges.insert(edge.canonicalIndex).second) {
        return fail(failureReason, "certificate support edges are not globally disjoint");
      }
      if (!usedEdgeIds.insert(edge.edgeId).second) {
        return fail(failureReason, "certificate edge ids are not globally unique");
      }
      if (edge.sourceId.empty() || edge.targetId.empty() || edge.sourceId == edge.targetId) {
        return fail(failureReason, "certificate contains an invalid support edge");
      }
    }
    total += certificate.contribution;
    if (certificate.kind == Certificate::Kind::K3nSubdivision) {
      ++k3nCount;
      k3nContribution += certificate.contribution;
    } else {
      ++kuratowskiCount;
      kuratowskiContribution += certificate.contribution;
    }
  }
  if (
      total != report.totalLowerBound
      || k3nContribution != report.k3nContribution
      || kuratowskiContribution != report.kuratowskiContribution
      || k3nCount != report.k3nCertificateCount
      || kuratowskiCount != report.kuratowskiCertificateCount) {
    return fail(failureReason, "crossing lower-bound report totals do not match certificates");
  }
  if (report.planar && report.totalLowerBound != 0) {
    return fail(failureReason, "planar graph has a positive crossing lower bound");
  }
  if (report.method != kCrossingLowerBoundMethod || report.version != kCrossingLowerBoundVersion) {
    return fail(failureReason, "crossing lower-bound method/version mismatch");
  }
  if (failureReason) failureReason->clear();
  return true;
}

}  // namespace djerd
