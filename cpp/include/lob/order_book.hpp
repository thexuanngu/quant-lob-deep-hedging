#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "lob/spsc_ring_buffer.hpp"
namespace lob {

enum class Side { Bid, Ask };

// Intrusive linked-list node
struct Order {
  uint64_t order_id;  // Cannot be a string (as it would touch the heap)
  int64_t price;      // Integer tick count (RAW)
  uint64_t qty;
  Side side;

  Order* prev = nullptr;
  Order* next = nullptr;

  // Is this usable with the 'popping' off of a pointer?
  // Order(uint64_t order_id, int64_t price, uint64_t qty, Side side) :
  // order_id(order_id), price(price), qty(qty), side(side) {}
};

// Represents one price level in the FIFO queue
struct PriceLevel {
  // int64_t price = 0; offset indexing logic might might make this redundant
  uint64_t total_qty = 0;
  Order* head = nullptr;
  Order* tail = nullptr;
};

class OrderPool {
 public:
  explicit OrderPool(std::size_t capacity);

  // O(1) allocation: Pop a pointer off the back
  Order* allocate();

  // O(1) allocation with ORDER quantities initialized
  Order* allocate(uint64_t order_id, int64_t price, uint64_t qty, Side side);

  // O(1) deallocation: Push the pointer back to the list
  void deallocate(Order* order);

 private:
  std::vector<Order> pool_;
  std::vector<Order*> free_list_;
};

struct OrderEvent {  // Interface between the buffer and the OrderPool
  uint64_t order_id;
  int64_t price;
  uint64_t qty;
  Side side;
  // e.g., Action action; // Add, Cancel, Modify
};

class OrderBook {
 public:
  // OrderBook() = default;
  OrderBook(int64_t num_price_levels, double tick_size,
            int64_t tick_price_offset, OrderPool* order_pool);

  // The method you will implement in order_book.cpp
  void add_order(Order* order);
  void cancel_order(uint64_t order_id);
  void modify_order(uint64_t order_id, int64_t new_price, uint64_t new_qty);
  // void cancel_price_level(int64_t price, Side side); // TODO

  int64_t get_best_bid();
  int64_t get_best_ask();
  uint64_t get_total_quantity_at_price(Side side, int64_t price);
  std::vector<uint64_t> get_order_count_at_level(Side side, int64_t price);
  std::size_t get_all_resting_orders();
  PriceLevel get_price_level(Side side, int64_t price);

 private:
  // Contiguous memory for price levels (fast cache access)
  // at [0]: tick_price_offset -> at .back(): .size() * tick_price_offset
  std::vector<PriceLevel> asks_;
  std::vector<PriceLevel> bids_;
  double tick_size_;           // TODO: Currently does nothing
  int64_t tick_price_offset_;  // idx relative to an offset
  // offset in 'integer' terms produces the correct index (ex. 10000)

  // TODO: build in a safety check for orders beyond the range

  // Memory order
  OrderPool* order_pool_;
  // TODO: Preallocate these ^vector memories

  // Explicitly track the best bids and ask in the LOB
  // OFFSET ADJUSTED
  int64_t best_bid_ = 0;  // True: (tick_price_offset + best_bid_) * tick_size
  int64_t best_ask_ = INT64_MAX;

  // Global O(1) lookup table for instant cancellations
  std::unordered_map<uint64_t, Order*>
      order_map_;  // TODO: Convert this into a FLAT array format
};

// NOTE: The ORDER price is the original price
//--the BID/ASK prices are the adjusted, indexable prices

}  // namespace lob
