#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <type_traits>

namespace lob {

// Cache line size used to pad hot atomics apart so the producer's writes to
// `tail_` and the consumer's writes to `head_` never bounce the same cache
// line between cores (false sharing). Deliberately hardcoded rather than
// `std::hardware_destructive_interference_size`: that constant is allowed to
// vary with -mtune/-mcpu, which silently changes this struct's layout across
// translation units compiled with different flags - exactly the kind of ODR
// landmine you don't want in a component meant to be linked into both a
// Python extension module and a native test binary. 64B covers essentially
// every x86_64 and arm64 target this project targets; revisit explicitly if
// you ever deploy to something exotic.
inline constexpr std::size_t kCacheLineSize = 64;

// Single-producer, single-consumer lock-free ring buffer.
//
// This is intentionally SPSC, not MPMC: a real feed-handler architecture is
// naturally SPSC per link (one kernel-bypass / socket thread produces, one
// strategy thread consumes). Fan-in from multiple venues/symbols should be
// done with one SPSC queue per feed, not a single contended MPMC queue -
// that gives better cache locality and avoids the CAS-retry storms that
// plague general MPMC designs under bursty arrival (Hawkes-clustered order
// flow will absolutely produce those bursts). For fan-out or true MPMC
// needs, reach for `moodycamel::ConcurrentQueue` rather than hand-rolling one.
//
// Capacity must be a power of two so index wraparound is a bitmask instead
// of a modulo.
template <typename T, std::size_t Capacity>
class SpscRingBuffer {
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of two");
  static_assert(std::is_nothrow_move_constructible_v<T> ||
                    std::is_nothrow_copy_constructible_v<T>,
                "T should be cheap/noexcept to move or copy for a queue "
                "meant to run on a hot path");

 public:
  SpscRingBuffer() = default;

  SpscRingBuffer(const SpscRingBuffer&) = delete;
  SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

  // Producer side only. Returns false if the buffer is full (never blocks -
  // blocking on a hot path defeats the point of a lock-free structure).
  bool try_push(const T& value) noexcept(
      std::is_nothrow_copy_constructible_v<T>) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t next =
        (tail + 1) & kMask;  // Equivalent to a division instruction (hence why
                             // Capacity is power of 2)

    // Acquire here pairs with the release the consumer does after it frees
    // a slot, so we see the up-to-date head_ before deciding we're full.
    if (next == head_.load(std::memory_order_acquire)) {
      return false;  // full
    }

    buffer_[tail] = value;
    // Release publishes the write to `buffer_[tail]` before the consumer's
    // acquire-load of tail_ can observe the new tail.
    tail_.store(next, std::memory_order_release);
    return true;
  }

  bool try_push(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t next = (tail + 1) & kMask;

    if (next == head_.load(std::memory_order_acquire)) {
      return false;
    }

    buffer_[tail] = std::move(value);
    tail_.store(next, std::memory_order_release);
    return true;
  }

  // Consumer side only. Returns std::nullopt if the buffer is empty.
  std::optional<T> try_pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
    const std::size_t head = head_.load(std::memory_order_relaxed);

    // Acquire pairs with the producer's release store to tail_, so we see
    // the slot it just wrote.
    if (head == tail_.load(std::memory_order_acquire)) {
      return std::nullopt;  // empty
    }

    T value = std::move(buffer_[head]);
    // Release publishes that the slot is free before the producer's
    // acquire-load of head_ can observe it.
    head_.store((head + 1) & kMask, std::memory_order_release);
    return value;
  }

  // Approximate size - only safe for diagnostics/metrics, not control flow,
  // since head_/tail_ can move between the two loads.
  std::size_t size_approx() const noexcept {
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    const std::size_t head = head_.load(std::memory_order_acquire);
    return (tail - head) & kMask;
  }

  static constexpr std::size_t capacity() noexcept { return Capacity; }

 private:
  static constexpr std::size_t kMask = Capacity - 1;

  alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};
  alignas(kCacheLineSize) T buffer_[Capacity]{};
};

}  // namespace lob
