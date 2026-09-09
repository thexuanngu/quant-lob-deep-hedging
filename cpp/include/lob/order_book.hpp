#pragma once
#include <cstdint>
#include <unordered_map>

namespace lob {

enum class Side { Bid, Ask };

// Intrusive linked-list node
struct Order {
  uint64_t order_id;
  int64_t price;  // Integer tick count
  uint64_t qty;
  Side side;

  Order* prev = nullptr;
  Order* next = nullptr;
};

// Represents one price level in the book
struct PriceLevel {
  int64_t price;
  uint64_t total_qty = 0;

  Order* head = nullptr;
  Order* tail = nullptr;
};

}  // namespace lob