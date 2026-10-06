#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace djerd {

// Centres and real rendered dimensions, scoped to one fresh layout request.
struct StraightVisualNode { double width, height, x, y; };
struct StraightVisualEdge { std::size_t source, target; };
struct StraightVisualMove { std::size_t node; double x, y; };
struct StraightVisualRoute { double sourceX, sourceY, targetX, targetY; };
struct StraightVisualScore {
  std::int64_t edgeCrossings = 0, edgeNodeIntersections = 0;
  std::int64_t nodeOverlaps = 0, invalidRoutes = 0;
  std::int64_t visual() const { return edgeCrossings + edgeNodeIntersections + nodeOverlaps; }
};

StraightVisualScore measureStraightVisualFull(
  const std::vector<StraightVisualNode>& nodes,
  const std::vector<StraightVisualEdge>& edges);

// A move changes its incident lines and the card's obstruction of every
// unrelated line. Neither pair lists nor geometry from past requests is kept.
class StraightVisualState {
 public:
  StraightVisualState(std::vector<StraightVisualNode> nodes,
                      std::vector<StraightVisualEdge> edges);
  ~StraightVisualState();
  const std::vector<StraightVisualNode>& nodes() const;
  const std::vector<StraightVisualRoute>& routes() const;
  StraightVisualScore score() const;
  StraightVisualScore evaluateMove(std::size_t node, double x, double y) const;
  void move(std::size_t node, double x, double y);
  StraightVisualScore evaluateSwap(std::size_t first, std::size_t second) const;
  void swap(std::size_t first, std::size_t second);
  StraightVisualScore evaluateMoves(const std::vector<StraightVisualMove>& moves) const;
  void moveMany(const std::vector<StraightVisualMove>& moves);
  // Check the real card rectangles before changing routes or accepting a
  // candidate. Moving members are tested against their simultaneous poses.
  bool canMoveWithCardClearance(std::size_t node, double x, double y, double minimumX, double minimumY) const;
  bool canSwapWithCardClearance(std::size_t first, std::size_t second, double minimumX, double minimumY) const;
  bool canMoveManyWithCardClearance(const std::vector<StraightVisualMove>& moves, double minimumX, double minimumY) const;
  bool allCardsHaveClearance(double minimumX, double minimumY) const;
  std::vector<std::int64_t> pressure() const;

 private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

struct StraightVisualPlacementOptions {
  double budgetMs = 30000, groupBudgetMs = 1500;
  // Reserved within budgetMs, never added to the request's deadline.
  double escapeBudgetMs = 0;
  int maxRounds = 100, angularSamples = 8, nodeLimit = 0, swapLimit = 80;
  std::size_t maxEscapeIterations = 1000000;
  std::uint64_t seed = 42;
  // Zero preserves ordinary/offline placement. Source predictions start with
  // readable gaps and require every accepted move to retain those gaps.
  double cardGapX = 0, cardGapY = 0;
};
struct StraightVisualPlacementResult {
  std::vector<StraightVisualNode> nodes;
  std::vector<StraightVisualRoute> routes;
  StraightVisualScore before, after;
  std::size_t moves = 0, evaluations = 0;
  std::size_t escapeEvaluations = 0, uphillMoves = 0;
  int rounds = 0;
  double elapsedMs = 0;
  bool budgetHit = false;
};
StraightVisualPlacementResult optimizeStraightVisualPlacement(
  const std::vector<StraightVisualNode>& nodes,
  const std::vector<StraightVisualEdge>& edges,
  const std::vector<std::string>& ids,
  const std::vector<std::vector<std::size_t>>& groups,
  const StraightVisualPlacementOptions& options);

}  // namespace djerd
