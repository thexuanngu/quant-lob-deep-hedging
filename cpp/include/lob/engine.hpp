#pragma once

#include <thread>

#include "lob/order_book.hpp"
#include "lob/spsc_ring_buffer.hpp"

namespace lob {

class LOBEngine {
 public:
  LOBEngine(OrderBook& order_book, OrderPool& order_pool,
            SpscRingBuffer<OrderEvent, 1024>& ring_buffer)
      : book_(order_book),
        order_pool_(order_pool),
        ring_buffer_(ring_buffer),
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
