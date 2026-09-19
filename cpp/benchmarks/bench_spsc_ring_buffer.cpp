#include <benchmark/benchmark.h>

#include <cstdint>

#include "lob/spsc_ring_buffer.hpp"

// Single-threaded push+pop throughput. This is a latency/throughput floor:
// it never blocks on a second thread, so it tells you the pure per-op cost
// (atomic RMW + cache traffic) rather than end-to-end pipeline throughput.
// Use the CTest-registered concurrent stress test for correctness under
// real producer/consumer contention; use this benchmark to track regressions
// in the hot-path op cost as you touch this file.
static void BM_PushPop(benchmark::State& state) {
  lob::SpscRingBuffer<std::uint64_t, 1024> rb;
  std::uint64_t i = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(rb.try_push(i));
    auto v = rb.try_pop();
    benchmark::DoNotOptimize(v);
    ++i;
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PushPop);

// BENCHMARK_MAIN();
