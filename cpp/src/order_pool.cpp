#include <algorithm>

#include "lob/order_book.hpp"
#include "lob/spsc_ring_buffer.hpp"

namespace lob {
OrderPool::OrderPool(std::size_t capacity) {
  pool_.resize(capacity);
  free_list_.reserve(capacity);
  // Push the memory addresses of the pre-allocated vector into the free list
  for (std::size_t i = 0; i < capacity; ++i) {
    free_list_.push_back(&pool_[i]);
  }
}

Order* OrderPool::allocate() {
  if (free_list_.empty())
    return nullptr;  // In production, handle capacity exhaustion
  Order* order = free_list_.back();
  free_list_.pop_back();
  return order;
}

Order* OrderPool::allocate(uint64_t order_id, int64_t price, uint64_t qty,
                           Side side) {
  if (free_list_.empty())
    return nullptr;  // In production, handle capacity exhaustion
  Order* order = free_list_.back();
  free_list_.pop_back();
  order->order_id = order_id;
  order->price = price;
  order->qty = qty;
  order->side = side;
  return order;
}

void OrderPool::deallocate(Order* order) {
  // Reset structural pointers before returning to pool
  order->prev = nullptr;
  order->next = nullptr;
  order->order_id = 0;
  order->price = 0;
  order->qty = 0;
  order->side = Side::Bid;

  free_list_.push_back(order);
}

}  // namespace lob
