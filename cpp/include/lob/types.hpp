#pragma once
#include <cstdint>
#include <type_traits>


namespace lob {
enum class Side : std::uint8_t { Bid, Ask };
enum class Action : std::uint8_t { Add, Cancel, Modify };

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

struct OrderEvent {
  std::uint64_t ts_ns;  // SIMULATED time, not wall-clock (see §3)
  std::uint64_t order_id;
  std::int64_t price;  // integer ticks
  std::uint64_t qty;
  Side side;
  Action action;
};

struct ExecutionEvent {  // aggregate: no ctor, so the ring buffer's `T
                         // buf_[N]{}` compiles
  std::uint64_t ts_ns;
  std::uint64_t maker_id;  // you need to know which of YOUR orders filled
  std::uint64_t taker_id;
  std::int64_t price;  // maker's price
  std::uint64_t qty;
  Side aggressor;  // taker's side
};

static_assert(std::is_trivially_copyable_v<OrderEvent>);
static_assert(std::is_trivially_copyable_v<ExecutionEvent>);
}  // namespace lob