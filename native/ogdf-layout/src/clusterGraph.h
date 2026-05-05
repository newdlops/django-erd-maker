#pragma once

// Cluster-graph layout pipeline implementing graph-terminology.md.
//
// Pipeline:
//   1. Build dedup adjacency (parallel edges → 1)
//   2. Take pre-computed cluster labels (from Louvain)
//   3. Pick a root per cluster (max-dedup-degree member)
//   4. Identify connector nodes (connected to 2+ different cluster roots)
//      → remove them from their original cluster
//   5. Classify remaining members as LEAF / INTERNAL / BRIDGE
//   6. Inner layout: radial (root at centre, leaves close, internal middle,
//      bridges on the boundary)
//   7. Build super-graph: cluster super-nodes + connector nodes + independent
//      nodes, with super-edges between connected clusters (root↔root or via
//      connector)
//   8. Super-graph layout: cross-min focused (Sugiyama on the small graph)
//   9. Compose: each node's final position = cluster super-node centre +
//      member local position; connectors & independents use super-graph
//      coords directly

#include "types.h"

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>

#include <string>
#include <utility>
#include <vector>

namespace djerd {

enum class MemberCategory {
  Root,
  Leaf,
  Internal,
  Bridge,
};

struct ClusterMemberInfo {
  std::size_t nodeIdx;
  MemberCategory category;
};

struct ClusterRecord {
  std::string clusterId;
  std::size_t rootIdx;
  std::vector<ClusterMemberInfo> members;        // includes root
  std::pair<double, double> localSize{0.0, 0.0}; // bbox after inner layout
  std::string constellationId;                   // empty = standalone
};

struct ConnectorRecord {
  std::size_t nodeIdx;
  std::vector<std::string> connectedClusterIds;  // exactly 2 entries
};

// Router = connector connected to 3+ different cluster roots. Becomes the
// root of a higher-tier cluster constellation.
struct RouterRecord {
  std::size_t nodeIdx;
  std::vector<std::string> connectedClusterIds;  // 3+ entries
  std::string constellationId;                   // = "_const_<routerIdx>"
};

struct ConstellationRecord {
  std::string constellationId;
  std::size_t routerIdx;
  std::vector<std::string> clusterIds;          // member clusters
  std::pair<double, double> localSize{0.0, 0.0};
};

// Ring = closed cycle in the top-level super-graph (length-3 triangles in
// the current implementation). Each entry stores the OGDF super-node
// pointers via opaque indices into the super-node vector built in
// runClusterGraphLayout. We expose only the count via metrics.
struct RingRecord {
  std::vector<std::size_t> superNodeIndices;    // 3+ entries (triangle = 3)
};

// Pruned node = a node removed during iterative leaf-pruning (graph-
// terminology.md §2.5). Each pruned node records when it was removed
// (level) and which neighbour it was attached to immediately before
// removal (parent). Used for reverse-order re-attachment in layout.
struct PrunedNodeRecord {
  std::size_t nodeIdx;
  std::size_t level;        // 1-based pruning level (1, 2, 3, ...)
  std::size_t parentIdx;    // attachment parent (= self if alone-root deg 0)
  bool isAloneRoot = false;
};

// Polar = cluster root that is also adjacent to 3+ OTHER cluster roots
// (= router-like connectivity but the node IS itself a root). Polars form
// the topmost structural skeleton of the graph. graph-terminology.md §3.5/3.6.
struct PolarRecord {
  std::size_t nodeIdx;
  std::string clusterId;                          // own cluster
  std::vector<std::string> connectedClusterIds;   // 3+ other-cluster roots
  double anchorX = 0.0;                           // final anchor x
  double anchorY = 0.0;                           // final anchor y
  std::size_t superPolarIdx = 0;                  // super-polar group index
};

// Super-polar = a meta-tier grouping above polars. When the polar count is
// large (≥4), the polar skeleton itself becomes complex and we need a
// higher-tier "simplest meta structure" to draw kink-free. Super-polars
// group polars (e.g., via BFS from highest-skeleton-degree seeds) and the
// super-polar meta-graph is laid out with cross-min priority.
struct SuperPolarRecord {
  std::size_t centerNodeIdx;                      // representative polar
  std::vector<std::size_t> memberPolarIdxs;       // member polar node indices
  double centerX = 0.0;
  double centerY = 0.0;
  double bboxDiam = 0.0;                          // computed bottom-up
  double polarRingRadius = 0.0;                   // polars sit at this radius
};

struct ClusterGraphResult {
  std::vector<ClusterRecord> clusters;
  std::vector<ConnectorRecord> connectors;
  std::vector<RouterRecord> routers;
  std::vector<ConstellationRecord> constellations;
  std::vector<PolarRecord> polars;
  std::vector<SuperPolarRecord> superPolars;
  std::vector<std::size_t> independentNodeIndices;  // not in any cluster
  std::size_t topLevelEdgeCount = 0;
  std::size_t deduplicatedEdges = 0;
  std::size_t ringCount = 0;
  std::size_t polarSkeletonEdgeCount = 0;
  std::size_t polarRingCount = 0;
  std::size_t polarLineCount = 0;
  std::size_t superPolarMetaEdgeCount = 0;
  std::size_t singletonClusterCount = 0;          // demoted to independents
  std::size_t spuriousClusterCount = 0;           // demoted "connector + leaf" clusters
  std::size_t mainRingSize = 0;                   // §3.10 main backbone node count
  double mainRingScore = 0.0;                     // §3.10 sum of root degs
  bool mainRingClosed = false;                    // §3.10 true = ring (cycle), false = open path
  std::vector<std::size_t> mainRingNodeIdxs;      // §3.10 ordered node indices on main backbone
  std::vector<std::size_t> mainRingRowOfNode;     // §3.10 row index per spine member (parallel to mainRingNodeIdxs); empty for closed ring
  // Leaf matrix groups: when a high-deg parent's leaves are placed as a
  // rectangular grid (see §12 matrix override), each group records its
  // shared anchor port (matrix face nearest the parent). Downstream edge
  // routing inserts the anchor as a waypoint on every leaf→parent route
  // so the bundle's edges share an exit/entry segment, dramatically
  // reducing crossings against unrelated edges.
  struct LeafMatrixGroup {
    std::size_t parentIdx = 0;
    std::vector<std::size_t> leafIdxs;
    // For bus bundles: indices of all cluster roots the bundle members
    // collectively connect to (multi-root signature). Includes
    // parentIdx. For classic leaf bundles (single parent) this is just
    // [parentIdx].
    std::vector<std::size_t> sharedRootIdxs;
    double anchorX = 0.0;
    double anchorY = 0.0;
  };
  std::vector<LeafMatrixGroup> leafMatrixGroups;
  std::vector<PrunedNodeRecord> prunedNodes;      // §2.5 graph pruning
  std::size_t coreNodeCount = 0;                  // nodes left in 2-core
  std::size_t maxPruningLevel = 0;
  std::size_t aloneRootCount = 0;                 // deg-0 nodes (in pruning)
  std::string superPolarTopology;                 // "ring", "path", "complex", "trivial"
  std::string strategyReason;
};

// Runs the full cluster-graph pipeline. `clusterLabels[i]` is the cluster id
// for `nodes[i]` (e.g. "_louv_3"). Mutates `attributes` to write final
// positions for every node.
//
// `bubbleMode` (graph-terminology.md §11): when true, the per-cluster inner
// placement uses a "bubble fill" — root at centre, all members packed in
// concentric rings around it (full 360° per ring). Each cluster looks like
// a circular bubble. Bridges are not directionalised toward external
// destinations; bus-alignment and outward overrides are skipped.
ClusterGraphResult runClusterGraphLayout(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::string>& clusterLabels,
  ogdf::GraphAttributes& attributes,
  bool bubbleMode = false);

}  // namespace djerd
