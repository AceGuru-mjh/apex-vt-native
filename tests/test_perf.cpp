// Throughput benchmark — guards the core performance contract:
// the feed() hot path must be allocation-free and cache-friendly.
// Release builds (NDEBUG) assert a floor throughput; debug prints only.
#include "mini_test.h"

#include "../include/apex/vt/vt_engine.h"

#include <chrono>
#include <cstdio>
#include <string>

using namespace apex::vt;
using Clock = std::chrono::steady_clock;

namespace {

// Synthetic "realistic terminal traffic": text + cursor moves + SGR colors
// + full redraws + scrolling (roughly like `vim` + `ls --color` output).
std::string makeTraffic(size_t targetBytes) {
  std::string out;
  out.reserve(targetBytes + 256);
  const char* words[] = {"lorem ", "ipsum ", "dolor ", "sit ", "amet ",
                         "\x1B[31m", "\x1B[0m", "\x1B[1;32m", "café ", "中文测试 "};
  size_t i = 0;
  while (out.size() < targetBytes) {
    out += words[i % 10];
    if (i % 64 == 63) out += "\x1B[2J\x1B[H";
    if (i % 512 == 511) out += "line\r\n";
    ++i;
  }
  return out;
}

}  // namespace

MINI_TEST(feed_throughput_floor) {
  const size_t kBytes = 8u << 20;  // 8 MiB
  const std::string traffic = makeTraffic(kBytes);
  Engine e(24, 80, 1000);

  // warmup (page in the string, warm branches)
  e.feed(reinterpret_cast<const uint8_t*>(traffic.data()), 1 << 16);
  e.reset();

  auto t0 = Clock::now();
  const size_t kChunk = 4096;  // realistic PTY read size
  for (size_t off = 0; off < traffic.size(); off += kChunk) {
    size_t n = std::min(kChunk, traffic.size() - off);
    e.feed(reinterpret_cast<const uint8_t*>(traffic.data()) + off, n);
    if ((off / kChunk) % 512 == 511) e.drainMutations();
  }
  e.drainMutations();
  auto t1 = Clock::now();

  double secs = std::chrono::duration<double>(t1 - t0).count();
  double mbps = double(kBytes) / (1024.0 * 1024.0) / secs;
  std::printf("  feed throughput: %.1f MiB/s (%.3fs for %zu MiB)\n", mbps, secs, kBytes >> 20);

#ifndef NDEBUG
  std::printf("  (debug build — floor check skipped)\n");
#else
  // CI floor: local runs measure ~100-160 MiB/s (mixed traffic with full
  // redraws + CJK); the floor catches regressions like per-byte allocation.
  CHECK(mbps > 60.0);
#endif
}

MINI_TEST(render_snapshot_cost) {
  // 33ms-sample cadence: snapshot must be far cheaper than the interval.
  Engine e(80, 200, 2000);
  std::string traffic = makeTraffic(2u << 20);
  e.feed(reinterpret_cast<const uint8_t*>(traffic.data()), traffic.size());
  e.drainMutations();

  auto t0 = Clock::now();
  for (int i = 0; i < 100; ++i) {
    FlatSnapshot s = e.renderSnapshot(400);
  }
  auto t1 = Clock::now();
  double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / 100.0;
  std::printf("  renderSnapshot(400 scrollback): %.0f us/call\n", us);
  CHECK(us < 5000.0);  // < 5ms even on slow CI runners
}
