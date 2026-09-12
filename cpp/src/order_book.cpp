#include <order_book.hpp>
#include <spsc_ring_buffer.hpp>

namespace lob {

/* 2 Main scenarios to handle:
  1) Order doesn't even cross the order book
  2) Order crosses the order book (aggressive)
    a. Order consumes multiple levels of liquidity
    b. Order doesn't entirely consume the head liquidity
   */
void OrderBook::add_order(Order* order) {
  // Determine whether we've received a BID (buy) or ASK (sell) order
  if (order->side == Side::Bid) {  // User wants to buy
    // Do asks exist?
    if (!asks_.empty()) {
      auto& best_ask = asks_.front();
      // Is the bid greater or equal to the best_ask?
      if (order->price >= best_ask.price) {
        // Consume liquidity until the order->qty has been consumed or there are
        // no more asks
        while (order->qty > 0 && !asks_.empty()) {
        }
      }
    }

    // Add to the bids if the above conditions are not met
    order->prev = bids_[order->price].tail;
    bids_[order->price].tail = order;
  } else {
    // Reoeat the above for asks (sell orders)
  }
}

void OrderBook::cancel_order(uint64_t order_id) {}

}  // namespace lob