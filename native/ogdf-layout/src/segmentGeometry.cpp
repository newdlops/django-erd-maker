#include "layoutPipeline.h"

namespace djerd {

double crossProduct(double ax, double ay, double bx, double by) {
  return ax * by - ay * bx;
}

bool sharesEndpoint(const EdgeRecord& left, const EdgeRecord& right) {
  return left.sourceHandle == right.sourceHandle
    || left.sourceHandle == right.targetHandle
    || left.targetHandle == right.sourceHandle
    || left.targetHandle == right.targetHandle;
}

bool properSegmentIntersection(
  const RoutePoint& leftStart,
  const RoutePoint& leftEnd,
  const RoutePoint& rightStart,
  const RoutePoint& rightEnd,
  RoutePoint& intersection) {
  const double rx = leftEnd.x - leftStart.x;
  const double ry = leftEnd.y - leftStart.y;
  const double sx = rightEnd.x - rightStart.x;
  const double sy = rightEnd.y - rightStart.y;
  const double denominator = crossProduct(rx, ry, sx, sy);
  if (std::abs(denominator) < 1e-9) {
    return false;
  }

  const double qpx = rightStart.x - leftStart.x;
  const double qpy = rightStart.y - leftStart.y;
  const double t = crossProduct(qpx, qpy, sx, sy) / denominator;
  const double u = crossProduct(qpx, qpy, rx, ry) / denominator;
  constexpr double endpointEpsilon = 1e-9;
  if (
    t <= endpointEpsilon
    || t >= 1.0 - endpointEpsilon
    || u <= endpointEpsilon
    || u >= 1.0 - endpointEpsilon) {
    return false;
  }

  intersection = {
    leftStart.x + t * rx,
    leftStart.y + t * ry,
  };
  return true;
}


}  // namespace djerd
