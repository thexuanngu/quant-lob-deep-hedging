#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <numeric>
#include <thread>
#include <vector>

#include "lob/spsc_ring_buffer.hpp"

TEST_CASE("push/pop round-trip preserves order", "[ring_buffer]") {
  lob::SpscRingBuffer<int, 8> rb;

  REQUIRE(rb.try_push(1));
  REQUIRE(rb.try_push(2));
  REQUIRE(rb.try_push(3));

  auto a = rb.try_pop();
  auto b = rb.try_pop();
  auto c = rb.try_pop();

  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  REQUIRE(c.has_value());
  CHECK(*a == 1);
  CHECK(*b == 2);
  CHECK(*c == 3);
}

TEST_CASE("pop on empty buffer returns nullopt", "[ring_buffer]") {
  lob::SpscRingBuffer<int, 4> rb;
  CHECK_FALSE(rb.try_pop().has_value());
}

TEST_CASE("push fails once buffer is full", "[ring_buffer]") {
  // Capacity 4 means 3 usable slots (one slot always kept empty to
  // distinguish full from empty using only head/tail).
  lob::SpscRingBuffer<int, 4> rb;
  CHECK(rb.try_push(1));
  CHECK(rb.try_push(2));
  CHECK(rb.try_push(3));
  CHECK_FALSE(rb.try_push(4));  // full

  REQUIRE(rb.try_pop().has_value());
  CHECK(rb.try_push(4));  // now there's room again
}

TEST_CASE("wraparound preserves FIFO order over many cycles", "[ring_buffer]") {
  lob::SpscRingBuffer<int, 4> rb;
  for (int cycle = 0; cycle < 100; ++cycle) {
    REQUIRE(rb.try_push(cycle));
    REQUIRE(rb.try_push(cycle * 1000));
    auto a = rb.try_pop();
    auto b = rb.try_pop();
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == cycle);
    CHECK(*b == cycle * 1000);
  }
}

TEST_CASE("concurrent single-producer/single-consumer preserves all items and order",
          "[ring_buffer][concurrency]") {
  constexpr std::size_t kItems = 2'000'000;
  lob::SpscRingBuffer<std::uint64_t, 1024> rb;

  std::atomic<bool> producer_done{false};
  std::thread producer([&] {
    for (std::uint64_t i = 0; i < kItems; ++i) {
      while (!rb.try_push(i)) {
        std::this_thread::yield();  // backpressure: consumer is behind
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  std::uint64_t expected = 0;
  std::uint64_t consumed = 0;
  std::thread consumer([&] {
    while (consumed < kItems) {
      if (auto v = rb.try_pop()) {
        REQUIRE(*v == expected);  // strict FIFO order, no drops/dups
        ++expected;
        ++consumed;
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  CHECK(producer_done.load());
  CHECK(consumed == kItems);
  CHECK(expected == kItems);
}
