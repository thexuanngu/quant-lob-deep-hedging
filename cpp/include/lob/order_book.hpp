#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "lob/spsc_ring_buffer.hpp"
#include "lob/order_pool.hpp"

namespace lob {

class OrderBook {
 public:
  // OrderBook() = default;
  OrderBook(int64_t num_price_levels,
            int64_t tick_price_offset, OrderPool* order_pool,
            SpscRingBuffer<ExecutionEvent, 4096>& outbound_fills);

  // The method you will implement in order_book.cpp
  void add_order(Order* order);
  void cancel_order(uint64_t order_id);
  void modify_order(uint64_t order_id, int64_t new_price, uint64_t new_qty);
  // void cancel_price_level(int64_t price, Side side); // TODO

  int64_t get_best_bid();
  int64_t get_best_ask();
  bool has_bid(int64_t indexed_price);
  bool has_ask(int64_t indexed_price);
  uint64_t get_total_quantity_at_price(Side side, int64_t price);
  std::vector<uint64_t> get_order_count_at_level(Side side, int64_t price);
  std::size_t get_all_resting_orders();
  PriceLevel get_price_level(Side side, int64_t price);

 private:
  // Contiguous memory for price levels (fast cache access)
  // at [0]: tick_price_offset -> at .back(): .size() * tick_price_offset
  std::vector<PriceLevel> asks_;
  std::vector<PriceLevel> bids_;
  int64_t tick_price_offset_;  // idx relative to an offset
  // offset in 'integer' terms produces the correct index (ex. 10000)

  // TODO: build in a safety check for orders beyond the range

  // Memory order
  OrderPool* order_pool_;
  // TODO: Preallocate these ^vector memories

  // Explicitly track the best bids and ask in the LOB
  // OFFSET ADJUSTED
  int64_t best_bid_ = -1;  // 0 index could be a valid price (because of tick_price offset)
  int64_t best_ask_ = INT64_MAX;

  // Notifications for the decision layer
  SpscRingBuffer<ExecutionEvent, 4096>& outbound_fills_;

  // Track the try_pushes
  uint64_t dropped_fills_  = 0;
  uint64_t pool_exhausted_ = 0;
  uint64_t out_of_window   = 0;

  // Global O(1) lookup table for instant cancellations
  std::unordered_map<uint64_t, Order*>
      order_map_;  // TODO: Convert this into a FLAT array format
};

// NOTE: The ORDER price is the original price
//--the BID/ASK prices are the adjusted, indexable prices

}  // namespace lob
