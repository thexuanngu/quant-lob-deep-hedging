#include <order_book.hpp>
#include <spsc_ring_buffer.hpp>

namespace lob {
class OrderBook {
  void add_order(Order* order) {
    // scenario to consider: aggressive order (cross the book) that partically
    // depletes the head
  }

  void cancel_order(uint64_t order_id) {}
};
}  // namespace lob