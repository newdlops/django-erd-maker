#include "faceRasterGrid.h"
#include <cstdint>
#include <iostream>
#include <queue>
#include <random>
#include <vector>

int main() {
  std::mt19937 random(673);
  for (int trial = 0; trial < 100; ++trial) {
    constexpr int width = 73, height = 57, edgeCount = 1732;
    const std::size_t count = width * height;
    djerd::FaceRasterGrid compact(count, edgeCount);
    std::vector<std::int32_t> obstacles(count), labels(count);
    for (std::size_t index = 0; index < count; ++index) {
      const auto value = random() % 7;
      const std::int32_t obstacle = value < 2 ? (value == 0
        ? static_cast<int>(random() % 1247) + 1
        : -static_cast<int>(random() % edgeCount) - 1) : 0;
      obstacles[index] = obstacle;
      compact.setObstacle(index, obstacle);
    }
    int faceCount = 0;
    for (std::size_t seed = 0; seed < count; ++seed) {
      if (obstacles[seed] != 0 || labels[seed] != 0) continue;
      ++faceCount;
      std::queue<std::size_t> pending;
      labels[seed] = faceCount;
      compact.setFace(seed, faceCount);
      pending.push(seed);
      while (!pending.empty()) {
        const auto index = pending.front(); pending.pop();
        const int x = index % width, y = index / width;
        const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
        for (int direction = 0; direction < 4; ++direction) {
          const int nx = x + dx[direction], ny = y + dy[direction];
          if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
          const std::size_t next = ny * width + nx;
          const bool oldUnassigned = obstacles[next] == 0 && labels[next] == 0;
          if (oldUnassigned != compact.unassignedBackground(next)) return 1;
          if (!oldUnassigned) continue;
          labels[next] = faceCount;
          compact.setFace(next, faceCount);
          pending.push(next);
        }
      }
    }
    for (std::size_t index = 0; index < count; ++index) {
      if (obstacles[index] != compact.obstacle(index) || labels[index] != compact.face(index)) {
        std::cerr << "Raster obstacle or face label differs at " << index << '\n';
        return 1;
      }
    }
    if (compact.storageBytes() != count * sizeof(std::int16_t)) {
      std::cerr << "Small labels must use a single 16-bit grid\n";
      return 1;
    }
    const auto previousFace = compact.face(0);
    const auto previousObstacle = compact.obstacle(0);
    compact.setObstacle(count - 1, 40000);
    if (compact.storageBytes() != count * sizeof(std::int32_t)
        || compact.face(0) != previousFace || compact.obstacle(0) != previousObstacle
        || compact.obstacle(count - 1) != 40000) return 1;
    compact.setFace(count - 1, 100000);
    if (compact.face(count - 1) != 100000 || compact.obstacle(count - 1) != 0) return 1;
  }
  std::cout << "100 flood fills preserve every obstacle and face label using one grid\n";
}
