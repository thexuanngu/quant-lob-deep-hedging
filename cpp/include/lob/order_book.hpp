#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lob {

enum class Side { Bid, Ask };

// Intrusive linked-list node
struct Order {
  uint64_t order_id;  // Cannot be a string (as it would touch the heap, bad
                      // during a hot path)
  int64_t price;      // Integer tick count
  uint64_t qty;
  Side side;

  Order* prev = nullptr;
  Order* next = nullptr;
};

// Represents one price level in the FIFO queue
struct PriceLevel {
  int64_t price = 0;
  uint64_t total_qty = 0;

  Order* head = nullptr;
  Order* tail = nullptr;
};

class OrderBook {
 public:
  OrderBook() = default;

  // The method you will implement in order_book.cpp
  void add_order(Order* order);
  void cancel_order(uint64_t order_id);

 private:
  // Contiguous memory for price levels (fast cache access)
  std::vector<PriceLevel> bids_;
  std::vector<PriceLevel> asks_;

  // Global O(1) lookup table for instant cancellations
  std::unordered_map<uint64_t, Order*> order_map_;
};

}  // namespace lob