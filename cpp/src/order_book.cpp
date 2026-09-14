#include <order_book.hpp>
#include <spsc_ring_buffer.hpp>

namespace lob {

void OrderBook::cancel_order(uint64_t order_id) {
  // Check the order exists (might be redundant if I'm really careful with how
  // order_map_ is maintained)
  if (order_map_.find(order_id) == order_map_.end()) return;

  auto& order = order_map_[order_id];
  auto& prev_order = order->prev;
  auto& next_order = order->next;

  // Connect the prior prev and next orders
  prev_order->next = next_order;
  next_order->prev = prev_order;

  // Delete / remove the previous order
  order_map_.erase(order_id);
  delete order;
}

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
      // Is the bid greater or equal to the best_ask?
      if (order->price >= best_ask_) {
        // Consume liquidity until:
        while (
            order->qty > 0     // (1) the order->qty has been consumed
            && !asks_.empty()  // (2) there are no more asks
            &&
            best_ask_ <=
                order->price) {  // (3) the best ask price is lower than the bid
          // Check if the order quantity would clear the price level
          auto& best_ask = asks_[best_ask_];
          if (best_ask.total_qty <= order->qty) {
            order->qty -= best_ask.total_qty;
            delete &best_ask;  // Delete everything and reset to 0?
            best_ask_ += 1;
          } else {  // Iterate through the orders
            best_ask.total_qty -= order->qty;
            auto& topOrder = best_ask.head;
            while (order->qty > 0) {
              order->qty -= topOrder->qty;
              auto& nextOrder = topOrder->next;
              cancel_order(topOrder->order_id);
              topOrder = nextOrder;
            }
            best_ask.head = topOrder->prev;
          }
        }
      }
    }
    // Add to the bids if the above conditions are not met
    PriceLevel& level = bids_[order->price];

    // 1. Wire the intrusive pointers
    if (level.tail == nullptr) {
      // This is the first order at this price level
      level.head = order;
      level.tail = order;
    } else {
      // Append to the existing FIFO queue
      order->prev = level.tail;
      level.tail->next = order;
      level.tail = order;
    }

    // 2. Update price level statistics
    level.total_qty += order->qty;

    // 3. Update the global index for O(1) cancellations
    order_map_[order->order_id] = order;

    // 4. Update the best bid/ask tracker if necessary
    if (order->price > best_bid_) {
      best_bid_ = order->price;
    }

  } else {  // Repeat the above for asks (sell orders)
    // Do bids exist?
    if (!bids_.empty()) {
      // Is the ask less than or equal to the best_bid?
      if (order->price <= best_bid_) {
        // Consume liquidity until:
        while (order->qty > 0     // (1) the order->qty has been consumed
               && !bids_.empty()  // (2) there are no more asks
               && best_bid_ >= order->price) {  // (3) the best bid price is
                                                // higher than the ask
          // Check if the order quantity would clear the price level
          auto& best_bid = bids_[best_bid_];
          if (best_bid.total_qty <= order->qty) {
            order->qty -= best_bid.total_qty;
            delete &best_bid;  // Delete everything and reset to 0?
            best_bid_ -= 1;
          } else {  // Iterate through the orders
            best_bid.total_qty -= order->qty;
            auto& topOrder = best_bid.head;
            while (order->qty > 0) {
              order->qty -= topOrder->qty;
              auto& nextOrder = topOrder->next;
              cancel_order(topOrder->order_id);
              topOrder = nextOrder;
            }
            best_bid.head = topOrder->prev;
          }
        }
      }
    }
    // Add to the asks if the above conditions are not met
    PriceLevel& level = asks_[order->price];

    // 1. Wire the intrusive pointers
    if (level.tail == nullptr) {
      // This is the first order at this price level
      level.head = order;
      level.tail = order;
    } else {
      // Append to the existing FIFO queue
      order->prev = level.tail;
      level.tail->next = order;
      level.tail = order;
    }

    // 2. Update price level statistics
    level.total_qty += order->qty;

    // 3. Update the global index for O(1) cancellations
    order_map_[order->order_id] = order;

    // 4. Update the best bid/ask tracker if necessary
    if (order->price < best_ask_) {
      best_bid_ = order->price;
    }
  }
}

}  // namespace lob