#pragma once

#include <thread>

#include "lob/order_book.hpp"
#include "lob/spsc_ring_buffer.hpp"
#include "lob/queues.hpp"

namespace lob {

class LOBEngine {
 public:
  LOBEngine(OrderBook& order_book, OrderPool& order_pool,
            InputBuffer& ring_buffer);
  ~LOBEngine();  // calls stop(); a joinable std::thread destructing = terminate

  LOBEngine(const LOBEngine&) = delete;
  LOBEngine& operator=(const LOBEngine&) = delete;

  std::uint64_t processed() const noexcept {return processed_.load(std::memory_order_acquire); }

  void process(const OrderEvent& ev);  // shared by sync + threaded paths
  void runEngine();
  void start();
  void stop();  // contract: producer has already finished pushing

 private:
  OrderBook& book_;
  OrderPool& order_pool_;
  InputBuffer& ring_buffer_;
  std::atomic<bool> running_;
  alignas(kCacheLineSize) std::atomic<std::uint64_t> processed_{0};
  std::thread engine_thread_;
};
}  // namespace lob
