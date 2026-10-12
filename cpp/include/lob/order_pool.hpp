#pragma once
#include <cstdint>
#include <vector>

#include "lob/types.hpp"

namespace lob {
class OrderPool {
 public:
  explicit OrderPool(std::size_t capacity);


  OrderPool(const OrderPool&) = delete;
  OrderPool& operator=(const OrderPool&) = delete;
  // O(1) allocation: Pop a pointer off the back
  Order* allocate();

  // O(1) allocation with ORDER quantities initialized
  Order* allocate(uint64_t order_id, int64_t price, uint64_t qty, Side side);

  // O(1) deallocation: Push the pointer back to the list
  void deallocate(Order* order);

  uint64_t exhausted() const noexcept { return pool_exhausted_; }
  uint64_t free_count() const noexcept { return free_list_.size();}

 private:
  std::vector<Order> pool_;
  std::vector<Order*> free_list_;
  uint64_t pool_exhausted_ = 0;
};
}  // namespace lob