#include <order_book.hpp>
#include <spsc_ring_buffer.hpp>

namespace lob {
class OrderBook {
  void add_order(Order* order) {
    // scenario to consider: aggressive order (cross the book) that partically
    // depletes the head
    auto tradeSide = order->side;

    (tradeSide == Side::Bid) ? apply_matching_logic(order)
  }

  void apply_matching_logic(Order* order,
                            std::vector<PriceLevel> tradingPriceLevels,
                            std::vector<PriceLevel> storagePriceLevels) {
    // Case 1: Order doesn't cross the book:
    auto orderIndex = order->prev / ...  // tick_size
  }

  void cancel_order(uint64_t order_id) {}
};
}  // namespace lob