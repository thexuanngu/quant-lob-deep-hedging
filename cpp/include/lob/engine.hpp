#pragma once

#include "lob/order_book.hpp"
#include "lob/spsc_ring_buffer.hpp"
#include <thread>

namespace lob {
enum class Action { Add, Cancel, Modify };

struct OrderEvent {  // Interface between the buffer and the OrderPool
  uint64_t order_id;
  int64_t price;
  uint64_t qty;
  Side side;
  Action action;  // Add, Cancel, Modify
};

class LOBEngine {
 public:
  LOBEngine(OrderBook& order_book, OrderPool& order_pool,
            SpscRingBuffer<OrderEvent, 1024>& ring_buffer)
      : book_(order_book), order_pool_(order_pool), ring_buffer_(ring_buffer),
        running_(false) {}

  void runEngine();
  void start();
  void stop();

 private:
  OrderBook& book_;
  OrderPool& order_pool_;
  SpscRingBuffer<OrderEvent, 1024>& ring_buffer_;
  std::atomic<bool> running_;
  std::thread engine_thread_;
};
}  // namespace lob
