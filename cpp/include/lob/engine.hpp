#pragma once

#include <thread>

#include "lob/order_book.hpp"
#include "lob/spsc_ring_buffer.hpp"

namespace lob {

class LOBEngine {
 public:
  LOBEngine(OrderBook& order_book, OrderPool& order_pool,
            SpscRingBuffer<OrderEvent, 4096>& ring_buffer);
  ~LOBEngine();  // calls stop(); a joinable std::thread destructing = terminate

  LOBEngine(const LOBEngine&) = delete;
  LOBEngine& operator=(const LOBEngine&) = delete;

  void process(const OrderEvent& ev);  // shared by sync + threaded paths
  void runEngine();
  void start();
  void stop();  // contract: producer has already finished pushing

 private:
  OrderBook& book_;
  OrderPool& order_pool_;
  SpscRingBuffer<OrderEvent, 4096>& ring_buffer_;
  std::atomic<bool> running_;
  alignas(kCacheLineSize) std::atomic<std::uint64_t> processed_{0};
  std::thread engine_thread_;
};
}  // namespace lob
