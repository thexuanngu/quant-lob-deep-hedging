#pragma once
#include <vector>
#include <cstdint>

#include "lob/types.hpp"

namespace lob {
    class OrderPool {
    public:
    explicit OrderPool(std::size_t capacity);

    // O(1) allocation: Pop a pointer off the back
    Order* allocate();

    // O(1) allocation with ORDER quantities initialized
    Order* allocate(uint64_t order_id, int64_t price, uint64_t qty, Side side);

    // O(1) deallocation: Push the pointer back to the list
    void deallocate(Order* order);

    private:
    std::vector<Order> pool_;
    std::vector<Order*> free_list_;
    };
}