#include <algorithm>
#include <order_book.hpp>
#include <spsc_ring_buffer.hpp>

namespace lob {

void OrderBook::cancel_order(uint64_t order_id) {
  // Check the order exists (might be redundant if I'm really careful with how
  // order_map_ is maintained)
  auto it = order_map_.find(order_id);
  if (it == order_map_.end()) return;

  Order* order = it->second;

  // 1. Locate the current price level
  PriceLevel& level =
      (order->side == Side::Ask) ? asks_[order->price] : bids_[order->price];

  // 2. Safely unlink the previous pointer
  if (order->prev != nullptr) {
    order->prev->next = order->next;
  } else {
    level.head = order->next;  // Delete the head
  }

  // 3. Safely unlink the next pointer
  if (order->next != nullptr) {
    order->next->prev = order->prev;
  } else {
    level.tail = order->prev;  // Delete the tail (if we were at tail)
  }

  // 4. Update the state variables
  level.total_qty -= order->qty;
  order_map_.erase(it);

  // Note: Return `order` to a pre-allocated object pool here. DO NOT use
  // `delete`.
}

/* 2 Main scenarios to handle:
  1) Order doesn't even cross the order book
  2) Order crosses the order book (aggressive)
    a. Order consumes multiple levels of liquidity
    b. Order doesn't entirely consume the head liquidity
   */
void OrderBook::add_order(Order* order) {
  // Determine whether we've received a BID (buy) or ASK (sell) order
  if (order->side == Side::Bid) {          // User wants to buy (BID)
    if (order->price >= best_ask_) {       // Order WILL consume liquidity
      while (order->qty > 0                // The incoming order's quantity > 0
             && best_ask_ <= order->price  // The incoming order's price is
                                           // greater than the best ask (sell)
             && best_ask_ < asks_.size()) {
        auto& level = asks_[best_ask_];

        if (level.total_qty ==
            0) {        // This will happen POST price level consumption
          best_ask_++;  // Fast forward past empty levels
          continue;
        }

        auto topOrder = level.head;
        while (topOrder && order->qty > 0) {
          // Safely caclulate the execution quantity to prevent underflow
          uint64_t execQuantity = std::min(order->qty, topOrder->qty);

          order->qty -= execQuantity;
          topOrder->qty -= execQuantity;
          level.total_qty -= execQuantity;

          if (topOrder->qty == 0) {  // i.e., it was the minimum
            Order* nextOrder = topOrder->next;
            cancel_order(topOrder->order_id);
            topOrder = nextOrder;
          } else {  // The order or price level qty is now 0
            break;
          }
        }
      }
    }
    if (order->qty == 0) return;
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
  } else {                            // Repeat the work but for incoming ASKS
    if (order->price <= best_bid_) {  // Order WILL consume liquidity
      while (order->qty > 0           // The incoming order's quantity > 0
             && best_bid_ >= order->price  // The incoming order's price is
                                           // less than the best bid (buy)
             && best_bid_ >= 0) {
        auto& level = bids_[best_bid_];

        if (level.total_qty ==
            0) {        // This will happen POST price level consumption
          best_bid_--;  // Fast forward past empty levels
          continue;
        }

        auto topOrder = level.head;
        while (topOrder && order->qty > 0) {
          // Safely caclulate the execution quantity to prevent underflow
          uint64_t execQuantity = std::min(order->qty, topOrder->qty);

          order->qty -= execQuantity;
          topOrder->qty -= execQuantity;
          level.total_qty -= execQuantity;

          if (topOrder->qty == 0) {  // i.e., it was the minimum
            Order* nextOrder = topOrder->next;
            cancel_order(topOrder->order_id);
            topOrder = nextOrder;
          } else {  // The order or price level qty is now 0
            break;
          }
        }
      }
    }
    if (order->qty == 0) return;
    // Add to the bids if the above conditions are not met
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
      best_ask_ = order->price;
    }
  }
}

}  // namespace lob
