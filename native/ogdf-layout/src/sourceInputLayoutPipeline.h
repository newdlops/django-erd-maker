#pragma once

#include "io.h"

namespace djerd {

struct SourceInputLayoutAttempt {
  bool applied = false;
  double elapsedMs = 0;
};

// A failed attempt emits no partial JSON and restores this request's input
// positions. Its elapsed time is deducted from the legacy search reservation.
SourceInputLayoutAttempt tryWriteSourceInputLayout(
  const CliArguments& arguments,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  const LayoutRunMetadata& metadata,
  const CanonicalCrossingMetadata& canonical,
  const std::string& modelPath,
  double budgetMs,
  std::ostream& output);

}  // namespace djerd
