#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <ogdf/basic/EdgeArray.h>
#include <ogdf/basic/Graph.h>

namespace djerd {

// Versioned independently from the layout protocol: the certificate ordering is
// part of the reproducibility contract and must change when the algorithm does.
inline constexpr const char* kCrossingLowerBoundMethod =
  "degree2-k3n+deterministic-boyer-myrvold-packing";
inline constexpr const char* kCrossingLowerBoundVersion = "1";

struct CrossingLowerBoundOptions {
  // A fixed work budget, rather than a wall-clock deadline, keeps successful
  // reports byte-for-byte deterministic across runs.
  std::size_t maxTripleOccurrences = 2'000'000;
  std::size_t maxK3nRounds = 16;
  std::size_t minimumK3nRightSize = 4;
  std::size_t maxKuratowskiRounds = 64;
  std::size_t deterministicOrderingsPerRound = 16;
  std::size_t kuratowskiCandidatesPerOrdering = 8;
  std::size_t maxPlanarityCalls = 1024;
};

struct CertifiedSupportEdge {
  std::size_t canonicalIndex = 0;
  std::string edgeId;
  std::string sourceId;
  std::string targetId;
};

struct CrossingLowerBoundCertificate {
  enum class Kind {
    K3nSubdivision,
    KuratowskiK33Subdivision,
    KuratowskiK5Subdivision,
  };

  Kind kind = Kind::KuratowskiK33Subdivision;
  std::size_t contribution = 0;
  // K3,n certificates expose the three-vertex and n-vertex branch sets.
  // Kuratowski certificates expose all branch vertices in branchVertices.
  std::vector<std::string> leftVertices;
  std::vector<std::string> rightVertices;
  std::vector<std::string> branchVertices;
  std::vector<CertifiedSupportEdge> supportEdges;
  // Canonical, stable summary suitable for logs and independent comparison.
  std::string signature;
};

struct CrossingLowerBoundReport {
  std::string method = kCrossingLowerBoundMethod;
  std::string version = kCrossingLowerBoundVersion;
  std::size_t nodeCount = 0;
  std::size_t edgeCount = 0;
  std::size_t totalLowerBound = 0;
  std::size_t k3nContribution = 0;
  std::size_t kuratowskiContribution = 0;
  std::size_t k3nCertificateCount = 0;
  std::size_t kuratowskiCertificateCount = 0;
  std::size_t tripleOccurrences = 0;
  std::size_t planarityCalls = 0;
  bool planar = false;
  bool residualPlanar = false;
  bool workLimitReached = false;
  bool inputWasSimple = true;
  bool invariantsVerified = false;
  std::vector<CrossingLowerBoundCertificate> certificates;
};

// Computes a lower bound for both the ordinary topological crossing number and
// the pair-crossing number. The input graph is never modified. nodeIds and
// edgeIds are optional; when absent, stable keys based on OGDF indices are used.
// Parallel edges are canonicalized to one representative and self-loops are
// ignored, with inputWasSimple recording whether canonicalization was needed.
CrossingLowerBoundReport computeCertifiedCrossingLowerBound(
  const ogdf::Graph& graph,
  const ogdf::NodeArray<std::string>* nodeIds = nullptr,
  const ogdf::EdgeArray<std::string>* edgeIds = nullptr,
  const CrossingLowerBoundOptions& options = {});

// Independently re-checks all structural certificate invariants against the
// report's canonical support edge references. This does not run a planarity
// algorithm and is intended to catch generator or serialization defects.
bool verifyCrossingLowerBoundReport(
  const CrossingLowerBoundReport& report,
  std::string* failureReason = nullptr);

const char* crossingLowerBoundCertificateKindName(
  CrossingLowerBoundCertificate::Kind kind);

}  // namespace djerd
