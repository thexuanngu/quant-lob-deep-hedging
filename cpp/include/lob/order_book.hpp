#pragma once
#include <assert.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>
#include <string>  // for testing purposes

#include "lob/order_pool.hpp"
#include "lob/queues.hpp"

namespace lob {

inline constexpr int64_t kNoBid = -1;
inline constexpr int64_t kNoAsk = INT64_MAX;

class OrderBook {
 public:
  // OrderBook() = default;
  OrderBook(int64_t num_price_levels, int64_t tick_price_offset,
            OrderPool* order_pool, OutputBuffer& outbound_fills);

  OrderBook(const OrderBook&) = delete;
  OrderBook& operator=(const OrderBook&) = delete;

  // The method you will implement in order_book.cpp
  void add_order(Order* order);
  void cancel_order(uint64_t order_id);
  void modify_order(uint64_t order_id, int64_t new_price, uint64_t new_qty);
  void set_time(std::uint64_t ts_ns) noexcept {
    assert(ts_ns >= now_ns_);
    now_ns_ = ts_ns;
  }

  // Testing functionality
  std::optional<int64_t> best_bid() const noexcept;
  std::optional<int64_t> best_ask() const noexcept;
  uint64_t dropped_fills() const noexcept { return dropped_fills_; }
  uint64_t out_of_window() const noexcept { return out_of_window_; }

  uint64_t get_total_quantity_at_price(Side side, int64_t price);
  std::vector<uint64_t> get_order_count_at_level(Side side, int64_t price);
  std::size_t get_all_resting_orders();
  PriceLevel get_price_level(Side side, int64_t price);
  std::string check_invariants() const;

  std::uint64_t state_hash() const;  // similar to check_invariants for the SyntheticFeed

 private:
  // Contiguous memory for price levels (fast cache access)
  // at [0]: tick_price_offset -> at .back(): .size() * tick_price_offset
  std::vector<PriceLevel> asks_;
  std::vector<PriceLevel> bids_;
  int64_t tick_price_offset_;  // idx relative to an offset
  // offset in 'integer' terms produces the correct index (ex. 10000)

  // Memory order
  OrderPool* order_pool_;

  // Explicitly track the best bids and ask in the LOB
  // OFFSET ADJUSTED
  int64_t best_bid_ = kNoBid;  // idx 0 could be valid (bc of offset)
  int64_t best_ask_ = kNoAsk;

  // Notifications for the decision layer
  OutputBuffer& outbound_fills_;

  // Track the try_pushes
  uint64_t dropped_fills_ = 0;
  uint64_t out_of_window_ = 0;
  uint64_t now_ns_ = 0;

  // Global O(1) lookup table for instant cancellations
  std::unordered_map<uint64_t, Order*>
      order_map_;  // TODO: Convert this into a FLAT array format
};

// NOTE: The ORDER price is the original price
//--the BID/ASK prices are the adjusted, indexable prices

}  // namespace lob
