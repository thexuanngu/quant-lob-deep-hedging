
#include "lob/engine.hpp"

namespace lob {

void LOBEngine::runEngine() {
  while (running_.load(std::memory_order_acquire)) {
    auto event_opt = ring_buffer_.try_pop();
    if (!event_opt) std::this_thread::yield();  // Spin-lock: immediately retry -> Is this ok?

    OrderEvent& ev = *event_opt;

    // Route the event
    switch (ev.action) {
      case Action::Add:
        Order* order =
            order_pool_.allocate(ev.order_id, ev.price, ev.qty, ev.side);
        if (order) {  // null check
          book_.add_order(order);
        }
        break;
      case Action::Cancel:
        book_.cancel_order(ev.order_id);
        break;

      case Action::Modify:
        book_.modify_order(ev.order_id, ev.price, ev.qty);
        break;
    }
  }
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