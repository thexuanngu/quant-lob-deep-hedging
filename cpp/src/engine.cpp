
#include "lob/engine.hpp"

namespace lob {

    void run_engine(SpscRingBuffer<OrderEvent, 1024>& buffer, OrderBook& book, OrderPool& pool) {
    while (true) {
        auto event_opt = buffer.try_pop();
        if (!event_opt) continue; // Spin lock: buffer is empty, immediately check again

        OrderEvent event = *event_opt;
        
        // Translate the cross-thread event into a hot-path Order pointer
        Order* order = pool.allocate();
        order->order_id = event.order_id;
        order->price = event.price;
        order->qty = event.qty;
        order->side = event.side;

        book.add_order(order);
    }
}

void LOBEngine::start() {
    running_.store(true, std::memory_order_release);
    
    // Spawn the consumer thread (The HFT Engine)
    engine_thread_ = std::thread([this]() {
        while (running_.load(std::memory_order_acquire)) {
            auto event_opt = ring_buffer_.try_pop();
            if (!event_opt) continue; // Spin-lock: immediately retry -> Is this ok?
            
            OrderEvent& ev = *event_opt;
            
            // Route the event
            if (ev.action == Action::Add) { 

                Order* order = order_pool_.allocate(ev.order_id, ev.price, ev.qty, ev.side);
                if (order) {
                    book_.add_order(order);
                }
            } else {
                book_.cancel_order(ev.order_id);
            }
        }
    });
}

void LOBEngine::stop() {
    running_.store(false, std::memory_order_release);
    if (engine_thread_.joinable()) {
        engine_thread_.join();
    }
}
}