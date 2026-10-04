// Research only: serialize a planarization, never a product layout.
// The input consists of complete biconnected edge blocks. No edge is removed
// from the exported result; crossings are explicit temporary degree-4 nodes.
#include <ogdf/basic/Graph.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/extended_graph_alg.h>
#include <ogdf/basic/simple_graph_alg.h>
#include <ogdf/planarity/PlanRep.h>
#include <ogdf/planarity/SubgraphPlanarizer.h>
#include <ogdf/planarity/PlanarSubgraphFast.h>
#include <ogdf/planarity/FixedEmbeddingInserter.h>
#include <ogdf/planarity/RemoveReinsertType.h>
#include <ogdf/planarity/PlanarizerStarReinsertion.h>
#include <ogdf/planarity/embedder/CrossingStructure.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Compact the graph between bounded batches. CrossingStructure alone does not
// retain the cyclic edge order, so explicitly save and restore every rotation.
class SnapshotInitial : public ogdf::CrossingMinimizationModule {
  struct Saved {
    ogdf::embedder::CrossingStructure crossings;
    std::map<int, std::vector<std::pair<int, bool>>> rotation;
  };
  std::shared_ptr<Saved> saved;
public:
  SnapshotInitial(ogdf::PlanRep& pr, int count) : saved(std::make_shared<Saved>()) {
    saved->crossings.init(pr, count);
    int dummy = 0;
    for (auto v : pr.nodes) {
      const auto original = pr.original(v);
      const int key = original ? original->index() : -1 - dummy++;
      for (auto adj : v->adjEntries)
        saved->rotation[key].push_back({pr.original(adj->theEdge())->index(), adj->isSource()});
    }
  }
  ogdf::CrossingMinimizationModule* clone() const override { return new SnapshotInitial(*this); }
protected:
  ReturnType doCall(ogdf::PlanRep& pr, int cc, const ogdf::EdgeArray<int>* costs,
      const ogdf::EdgeArray<bool>*, const ogdf::EdgeArray<uint32_t>*, int& count) override {
    pr.initCC(cc);
    saved->crossings.restore(pr, cc);
    ogdf::NodeArray<int> key(pr, 0);
    for (auto v : pr.nodes) if (pr.original(v)) key[v] = pr.original(v)->index();
    for (auto original : pr.original().edges) {
      auto chain = pr.chain(original).begin();
      for (int crossing : saved->crossings.crossings(original)) {
        ++chain;
        if (!chain.valid()) throw std::runtime_error("snapshot path is truncated");
        key[(*chain)->source()] = -1 - crossing;
      }
    }
    for (auto v : pr.nodes) {
      ogdf::List<ogdf::adjEntry> ordered;
      const auto& rotation = saved->rotation.at(key[v]);
      if (rotation.size() != static_cast<std::size_t>(v->degree()))
        throw std::runtime_error("snapshot changed vertex degree");
      for (const auto& dart : rotation) {
        ogdf::adjEntry found = nullptr;
        for (auto adj : v->adjEntries)
          if (pr.original(adj->theEdge())->index() == dart.first && adj->isSource() == dart.second)
            found = adj;
        if (!found) throw std::runtime_error("snapshot lost an incidence");
        ordered.pushBack(found);
      }
      pr.sort(v, ordered);
    }
    count = saved->crossings.weightedCrossingNumber();
    if (!pr.representsCombEmbedding() || pr.hasNonSimpleCrossings() ||
        computeCrossingNumber(pr, costs, nullptr) != count)
      throw std::runtime_error("snapshot restore failed");
    return ReturnType::Feasible;
  }
};

int main(int argc, char** argv) {
  try {
    if (argc != 6 && argc != 7) throw std::runtime_error("usage: probe blocks.tsv output.tsv iterations seconds seed [batch]");
    const int iterations = std::stoi(argv[3]);
    const double seconds = std::stod(argv[4]);
    const int seed = std::stoi(argv[5]);
    const int batchSize = argc == 7 ? std::stoi(argv[6]) : std::max(1, iterations);
    if (iterations < 0 || seconds <= 0 || batchSize <= 0) throw std::runtime_error("finite positive work bounds required");
    std::ifstream input(argv[1]);
    std::ofstream output(argv[2]);
    if (!input || !output) throw std::runtime_error("cannot open input/output");
    char type;
    int block, nodeCount, edgeCount;
    while (input >> type >> block >> nodeCount >> edgeCount) {
      if (type != 'B') throw std::runtime_error("expected block");
      ogdf::Graph graph;
      ogdf::NodeArray<int> nodeIds(graph, -1);
      ogdf::EdgeArray<int> edgeIds(graph, -1);
      ogdf::EdgeArray<int> edgeCosts(graph, 1);
      std::map<int, ogdf::node> byId;
      for (int i = 0; i < nodeCount; ++i) {
        int id;
        if (!(input >> type >> id) || type != 'N' || byId.count(id)) throw std::runtime_error("bad node");
        auto v = graph.newNode();
        byId[id] = v;
        nodeIds[v] = id;
      }
      for (int i = 0; i < edgeCount; ++i) {
        int id, a, b;
        if (!(input >> type >> id >> a >> b) || (type != 'E' && type != 'W')) throw std::runtime_error("bad edge");
        int cost = 1;
        if (type == 'W' && (!(input >> cost) || cost <= 0)) throw std::runtime_error("bad multiplicity");
        auto edge = graph.newEdge(byId.at(a), byId.at(b));
        edgeIds[edge] = id;
        edgeCosts[edge] = cost;
      }
      if (!ogdf::isSimpleUndirected(graph) || !ogdf::isBiconnected(graph))
        throw std::runtime_error("input must be a simple biconnected edge block");
      ogdf::setSeed(seed + block);
      std::srand(seed + block);
      auto* initial = new ogdf::SubgraphPlanarizer();
      auto* subgraph = new ogdf::PlanarSubgraphFast<int>();
      subgraph->runs(1);
      subgraph->maxThreads(1);
      initial->setSubgraph(subgraph);
      auto* inserter = new ogdf::FixedEmbeddingInserter();
      inserter->removeReinsert(ogdf::RemoveReinsertType::None);
      initial->setInserter(inserter);
      initial->permutations(1);
      initial->maxThreads(1);
      std::unique_ptr<ogdf::CrossingMinimizationModule> nextInitial(initial);
      std::unique_ptr<ogdf::PlanRep> result;
      int crossings = -1;
      const auto start = std::chrono::steady_clock::now();
      auto status = ogdf::Module::ReturnType::Feasible;
      int budgetUsed = 0;
      do {
        const int steps = std::min(batchSize, iterations - budgetUsed);
        const double remaining = seconds - std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (remaining <= 0 && result) break;
        result = std::make_unique<ogdf::PlanRep>(graph);
        result->initCC(0);
        ogdf::PlanarizerStarReinsertion planarizer;
        planarizer.setPlanarization(nextInitial.release());
        planarizer.maxIterations(steps);
        planarizer.timeLimit(std::max(0.001, remaining));
        planarizer.setTimeout(true);
        const int before = crossings;
        status = planarizer.call(*result, 0, crossings, &edgeCosts);
        if (!ogdf::Module::isSolution(status)) throw std::runtime_error("planarizer returned no solution");
        if (before >= 0 && crossings > before) throw std::runtime_error("reinsertion worsened crossings");
        budgetUsed += steps;
        if (iterations && edgeCount > 10)
          std::cerr << "checkpoint block=" << block << " budget=" << budgetUsed << " crossings=" << crossings
                    << " seconds=" << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << std::endl;
        if (crossings == 0 || crossings == before || budgetUsed >= iterations ||
            status == ogdf::Module::ReturnType::TimeoutFeasible) break;
        nextInitial = std::make_unique<SnapshotInitial>(*result, crossings);
      } while (true);
      auto& representation = *result;
      const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      if (!ogdf::Module::isSolution(status)) throw std::runtime_error("planarizer returned no solution");
      // A planar initial graph takes OGDF's early-return path before embedding.
      if (crossings == 0 && !representation.representsCombEmbedding() && !ogdf::planarEmbed(representation))
        throw std::runtime_error("zero-crossing result is not planar");
      if (representation.hasNonSimpleCrossings() || !representation.representsCombEmbedding())
        throw std::runtime_error("invalid or non-simple planarization");
      int dummyCount = 0;
      for (auto v : representation.nodes) {
        const auto original = representation.original(v);
        if (!original) {
          if (v->degree() != 4) throw std::runtime_error("crossing dummy must have degree four");
          ++dummyCount;
        }
        output << "N\t" << block << '\t' << v->index() << '\t' << (original ? nodeIds[original] : -1) << '\n';
      }
      int computedCost = 0;
      for (auto v : representation.nodes) if (!representation.original(v)) {
        auto adj = v->firstAdj();
        computedCost += edgeCosts[representation.original(adj->theEdge())] *
                        edgeCosts[representation.original(adj->cyclicSucc()->theEdge())];
      }
      if (computedCost != crossings) throw std::runtime_error("crossing cost does not match representation");
      for (auto e : representation.edges) {
        const auto original = representation.original(e);
        if (!original) throw std::runtime_error("segment has no original relation");
        output << "S\t" << block << '\t' << e->index() << '\t' << e->source()->index()
               << '\t' << e->target()->index() << '\t' << edgeIds[original] << '\n';
      }
      for (auto v : representation.nodes) {
        output << "R\t" << block << '\t' << v->index();
        for (auto adj : v->adjEntries) output << '\t' << adj->theEdge()->index();
        output << '\n';
      }
      output << "M\t" << block << '\t' << nodeCount << '\t' << edgeCount << '\t' << crossings
             << '\t' << elapsed << '\t' << int(status) << '\t' << dummyCount << '\n';
      output.flush();
      std::cerr << "block=" << block << " nodes=" << nodeCount << " edges=" << edgeCount
                << " crossings=" << crossings << " seconds=" << elapsed << " status=" << int(status) << std::endl;
    }
    if (!input.eof()) throw std::runtime_error("malformed trailing input");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
