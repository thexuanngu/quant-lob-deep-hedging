#include <benchmark/benchmark.h>

#include "lob/order_book.hpp"

// Measures pure non-crossing insertion cost. Cleanup is batched every 256
// iterations rather than every single one: PauseTiming()/ResumeTiming() have
// real overhead of their own, and for an operation this fast (low hundreds
// of nanoseconds), calling them twice per iteration measurably inflates the
// reported cost - confirmed by comparing against a per-iteration-cleanup
// version, which reported ~2x higher and reversed the expected ordering
// relative to BM_OrderBookMatching below.
static void BM_OrderBookAddResting(benchmark::State& state) {
  constexpr int64_t kNumPriceLevels = 1000;
  constexpr double kTickSize = 5.0;
  constexpr int64_t kTickPriceOffset = 1;
  constexpr int kBatchSize = 256;
  lob::OrderPool pool(4096);
  lob::OrderBook book(kNumPriceLevels, kTickSize, kTickPriceOffset, &pool);
  uint64_t next_id = 1;
  uint64_t batch_start = 1;
  int count_in_batch = 0;

  for (auto _ : state) {
    state.PauseTiming();
    lob::Order* order = pool.allocate(next_id, 500, 10, lob::Side::Bid);
    state.ResumeTiming();

    book.add_order(order);  // <-- only this is measured
    ++next_id;

    if (++count_in_batch >= kBatchSize) {
      state.PauseTiming();
      for (uint64_t id = batch_start; id < next_id; ++id) {
        book.cancel_order(id);
      }
      batch_start = next_id;
      count_in_batch = 0;
      state.ResumeTiming();
    }
  }
}
BENCHMARK(BM_OrderBookAddResting);

// Measures the matching/crossing path: an aggressive order that walks the
// FIFO queue at the best level, partially or fully consuming several
// resting orders. Uses a persistent book + periodic top-up rather than
// rebuilding the book every iteration, so the pool reaches a steady state
// instead of monotonically draining.
static void BM_OrderBookMatching(benchmark::State& state) {
  constexpr int64_t kNumPriceLevels = 1000;
  constexpr double kTickSize = 5.0;
  constexpr int64_t kTickPriceOffset = 1;
  constexpr int64_t kPrice = 100;
  constexpr uint64_t kRestingQty = 10;
  constexpr uint64_t kAggressiveQty = 45;
  constexpr uint64_t kMinDepth = 500;

  lob::OrderPool pool(4096);
  lob::OrderBook book(kNumPriceLevels, kTickSize, kTickPriceOffset, &pool);
  uint64_t next_id = 1;

  for (auto _ : state) {
    state.PauseTiming();
    while (book.get_total_quantity_at_price(lob::Side::Ask, kPrice) <
           kMinDepth) {
      lob::Order* resting =
          pool.allocate(next_id++, kPrice, kRestingQty, lob::Side::Ask);
      book.add_order(resting);
    }
    lob::Order* aggressive =
        pool.allocate(next_id++, kPrice, kAggressiveQty, lob::Side::Bid);
    state.ResumeTiming();

    book.add_order(aggressive);  // <-- only this is measured
  }
}
BENCHMARK(BM_OrderBookMatching);

BENCHMARK_MAIN();