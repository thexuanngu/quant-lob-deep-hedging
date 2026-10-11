#include "lob/engine.hpp"
// #include <immintrin.h>

namespace lob {

LOBEngine::LOBEngine(OrderBook& order_book, OrderPool& order_pool,
                     InputBuffer& ring_buffer)
    : book_(order_book),
      order_pool_(order_pool),
      ring_buffer_(ring_buffer),
      running_(false) {}

LOBEngine::~LOBEngine() { stop(); }

void LOBEngine::process(const OrderEvent& ev) {
  book_.set_time(ev.ts_ns);
  // Route the event
  switch (ev.action) {
    case Action::Add: {
      Order* order =
          order_pool_.allocate(ev.order_id, ev.price, ev.qty, ev.side);
      if (order)   // null check
        book_.add_order(order); 
    } break;
    case Action::Cancel:
      book_.cancel_order(ev.order_id);
      break;
    case Action::Modify:
      book_.modify_order(ev.order_id, ev.price, ev.qty);
      break;
  }
}

void LOBEngine::runEngine() {
  auto handle = [this](const OrderEvent& ev) {
    process(ev);
    // Single writer (this thread), so a plain load+store beats fetch_add:
    // no lock-prefixed RMW on the hot path. Release pairs with processed().
    processed_.store(processed_.load(std::memory_order_relaxed) + 1,
                     std::memory_order_release);
  };

  while (running_.load(std::memory_order_acquire)) {
    if (auto ev = ring_buffer_.try_pop())
      handle(*ev);
    else
      std::this_thread::yield();  // keep for now; hybrid backoff later
  }

  // Drain: anything pushed before stop() still gets processed.
  while (auto ev = ring_buffer_.try_pop()) 
    handle(*ev);
}

void LOBEngine::start() {
  running_.store(true, std::memory_order_release);

  // Spawn the consumer thread (The HFT Engine)
  engine_thread_ = std::thread(&LOBEngine::runEngine, this);
}

void LOBEngine::stop() {
  running_.store(false, std::memory_order_release);
  if (engine_thread_.joinable()) {
    engine_thread_.join();
  }
}
}  // namespace lob