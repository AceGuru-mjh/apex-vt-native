// Fuzz harness — crash/invariant guard over pseudo-random byte streams and
// structured mutation of valid escape sequences. Runs with a fixed seed in CI
// (deterministic); pass a seed argument to explore.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "../include/apex/vt/vt_engine.h"

using namespace apex::vt;

namespace {

bool gFailed = false;

void runFuzz(uint32_t seed, int iterations) {
  std::mt19937 rng(seed);
  Engine e(24, 80, 500);
  std::vector<uint8_t> buf;
  size_t lastFootprint = 0;
  for (int it = 0; it < iterations; ++it) {
    buf.clear();
    size_t n = 1 + (rng() % 512);
    for (size_t i = 0; i < n; ++i) {
      uint32_t r = rng() % 100;
      if (r < 55) {
        buf.push_back(uint8_t(0x20 + (rng() % 95)));  // printable ASCII
      } else if (r < 70) {
        buf.push_back(uint8_t(0x1B));  // ESC
      } else if (r < 85) {
        static const uint8_t seq[] = {'[', '3', '1', 'm', '[', '2', 'J', ']', '0', ';', 't'};
        buf.push_back(seq[rng() % sizeof(seq)]);
      } else if (r < 92) {
        buf.push_back(uint8_t(0x80 + (rng() % 0x80)));  // high bytes (UTF-8 / C1)
      } else {
        buf.push_back(uint8_t(0xE0 + (rng() % 0x10)));  // UTF-8 lead bytes
      }
    }
    e.feed(buf.data(), buf.size());
    if (it % 97 == 0) {
      e.drainMutations();
      e.renderSnapshot(100);
      e.renderedText();
      size_t fp = e.cellFootprint();
      // Visible 24×80 + scrollback ring ≤ 500×80 cells (plus slack).
      if (fp > (24 + 500) * 80 * 8 * 2) {
        std::printf("FOOTPRINT BLOWUP at seed=%u it=%d: %zu bytes\n", seed, it, fp);
        gFailed = true;
        return;
      }
      lastFootprint = fp;
    }
    if (e.pendingMutationCount() > 4097) {
      std::printf("MUTATION LIST UNBOUNDED at seed=%u it=%d: %zu\n", seed, it,
                  e.pendingMutationCount());
      gFailed = true;
      return;
    }
  }
  e.flush();
  (void)lastFootprint;
}

}  // namespace

int main(int argc, char** argv) {
  uint32_t seed = 0xC0FFEEu;
  int iterations = 20000;
  if (argc > 1) seed = uint32_t(std::strtoul(argv[1], nullptr, 0));
  if (argc > 2) iterations = std::atoi(argv[2]);
  std::printf("fuzzing seed=%u iterations=%d\n", seed, iterations);
  // A few seeds for breadth.
  runFuzz(seed, iterations);
  runFuzz(seed ^ 0x5A5A5A5Au, iterations / 4);
  runFuzz(seed + 1, iterations / 8);
  if (gFailed) return 1;
  std::printf("fuzz OK\n");
  return 0;
}
