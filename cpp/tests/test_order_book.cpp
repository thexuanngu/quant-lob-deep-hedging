#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>   // for GENERATE
#include <vector>

#include "lob/order_book.hpp"

using namespace lob;

struct BookFixture {
  static constexpr int64_t kLevels = 1000;
  OrderPool pool{256};
  OutputBuffer fills;
  OrderBook book;
  bool expect_clean = true;   // tests that provoke drops on purpose set this to false
  explicit BookFixture(int64_t offset = 1) : book(kLevels, offset, &pool, fills) {}
  ~BookFixture() {            // runs at the end of EVERY test that uses the fixture
    CHECK(book.check_invariants() == "");
    if (expect_clean) {
      CHECK(book.dropped_fills() == 0);
      CHECK(book.out_of_window() == 0);
      CHECK(pool.exhausted() == 0);
    }
  }
};

TEST_CASE_METHOD(BookFixture,
                 "OrderBook: maintains FIFO priority on resting orders",
                 "[orderbook]") {
  // Simulate setting the pool in the book (you'll need a setter or constructor
  // for this) book.set_pool(&pool);

  // 1. Arrange: Allocate and create two bids at the same price
  Order* order1 = pool.allocate(1, 100, 50, Side::Bid);
  Order* order2 = pool.allocate(2, 100, 25, Side::Bid);

  // 2. Act: Add them to the book sequentially
  book.add_order(order1);
  book.add_order(order2);

  // 3. Assert: Check the state of the book using Catch2's REQUIRE macros

  REQUIRE(book.best_bid() == 100);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 75);

  // // Check if the best bid moves
  Order* order3 = pool.allocate(3, 150, 25, Side::Bid);
  book.add_order(order3);

  REQUIRE(book.best_bid() == 150);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid,
                                           book.best_bid().value()) == 25);

  // Check the cancellation works
  book.cancel_order(order3->order_id);
  // Best bid should have changed
  REQUIRE(book.get_total_quantity_at_price(Side::Bid,
                                           book.best_bid().value()) == 75);

  // Check the queue is still 1,2
  auto result = book.get_order_count_at_level(Side::Bid, 100);
  REQUIRE(result.size() == 2);

  for (uint64_t i = 1; i <= 2; ++i) {
    REQUIRE(result[i - 1] == i);
  }
}

TEST_CASE_METHOD(BookFixture, "OrderBook: partial fill on first order",
                 "[orderbook]") {

  // 1. Arrange: Allocate and create two bids at the same price
  Order* order1 = pool.allocate(1, 100, 50, Side::Bid);
  Order* order2 = pool.allocate(2, 100, 25, Side::Bid);

  // 2. Act: Add them to the book sequentially
  book.add_order(order1);
  book.add_order(order2);

  // 3. Assert

  Order* order3 = pool.allocate(3, 100, 25, Side::Ask);
  book.add_order(order3);

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

TEST_CASE_METHOD(BookFixture, "OrderBook: clears first order", "[orderbook]") {
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

TEST_CASE_METHOD(BookFixture,
                 "OrderBook: aggressive orders that can clear multiple levels",
                 "[orderbook]") {
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
  REQUIRE(book.best_bid() == 130);

  book.add_order(order8);

  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 130) == 49);
  REQUIRE(book.best_bid() == 130);
  book.add_order(order9);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 130) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 120) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 110) == 25);
  book.add_order(order10);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 110) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 100) == 125);
}

TEST_CASE_METHOD(
    BookFixture,
    "OrderBook: aggressive orders that can clear multiple levels (ASK)",
    "[orderbook]") {
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
  REQUIRE(book.best_ask() == 120);

  book.add_order(order8);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 120) == 49);
  REQUIRE(book.best_ask() == 120);

  book.add_order(order9);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 120) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 130) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 140) == 25);

  book.add_order(order10);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 140) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 150) == 0);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 200) == 125);
}

TEST_CASE_METHOD(BookFixture, "one trade emits exactly one fill",
                 "[orderbook]") {
  book.add_order(pool.allocate(1, 100, 10, Side::Ask));
  book.add_order(pool.allocate(2, 100, 4, Side::Bid));
  REQUIRE(fills.size_approx() == 1);
  auto f = fills.try_pop();
  CHECK(f->maker_id == 1);
  CHECK(f->taker_id == 2);
  CHECK(f->qty == 4);
  CHECK(f->aggressor == Side::Bid);
}

TEST_CASE_METHOD(BookFixture, "three resting orders, cancel middle",
                 "[orderbook]") {
  book.add_order(pool.allocate(1, 100, 50, Side::Bid));
  book.add_order(pool.allocate(2, 100, 25, Side::Bid));
  book.add_order(pool.allocate(3, 100, 100, Side::Bid));
  book.cancel_order(2);
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 100) == 150);
  REQUIRE(book.get_order_count_at_level(Side::Bid, 100) ==
          std::vector<uint64_t>{1, 3});
}

TEST_CASE_METHOD(BookFixture, "Test the modification of a BID in place",
                 "[orderbook]") {
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

TEST_CASE_METHOD(BookFixture, "Test the modification of an ASK in place",
                 "[orderbook]") {
  // 1. Arrange: Allocate and create two bids at the same price
  Order* order1 = pool.allocate(1, 100, 1000, Side::Ask);
  Order* order2 = pool.allocate(2, 100, 100, Side::Ask);
  Order* order3 = pool.allocate(3, 100, 200, Side::Ask);
  Order* order4 = pool.allocate(4, 150, 500, Side::Ask);

  // 2. Act: Add them to the book sequentially
  book.add_order(order1);
  book.add_order(order2);
  book.add_order(order3);
  book.add_order(order4);

  // 3. Assert:

  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 100) == 1300);

  // The current order at 100 should be 1,2,3.
  auto result = book.get_order_count_at_level(Side::Ask, 100);
  REQUIRE(result.size() == 3);

  for (uint64_t i = 1; i <= 3; ++i) {
    REQUIRE(result[i - 1] == i);
  }

  // Now modify the quantity of order 1:
  book.modify_order(1, 100, 100);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 100) == 400);
  result = book.get_order_count_at_level(Side::Ask, 100);

  for (uint64_t i = 1; i <= 3; ++i) {
    REQUIRE((result[i - 1] % 3) == (i + 1) % 3);
  }

  // Modify again
  book.modify_order(1, 150, 100);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 100) == 300);
  result = book.get_order_count_at_level(Side::Ask, 100);
  REQUIRE(result.size() == 2);
  for (uint64_t i = 2; i <= 3; ++i) {
    REQUIRE((result[i - 2]) == i);
  }

  result = book.get_order_count_at_level(Side::Ask, 150);
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 150) == 600);
  REQUIRE(result.size() == 2);

  auto priceLevel = book.get_price_level(Side::Ask, 150);
  REQUIRE(priceLevel.head->order_id == 4);
  REQUIRE(priceLevel.tail->order_id == 1);
  REQUIRE(priceLevel.head->next->order_id == 1);
  REQUIRE(priceLevel.head->next->prev->order_id == 4);
}

TEST_CASE_METHOD(BookFixture, "Cancel only bid and ask", "[orderbook]") {
  book.add_order(pool.allocate(1, 90, 50, Side::Bid));
  book.add_order(pool.allocate(2, 100, 25, Side::Ask));
  REQUIRE(book.best_ask() == 100);
  REQUIRE(book.best_bid() == 90);

  book.cancel_order(1);
  book.cancel_order(2);
  REQUIRE(book.best_ask() == std::nullopt);
  REQUIRE(book.best_bid() == std::nullopt);
}

TEST_CASE("modify an ask keeps its side, at several offsets", "[orderbook]") {
  const int64_t off = GENERATE(0, 1, 1000);
  CAPTURE(off);
  BookFixture f(off);
  auto P = [off](int64_t p) { return off + p; };

  f.book.add_order(f.pool.allocate(1, P(100), 10, Side::Ask));
  f.book.add_order(f.pool.allocate(2, P(300), 10, Side::Ask));
  f.book.modify_order(1, P(105), 10);

  REQUIRE(f.book.get_total_quantity_at_price(Side::Ask, P(105)) == 10);
  REQUIRE(f.book.get_total_quantity_at_price(Side::Bid, P(105)) == 0);
  REQUIRE(f.book.best_ask() == P(105));
}

TEST_CASE("window edges at several offsets", "[orderbook]") {
  const int64_t off = GENERATE(0, 1, 1000);
  CAPTURE(off);
  BookFixture f(off);
  f.expect_clean = false;                                          // we provoke rejections on purpose
  f.book.add_order(f.pool.allocate(1, off + 0,    10, Side::Bid)); // lowest valid index
  f.book.add_order(f.pool.allocate(2, off + 999,  10, Side::Ask)); // highest valid index
  REQUIRE(f.book.best_bid() == off);
  REQUIRE(f.book.best_ask() == off + 999);
  f.book.add_order(f.pool.allocate(3, off + 1000, 10, Side::Ask)); // one past the end
  f.book.add_order(f.pool.allocate(4, off - 1,    10, Side::Bid)); // one before the start
  REQUIRE(f.book.out_of_window() == 2);
  REQUIRE(f.book.get_all_resting_orders() == 2);
}

TEST_CASE_METHOD(BookFixture, "modify with exhausted pool keeps the original order", "[orderbook]") {
  expect_clean = false;                      // we exhaust the pool on purpose
  for (uint64_t id = 1; id <= 256; ++id) {   // exactly the pool's capacity
    Order* o = pool.allocate(id, 100, 10, Side::Ask);
    REQUIRE(o != nullptr);                   // never hand a nullptr to add_order
    book.add_order(o);
  }
  REQUIRE(pool.exhausted() == 0);            // full, but nothing refused yet

  book.modify_order(1, 110, 50);             // needs a fresh slot, so the pool refuses

  CHECK(pool.exhausted() == 1);
  REQUIRE(book.get_all_resting_orders() == 256);                   // nothing lost
  REQUIRE(book.get_total_quantity_at_price(Side::Ask, 110) == 0);  // nothing moved
  const auto level = book.get_price_level(Side::Ask, 100);
  REQUIRE(level.head->order_id == 1);        // still first in the queue
  REQUIRE(level.head->qty == 10);            // original quantity untouched
}

TEST_CASE_METHOD(BookFixture, "fill: bid aggressor trades at the maker's price", "[orderbook][fills]") {
  book.set_time(1000);
  book.add_order(pool.allocate(7, 100, 10, Side::Ask));
  book.set_time(2500);
  book.add_order(pool.allocate(8, 105, 4, Side::Bid));   // limit 105 vs resting ask at 100
  REQUIRE(fills.size_approx() == 1);
  auto f = fills.try_pop();
  REQUIRE(f.has_value());
  CHECK(f->ts_ns == 2500);        // the taker's event time
  CHECK(f->maker_id == 7);
  CHECK(f->taker_id == 8);
  CHECK(f->price == 100);         // maker's price, not the 105 limit
  CHECK(f->qty == 4);
  CHECK(f->aggressor == Side::Bid);
}

TEST_CASE_METHOD(BookFixture, "fills: ask aggressor sweeps two levels in price-time order", "[orderbook][fills]") {
  book.add_order(pool.allocate(1, 100, 5, Side::Bid));   // best level, first in queue
  book.add_order(pool.allocate(2, 100, 5, Side::Bid));   // same level, second
  book.add_order(pool.allocate(3, 99, 5, Side::Bid));    // next level
  book.add_order(pool.allocate(9, 99, 12, Side::Ask));   // sells 12 down to 99

  std::vector<ExecutionEvent> got;
  while (auto f = fills.try_pop()) got.push_back(*f);
  REQUIRE(got.size() == 3);
  CHECK((got[0].maker_id == 1 && got[0].price == 100 && got[0].qty == 5));
  CHECK((got[1].maker_id == 2 && got[1].price == 100 && got[1].qty == 5));
  CHECK((got[2].maker_id == 3 && got[2].price == 99  && got[2].qty == 2));
  uint64_t total = 0;
  for (const auto& g : got) { CHECK(g.taker_id == 9); CHECK(g.aggressor == Side::Ask); total += g.qty; }
  CHECK(total == 12);                                     // quantity conserved
  REQUIRE(book.get_total_quantity_at_price(Side::Bid, 99) == 3);
}

TEST_CASE_METHOD(BookFixture, "check_invariants detects a leaked pool slot", "[invariants]") {
  book.add_order(pool.allocate(1, 100, 10, Side::Bid));
  REQUIRE(book.check_invariants() == "");
  Order* leaked = pool.allocate(2, 100, 10, Side::Bid);  // allocated, never added
  REQUIRE(book.check_invariants() != "");
  pool.deallocate(leaked);                               // repair so the end-of-test check passes
}