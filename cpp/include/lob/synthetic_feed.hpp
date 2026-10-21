#pragma once

#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "lob/types.hpp"

namespace lob {

struct SyntheticFeedParams {
  std::int64_t ref_px = 500;
  std::vector<double> event_mix = {0.5, 0.3, 0.2};
  double crossing_fraction = 0.1;
  double offset_p = 0.3;
  std::uint64_t max_quantity = 20;
  double mean_interarrival_gap = 1000.0;  // 1000ns
  std::uint64_t max_events = 100'000;
};
// SyntheticFeedParams p{.ref_px = 500, .max_events = 1'000'000};

class SyntheticFeed {
 public:
  explicit SyntheticFeed(uint64_t seed, SyntheticFeedParams params,
                         int64_t min_px, int64_t max_px);

  // only public function
  [[nodiscard]] std::optional<OrderEvent> next();

 private:
  // the next() helper functions
  OrderEvent generate_cancel(uint64_t id);

  int64_t draw_price(Side side);

  OrderEvent generate_add();
  OrderEvent generate_modify(uint64_t id);

  SyntheticFeedParams params_;
  int64_t min_px_;
  int64_t max_px_;
  std::exponential_distribution<double> time_gen_;
  std::geometric_distribution<> offset_dist_;
  std::vector<uint64_t> issued_ids_;
  std::discrete_distribution<> ev_dist_;
  std::mt19937_64 rng_{0};  // for reproducibility
  std::uint64_t current_time_ = 0;
  uint64_t idCounter_ = 1;
  uint64_t event_counter_ = 0;
  // std::mt19937 rng{std::random_device{}()};  // if we want 'true' random
};
}  // namespace lob