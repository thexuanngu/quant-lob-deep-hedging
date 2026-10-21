#include "lob/order_book.hpp"

#include <algorithm>
#include <vector>

namespace lob {

// TESTING FUNCTIONS

std::optional<int64_t> OrderBook::best_bid() const noexcept {
  return best_bid_ == kNoBid
             ? std::nullopt
             : std::optional<int64_t>{best_bid_ + tick_price_offset_};
}
std::optional<int64_t> OrderBook::best_ask() const noexcept {
  return best_ask_ == kNoAsk
             ? std::nullopt
             : std::optional<int64_t>{best_ask_ + tick_price_offset_};
}

std::size_t OrderBook::get_all_resting_orders() { return order_map_.size(); }

uint64_t OrderBook::get_total_quantity_at_price(Side side, int64_t price) {
  return (side == Side::Bid) ? bids_[price - tick_price_offset_].total_qty
                             : asks_[price - tick_price_offset_].total_qty;
}

PriceLevel OrderBook::get_price_level(Side side, int64_t price) {
  return (side == Side::Ask) ? asks_[price - tick_price_offset_]
                             : bids_[price - tick_price_offset_];
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

std::string OrderBook::check_invariants() const {  // end of every test -> check invariants
  const auto n = static_cast<int64_t>(asks_.size());
  const std::size_t guard = order_pool_->capacity();  // a longer list must be corrupt
  std::size_t reachable = 0;
  int64_t best_bid_seen = kNoBid;
  int64_t best_ask_seen = kNoAsk;

  for (Side side : {Side::Bid, Side::Ask}) {
    const auto& levels = (side == Side::Bid) ? bids_ : asks_;
    const std::string name = (side == Side::Bid) ? "bid" : "ask";
    for (int64_t idx = 0; idx < n; ++idx) {
      const PriceLevel& lv = levels[idx];
      auto where = [&] { return name + " level " + std::to_string(idx) + ": "; };

      if ((lv.head == nullptr) != (lv.tail == nullptr)) return where() + "head/tail null-ness differs";

      uint64_t sum = 0;
      const Order* prev = nullptr;
      std::size_t steps = 0;

      // Walk the orders in the level
      for (const Order* o = lv.head; o != nullptr; prev = o, o = o->next) {
        if (++steps > guard) return where() + "list longer than the pool (cycle?)";
        if (o->prev != prev) return where() + "prev link broken";
        if (o->side != side) return where() + "order on the wrong side";
        if (o->price - tick_price_offset_ != idx) return where() + "order price does not match its level";
        if (o->qty == 0) return where() + "resting order with zero qty";  // no resting orders should have 0 qty
        auto it = order_map_.find(o->order_id);
        if (it == order_map_.end() || it->second != o) return where() + "order missing from order_map_";
        sum += o->qty;
        ++reachable;
      }

      // after walking orders
      if (prev != lv.tail) return where() + "tail is not the last node";
      if (sum != lv.total_qty) return where() + "total_qty != sum of order qtys";
      if (lv.head != nullptr) {
        if (side == Side::Bid) best_bid_seen = idx;               // last non-empty bid = highest
        else if (best_ask_seen == kNoAsk) best_ask_seen = idx;    // first non-empty ask = lowest
      }
    }
  }

  // after walking all the levels
  if (reachable != order_map_.size()) return "order_map_ size != orders reachable from the levels";
  if (best_bid_seen != best_bid_) return "best_bid_ is stale";
  if (best_ask_seen != best_ask_) return "best_ask_ is stale";
  if (best_bid_ != kNoBid && best_ask_ != kNoAsk && best_bid_ >= best_ask_) return "book is crossed";
  if (order_pool_->free_count() + reachable != order_pool_->capacity())
    return "pool slots leaked or double-freed (free + resting != capacity)";
  return {};
}

// CORE FUNCTIONALITY

OrderBook::OrderBook(int64_t num_price_levels, int64_t tick_price_offset,
                     OrderPool* order_pool, OutputBuffer& outbound_fills)
    : asks_(num_price_levels),
      bids_(num_price_levels),
      tick_price_offset_(
          tick_price_offset),  // Be careful with setting this to 0
      order_pool_(order_pool),
      outbound_fills_(outbound_fills) {}

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
  if (order->prev != nullptr)
    order->prev->next = order->next;
  else
    level.head = order->next;  // Delete the head

  // 3. Safely unlink the next pointer
  if (order->next != nullptr)
    order->next->prev = order->prev;
  else
    level.tail = order->prev;  // Delete the tail (if we were at tail)

  // 4. Update the state variables
  level.total_qty -= order->qty;

  // 4b. If necessary, update the best_bid/ask
  if (level.total_qty == 0) {
    int64_t temp_idx = indexed_price;
    if (order->side == Side::Bid && indexed_price == best_bid_) {
      while (temp_idx >= 0 && bids_[temp_idx].total_qty == 0) temp_idx--;
      best_bid_ = temp_idx;  // falls out as temp_idx == -1
    } else if (order->side == Side::Ask && indexed_price == best_ask_) {
      const auto n = static_cast<int64_t>(asks_.size());
      while ((temp_idx < n) && (asks_[temp_idx].total_qty == 0)) temp_idx++;
      best_ask_ = (temp_idx < n) ? temp_idx : kNoAsk;
    }
  }
  order_map_.erase(it);

  // Note: Return `order` to a pre-allocated object pool here -> do not 'delete
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
      order->price >= tick_price_offset_ + static_cast<int64_t>(asks_.size())) {
    // Silently reject orders outside the acceptable window
    out_of_window_++;  // TODO(Me): Replace with a getter?
    order_pool_->deallocate(order);
    return;
  }

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

          const ExecutionEvent ev{
              .ts_ns = now_ns_,
              .maker_id = topOrder->order_id,
              .taker_id = order->order_id,
              .price = topOrder->price,
              .qty = execQuantity,
              .aggressor = Side::Bid};  // Ask in the other branch
          if (!outbound_fills_.try_push(ev)) ++dropped_fills_;

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
    if (order->qty == 0) {
      // Fully filled as the aggressive side - it never rests, so return
      // it to the pool now rather than leaking this slot forever.
      order_pool_->deallocate(order);
      return;
    }
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

          // notify that a trade has been executed
          const ExecutionEvent ev{
              .ts_ns = now_ns_,
              .maker_id = topOrder->order_id,
              .taker_id = order->order_id,
              .price = topOrder->price,
              .qty = execQuantity,
              .aggressor = Side::Ask};  // Ask in the other branch
          if (!outbound_fills_.try_push(ev)) ++dropped_fills_;

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
    if (order->qty == 0) {
      // Fully filled as the aggressive side - it never rests, so return
      // it to the pool now rather than leaking this slot forever.
      order_pool_->deallocate(order);
      return;
    }
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

void OrderBook::modify_order(uint64_t id, int64_t new_price, uint64_t new_qty) {

  auto it = order_map_.find(id);
  if (it == order_map_.end()) return;

  // In a real exchange, reducing quantity maintains priority (no cancel/re-add
  // needed). For now, to keep it simple and enforce queue penalty for changes:

  // modifying order w/ 0 qty = cancel
  if (new_qty == 0) {
    cancel_order(id);
    return;
  }

  if (new_price < tick_price_offset_ ||
    new_price >= tick_price_offset_ + static_cast<int64_t>(asks_.size())) {
    // Silently reject orders outside the acceptable window
    out_of_window_++;
    return;
  }

  // Retrieve a fresh pointer first, cancel existing, populate, and add
  const Side side = it->second->side;
  Order* fresh = order_pool_->allocate(id, new_price, new_qty, side);
  if (!fresh) return;
  cancel_order(id);
  add_order(fresh);
}

}  // namespace lob
