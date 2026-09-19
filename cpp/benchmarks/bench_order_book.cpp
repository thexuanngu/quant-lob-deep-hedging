#include <benchmark/benchmark.h>

#include "lob/order_book.hpp"

static void BM_OrderBookMatching(benchmark::State& state) {
  // Setup: Initialize OrderBook and Object Pool here
  int64_t num_price_levels = 1000;
  double tick_size = 5.0;
  int64_t tick_price_offset = 1;
  lob::OrderPool pool(1024);

  lob::OrderBook book(num_price_levels, tick_size, tick_price_offset, &pool);

  // Preload the order book:
  for (int i = 0; i < 1024; ++i) {
    book.add_order(pool.allocate(i, 100, 10, lob::Side::Ask));
  }

  for (auto _ : state) {
    // Code inside this loop is strictly timed
    lob::Order* aggressive_order = pool.allocate(1025, 100, 45, lob::Side::Bid);
    book.add_order(aggressive_order);
  }
}
BENCHMARK(BM_OrderBookMatching);
BENCHMARK_MAIN();