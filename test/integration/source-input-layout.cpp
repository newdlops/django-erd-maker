#include "sourceInputLayout.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace djerd;
static void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
  try {
    require(argc == 3, "usage: shared-model.bin truncated-model.bin");
    const auto deadline = [] { return std::chrono::steady_clock::now() + std::chrono::seconds(3); };
    const std::vector<std::string> ids{"test.Alpha", "test.Beta"};
    const std::vector<StraightVisualEdge> edges{{0, 1}};
    std::vector<StraightVisualNode> original{{160, 80, 123456, -987654}, {240, 100, -456789, 98765}};
    const auto first = source_input::propose(original, edges, ids, argv[1], deadline());
    require(first.complete && first.nodes.size() == 2, "complete original source prediction missing");
    for (std::size_t n = 0; n < original.size(); ++n)
      require(first.nodes[n].width == original[n].width && first.nodes[n].height == original[n].height,
        "source predictor changes original dimensions");
    auto moved = original;
    for (auto& node : moved) { node.x *= -13; node.y += 345678; }
    const auto second = source_input::propose(moved, edges, ids, argv[1], deadline());
    require(second.complete, "changed prior coordinates reject genuine source input");
    for (std::size_t n = 0; n < original.size(); ++n)
      require(first.nodes[n].x == second.nodes[n].x && first.nodes[n].y == second.nodes[n].y,
        "source predictor reads prior coordinates");
    auto changed = original;
    changed[0].height *= 2;
    const auto dynamic = source_input::propose(changed, edges, ids, argv[1], deadline());
    require(dynamic.complete, "changed source dimensions do not produce complete prediction");
    require(std::hypot(first.nodes[0].x - dynamic.nodes[0].x,
      first.nodes[0].y - dynamic.nodes[0].y) > .001, "actual source dimensions do not affect prediction");
    auto unsupported = ids; unsupported[0] = "test.\xc3\xa9";
    const auto encoding = source_input::propose(original, edges, unsupported, argv[1], deadline());
    require(!encoding.complete && encoding.nodes.empty(), "unsupported source names return partial geometry");
    const auto malformed = source_input::propose(original, edges, ids, argv[2], deadline());
    require(!malformed.complete && malformed.nodes.empty(), "malformed model returns partial geometry");
    const auto expired = source_input::propose(original, edges, ids, argv[1], std::chrono::steady_clock::now());
    require(!expired.complete && expired.nodes.empty(), "expired prediction returns partial geometry");
    changed[0].width = std::numeric_limits<double>::quiet_NaN();
    const auto invalid = source_input::propose(changed, edges, ids, argv[1], deadline());
    require(!invalid.complete && invalid.nodes.empty(), "invalid original dimensions accepted");
    std::cout << "source-input invariance, dynamic dimensions, malformed, unsupported and expired contracts pass\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
