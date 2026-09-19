#include <catch2/catch_test_macros.hpp>

#include "lob/order_book.hpp"

using namespace lob;

TEST_CASE("OrderBook: maintains FIFO priority on resting orders",
          "[orderbook]") {
  int64_t num_price_levels = 1000;
  double tick_size = 5.0;
  int64_t tick_price_offset = 1;
  OrderPool pool(256);

  OrderBook book(num_price_levels, tick_size, tick_price_offset, &pool);
  // Simulate setting the pool in the book (you'll need a setter or constructor
  // for this) book.set_pool(&pool);

  // 1. Arrange: Allocate and create two bids at the same price
  Order* order1 = pool.allocate(1, 100, 50, Side::Bid);
  Order* order2 = pool.allocate(2, 100, 25, Side::Bid);

  // 2. Act: Add them to the book sequentially
  book.add_order(order1);
  book.add_order(order2);

  // 3. Assert: Check the state of the book using Catch2's REQUIRE macros
  // (You will need to expose some read-only getters in OrderBook to make these
  // assertions

  REQUIRE(book.get_best_bid() == 100);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 75);

  // // Check if the best bid moves
  Order* order3 = pool.allocate(3, 150, 25, Side::Bid);
  book.add_order(order3);

  REQUIRE(book.get_best_bid() == 150);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, book.get_best_bid()) ==
          25);

  // Check the cancellation works
  book.cancel_order(order3->order_id);
  // Best bid won't have moved, but the quantity should have changed
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, book.get_best_bid()) ==
          0);
  // REQUIRE(book.get_best_bid() == 100); // TODO: Is it worth checking for
  // cancelled orders?
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 75);

  // Check the queue is still 1,2
  auto result = book.get_order_count_at_level(Side::Bid, 100);
  REQUIRE(result.size() == 2);

  for (uint64_t i = 1; i <= 2; ++i) {
    REQUIRE(result[i - 1] == i);
  }
}

TEST_CASE("OrderBook: partial fill on first order", "[orderbook]") {
  int64_t num_price_levels = 1000;
  double tick_size = 5.0;
  int64_t tick_price_offset = 1;
  OrderPool pool(256);

  OrderBook book(num_price_levels, tick_size, tick_price_offset, &pool);
  // Simulate setting the pool in the book (you'll need a setter or constructor
  // for this) book.set_pool(&pool);

  // 1. Arrange: Allocate and create two bids at the same price
  Order* order1 = pool.allocate(1, 100, 50, Side::Bid);
  Order* order2 = pool.allocate(2, 100, 25, Side::Bid);

  // 2. Act: Add them to the book sequentially
  book.add_order(order1);
  book.add_order(order2);

  // 3. Assert: Check the state of the book using Catch2's REQUIRE macros
  // (You will need to expose some read-only getters in OrderBook to make these
  // assertions)

  // // Check if the best bid moves
  Order* order3 = pool.allocate(3, 150, 25, Side::Bid);
  book.add_order(order3);

  // Check the cancellation works
  book.cancel_order(order3->order_id);
  // Best bid won't have moved, but the quantity should have changed
  REQUIRE(book.get_best_bid() == 150);
  order3 = pool.allocate(3, 100, 25, Side::Ask);
  book.add_order(order3);

  REQUIRE(book.get_best_bid() ==
          100);  // Check the best bid has moved appropriately
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 50);
  // Check the queue is still 1,2
  auto result = book.get_order_count_at_level(Side::Bid, 100);
  REQUIRE(result.size() == 2);

  for (uint64_t i = 1; i <= 2; ++i) {
    REQUIRE(result[i - 1] == i);
  }
  auto head = book.get_price_level(Side::Bid, 100).head;
  while (head) {
    REQUIRE(head->qty == 25);
    head = head->next;
  }
}

TEST_CASE("OrderBook: clears first order", "[orderbook]") {
  int64_t num_price_levels = 1000;
  double tick_size = 5.0;
  int64_t tick_price_offset = 1;
  OrderPool pool(256);

  OrderBook book(num_price_levels, tick_size, tick_price_offset, &pool);
  // Simulate setting the pool in the book (you'll need a setter or constructor
  // for this) book.set_pool(&pool);

  // 1. Arrange: Allocate and create two bids at the same price
  Order* order1 = pool.allocate(1, 100, 50, Side::Bid);
  Order* order2 = pool.allocate(2, 100, 25, Side::Bid);

  // 2. Act: Add them to the book sequentially
  book.add_order(order1);
  book.add_order(order2);

  // 3. Assert: Check the state of the book using Catch2's REQUIRE macros
  // (You will need to expose some read-only getters in OrderBook to make these
  // assertions)

  Order* order3 = pool.allocate(3, 100, 50, Side::Ask);
  book.add_order(order3);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 25);
  // Check the queue is now just 2
  auto result = book.get_order_count_at_level(Side::Bid, 100);
  REQUIRE(result.size() == 1);
  REQUIRE(result[0] == 2);
}

// TODO: This test needs debugging to actually inspect the interior of the
// add_order function TEST_CASE("OrderBook: order beyond window correctly
// doesn't populate",
//           "[orderbook]") {
//   int64_t num_price_levels = 1000;
//   double tick_size = 5.0;
//   int64_t tick_price_offset = 1;
//   OrderPool pool(256);

//   OrderBook book(num_price_levels, tick_size, tick_price_offset, &pool);
//   // Simulate setting the pool in the book (you'll need a setter or
//   constructor
//   // for this) book.set_pool(&pool);

//   // 1. Arrange: Allocate and create two bids at the same price
//   Order* order1 = pool.allocate(1, 1e10, 50, Side::Bid);

//   // 2. Act: Add them to the book sequentially
//   book.add_order(order1);

//   // 3. Assert: Check the state of the book using Catch2's REQUIRE macros
//   // (You will need to expose some read-only getters in OrderBook to make
//   these
//   // assertions)
// }

TEST_CASE("OrderBook: aggressive orders that can clear multiple levels",
          "[orderbook]") {
  int64_t num_price_levels = 1000;
  double tick_size = 5.0;
  int64_t tick_price_offset = 1;

  OrderPool pool(256);
  OrderBook book(num_price_levels, tick_size, tick_price_offset, &pool);
  // Simulate setting the pool in the book (you'll need a setter or constructor
  // for this) book.set_pool(&pool);

  // 1. Arrange: Allocate and create two bids at the same price
  Order* order1 = pool.allocate(1, 100, 50, Side::Bid);
  Order* order2 = pool.allocate(2, 110, 50, Side::Bid);
  Order* order3 = pool.allocate(3, 120, 50, Side::Bid);
  Order* order4 = pool.allocate(4, 130, 50, Side::Bid);
  Order* order5 = pool.allocate(5, 140, 50, Side::Bid);
  Order* order6 = pool.allocate(6, 150, 50, Side::Bid);

  // Aggressive sell orders -> The "best" bid is going to be a little 'laggy'
  Order* order7 =
      pool.allocate(7, 100, 100, Side::Ask);  // Can it clear TWO price levels?

  Order* order8 = pool.allocate(
      8, 100, 1,
      Side::Ask);  // This will check the best bid can move appropriately
  Order* order9 =
      pool.allocate(9, 100, 49 + 75, Side::Ask);  // Full + Partial Clear
  Order* order10 =
      pool.allocate(10, 100, 200, Side::Ask);  // Full + Partial Clear

  // 2 & 3. Act: Add them to the book sequentially and Check
  book.add_order(order1);
  book.add_order(order2);
  book.add_order(order3);
  book.add_order(order4);
  book.add_order(order5);
  book.add_order(order6);

  book.add_order(order7);
  // FIFO -> Order6 is consumed first
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 150) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 140) == 0);
  REQUIRE(book.get_best_bid() ==
          140);  // Laggy best_bid => only really updates with more add orders

  book.add_order(order8);

  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 130) == 49);
  REQUIRE(book.get_best_bid() == 130);
  book.add_order(order9);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 130) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 120) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 110) == 25);
  book.add_order(order10);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 110) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 100) == 125);
}

TEST_CASE("OrderBook: aggressive orders that can clear multiple levels (ASK)",
          "[orderbook]") {
  int64_t num_price_levels = 1000;
  double tick_size = 5.0;
  int64_t tick_price_offset = 1;

  OrderPool pool(256);
  OrderBook book(num_price_levels, tick_size, tick_price_offset, &pool);
  // Simulate setting the pool in the book (you'll need a setter or constructor
  // for this) book.set_pool(&pool);

  // 1. Arrange: Allocate and create two bids at the same price
  Order* order1 = pool.allocate(1, 100, 50, Side::Ask);
  Order* order2 = pool.allocate(2, 110, 50, Side::Ask);
  Order* order3 = pool.allocate(3, 120, 50, Side::Ask);
  Order* order4 = pool.allocate(4, 130, 50, Side::Ask);
  Order* order5 = pool.allocate(5, 140, 50, Side::Ask);
  Order* order6 = pool.allocate(6, 150, 50, Side::Ask);

  // Aggressive buy orders
  Order* order7 = pool.allocate(7, 200, 100, Side::Bid);
  Order* order8 = pool.allocate(8, 200, 1, Side::Bid);
  Order* order9 = pool.allocate(9, 200, 49 + 75, Side::Bid);
  Order* order10 = pool.allocate(10, 200, 200, Side::Bid);

  // 2 & 3. Act: Add them to the book sequentially and Check
  book.add_order(order1);
  book.add_order(order2);
  book.add_order(order3);
  book.add_order(order4);
  book.add_order(order5);
  book.add_order(order6);
  book.add_order(order7);
  // FIFO -> Order6 is consumed first
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 100) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 110) == 0);
  REQUIRE(book.get_best_ask() == 110);

  book.add_order(order8);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 120) == 49);
  REQUIRE(book.get_best_ask() == 120);

  book.add_order(order9);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 120) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 130) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 140) == 25);

  book.add_order(order10);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 140) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 150) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 200) == 125);
}

TEST_CASE("three resting orders, cancel middle", "[orderbook]") {
  int64_t num_price_levels = 1000;
  double tick_size = 5.0;
  int64_t tick_price_offset = 1;
  OrderPool pool(256);

  OrderBook book(num_price_levels, tick_size, tick_price_offset, &pool);
  // Simulate setting the pool in the book (you'll need a setter or constructor
  // for this) book.set_pool(&pool);

  // 1. Arrange: Allocate and create two bids at the same price
  Order* order1 = pool.allocate(1, 100, 50, Side::Bid);
  Order* order2 = pool.allocate(2, 100, 25, Side::Bid);
  Order* order3 = pool.allocate(3, 100, 100, Side::Bid);

  // 2. Act: Add them to the book sequentially
  book.add_order(order1);
  book.add_order(order2);
  book.add_order(order3);

  // 3. Assert: Check the state of the book using Catch2's REQUIRE macros
  // (You will need to expose some read-only getters in OrderBook to make these
  // assertions)

  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 175);
  REQUIRE(book.get_order_count_at_level(Side::Bid, 100).size() == 3);

  // Cancel the middle order
  book.cancel_order(2);
  auto result = book.get_order_count_at_level(Side::Bid, 100);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 150);
  REQUIRE(result.size() == 2);

  auto priceLevel = book.get_price_level(Side::Bid, 100);
  REQUIRE(priceLevel.head->order_id == 1);
  REQUIRE(priceLevel.tail->order_id == 3);
  REQUIRE(priceLevel.head->next->order_id == 3);
  REQUIRE(priceLevel.head->next->prev->order_id == 1);
}

TEST_CASE("Test the modification of an order in place", "[orderbook]") {
  int64_t num_price_levels = 1000;
  double tick_size = 5.0;
  int64_t tick_price_offset = 1;
  OrderPool pool(256);

  OrderBook book(num_price_levels, tick_size, tick_price_offset, &pool);
  // Simulate setting the pool in the book (you'll need a setter or constructor
  // for this) book.set_pool(&pool);

  // 1. Arrange: Allocate and create two bids at the same price
  Order* order1 = pool.allocate(1, 100, 1000, Side::Bid);
  Order* order2 = pool.allocate(2, 100, 100, Side::Bid);
  Order* order3 = pool.allocate(3, 100, 200, Side::Bid);
  Order* order4 = pool.allocate(4, 150, 500, Side::Bid);

  // 2. Act: Add them to the book sequentially
  book.add_order(order1);
  book.add_order(order2);
  book.add_order(order3);
  book.add_order(order4);

  // 3. Assert: Check the state of the book using Catch2's REQUIRE macros
  // (You will need to expose some read-only getters in OrderBook to make these
  // assertions)

  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 1300);

  // The current order at 100 should be 1,2,3.
  auto result = book.get_order_count_at_level(Side::Bid, 100);
  REQUIRE(result.size() == 3);

  for (uint64_t i = 1; i <= 3; ++i) {
    REQUIRE(result[i - 1] == i);
  }

  // Now modify the quantity of order 1:
  book.modify_order(1, 100, 100);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 400);
  result = book.get_order_count_at_level(Side::Bid, 100);

  for (uint64_t i = 1; i <= 3; ++i) {
    REQUIRE((result[i - 1] % 3) == (i + 1) % 3);
  }

  // Modify again
  book.modify_order(1, 150, 100);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 300);
  result = book.get_order_count_at_level(Side::Bid, 100);
  REQUIRE(result.size() == 2);
  for (uint64_t i = 2; i <= 3; ++i) {
    REQUIRE((result[i - 2]) == i);
  }

  result = book.get_order_count_at_level(Side::Bid, 150);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 150) == 600);
  REQUIRE(result.size() == 2);

  auto priceLevel = book.get_price_level(Side::Bid, 150);
  REQUIRE(priceLevel.head->order_id == 4);
  REQUIRE(priceLevel.tail->order_id == 1);
  REQUIRE(priceLevel.head->next->order_id == 1);
  REQUIRE(priceLevel.head->next->prev->order_id == 4);
}
