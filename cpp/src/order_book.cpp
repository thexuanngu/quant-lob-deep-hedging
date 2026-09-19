#include "lob/order_book.hpp"

#include <algorithm>

#include "lob/spsc_ring_buffer.hpp"

namespace lob {

// TESTING FUNCTIONS

int64_t OrderBook::get_best_bid() {
  return (best_bid_ + tick_price_offset_);  // TODO: Deal with the tick_size
                                            // multiplication differently
}
int64_t OrderBook::get_best_ask() { return (best_ask_ + tick_price_offset_); }

uint64_t OrderBook::get_total_quantity_at_price(Side side, int64_t price) {
  return (side == Side::Bid) ? bids_[price - tick_price_offset_].total_qty
                             : asks_[price - tick_price_offset_].total_qty;
}

std::vector<uint64_t> OrderBook::get_order_count_at_level(Side side,
                                                          int64_t price) {
  auto queue_of_interest = (side == Side::Bid)
                               ? bids_[price - tick_price_offset_]
                               : asks_[price - tick_price_offset_];
  std::vector<uint64_t> res;
  auto curHead = queue_of_interest.head;
  while (curHead) {
    res.push_back(curHead->order_id);
    curHead = curHead->next;
  }
  return res;
}

std::size_t OrderBook::get_all_resting_orders() { return order_map_.size(); }

PriceLevel OrderBook::get_price_level(Side side, int64_t price) {
  return (side == Side::Ask) ? asks_[price - tick_price_offset_]
                             : bids_[price - tick_price_offset_];
}

// CORE FUNCTIONALITY

OrderBook::OrderBook(int64_t num_price_levels, double tick_size,
                     int64_t tick_price_offset, OrderPool* order_pool)
    : asks_(num_price_levels),
      bids_(num_price_levels),
      tick_size_(tick_size),
      tick_price_offset_(
          tick_price_offset),  // Be careful with setting this to 0
      order_pool_(order_pool) {}

void OrderBook::cancel_order(uint64_t order_id) {
  // Check the order exists (might be redundant with careful design
  auto it = order_map_.find(order_id);
  if (it == order_map_.end()) return;

  Order* order = it->second;
  // 1. Locate the current price level
  // Remember: convert ORDER (original) price to the STORED (offset) price
  auto indexed_price = order->price - tick_price_offset_;
  PriceLevel& level =
      (order->side == Side::Ask) ? asks_[indexed_price] : bids_[indexed_price];

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
  // 4b. If necessary, update the best_bid/ask
  // TODO: Do I need a logic check for updating the best bid and ask here?
  order_map_.erase(it);

  // Note: Return `order` to a pre-allocated object pool here. DO NOT use
  // `delete`.
  order_pool_->deallocate(order);
}

/* 2 Main scenarios to handle:
  1) Order doesn't even cross the order book
  2) Order crosses the order book (aggressive)
    a. Order consumes multiple levels of liquidity
    b. Order doesn't entirely consume the head liquidity
   */
void OrderBook::add_order(Order* order) {
  // Check if the order is within the bounds of the LOB
  if (order->price < tick_price_offset_ ||
      order->price > tick_price_offset_ * asks_.size()) {
    return;
  }  // Silently reject orders outside the acceptable window

  // Generate the relevant index
  auto indexed_price = order->price - tick_price_offset_;
  // Determine whether we've received a BID (buy) or ASK (sell) order
  if (order->side == Side::Bid) {           // User wants to buy (BID)
    if (indexed_price >= best_ask_) {       // Order WILL consume liquidity
      while (order->qty > 0                 // The incoming order's quantity > 0
             && best_ask_ <= indexed_price  // incoming order's price > best ask
             && best_ask_ < static_cast<int64_t>(asks_.size())) {
        auto& level = asks_[best_ask_];

        if (level.total_qty == 0) {  // happens POST price level consumption
          best_ask_++;               // Fast forward past empty levels
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
    PriceLevel& level = bids_[indexed_price];

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
    if (indexed_price > best_bid_) {
      best_bid_ = indexed_price;
    }
  } else {                             // Repeat the work but for incoming ASKS
    if (indexed_price <= best_bid_) {  // Order WILL consume liquidity
      while (order->qty > 0            // The incoming order's quantity > 0
             && best_bid_ >= indexed_price  // incoming order's price < best bid
             && best_bid_ >= 0) {
        auto& level = bids_[best_bid_];

        if (level.total_qty == 0) {  // happens POST price level consumption
          best_bid_--;               // Fast forward past empty levels
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
    // Add to the asks if the above conditions are not met
    PriceLevel& level = asks_[indexed_price];

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
    if (indexed_price < best_ask_) {
      best_ask_ = indexed_price;
    }
  }
}

void OrderBook::modify_order(uint64_t order_id, int64_t new_price,
                             uint64_t new_qty) {
  auto it = order_map_.find(order_id);
  if (it == order_map_.end()) return;

  Order* existing_order = it->second;

  // In a real exchange, reducing quantity maintains priority (no cancel/re-add
  // needed). For now, to keep it simple and enforce queue penalty for changes:
  Side side = existing_order->side;

  cancel_order(order_id);

  // Retrieve a fresh pointer, populate it, and add it.
  Order* new_order = order_pool_->allocate();
  new_order->order_id = order_id;
  new_order->price = new_price;
  new_order->qty = new_qty;
  new_order->side = side;

  add_order(new_order);
}

}  // namespace lob
