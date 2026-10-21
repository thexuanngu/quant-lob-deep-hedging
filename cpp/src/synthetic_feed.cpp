
#include "lob/synthetic_feed.hpp"

namespace lob {

SyntheticFeed::SyntheticFeed(uint64_t seed, SyntheticFeedParams params,
                             int64_t min_px, int64_t max_px)
    : params_(params),
      min_px_(min_px),  // book: OrderBook(kLevels, kOff, ...);   feed: min_px =
                        // kOff, max_px = kOff + kLevels - 1
      max_px_(max_px) {
  // TODO(Me): I could add more asserts into the params (like non-negative) - not rn
  if (!(min_px <= params.ref_px && params.ref_px <= max_px))
    throw new std::invalid_argument("ref_px must be between min and max price");
  if (params.event_mix.size() != 3)
    throw new std::invalid_argument("event mix must have exactly 3 entries");

  rng_.seed(seed);
  issued_ids_.reserve(params.max_events);
  ev_dist_ = std::discrete_distribution<>(params.event_mix.begin(),
                                          params.event_mix.end());
  time_gen_ = std::exponential_distribution<double>(params.mean_interarrival_gap);
  offset_dist_ = std::geometric_distribution<>(params.offset_p);
}

int64_t SyntheticFeed::draw_price(Side side) {
  std::uniform_real_distribution<> marketable_gen(0., 1.);
  if (side == Side::Bid) {
    return (marketable_gen(rng_) < params_.crossing_fraction)
               ? params_.ref_px + offset_dist_(rng_)  // bid crosses
               : params_.ref_px - (1 + offset_dist_(rng_));
  } else {
    return (marketable_gen(rng_) < params_.crossing_fraction)
               ? params_.ref_px - offset_dist_(rng_)  // ask crosses
               : params_.ref_px + (1 + offset_dist_(rng_));
  }
}

std::optional<OrderEvent> SyntheticFeed::next() {
  // end of stream -> return null
  if (event_counter_ >= params_.max_events) {
    return std::nullopt;
  }

  // update time (id is updated when adding)
  current_time_ += static_cast<uint64_t>(time_gen_(rng_));

  // increment events
  event_counter_++;

  OrderEvent ev{};
  auto ev_fraction = ev_dist_(rng_);
  if (ev_fraction == 0 || issued_ids_.empty()) {  // first fraction is add
    ev = generate_add();
  } else {
    // pick id
    std::uniform_int_distribution<size_t> id_dist(0, issued_ids_.size() - 1);
    const size_t k = id_dist(rng_);
    const uint64_t id = issued_ids_[k];

    // cancel or modify
    if (ev_fraction == 1) {
      // swap and pop
      issued_ids_[k] = issued_ids_.back();
      issued_ids_.pop_back();
      ev = generate_cancel(id);
    } else {
      ev = generate_modify(id);
    }
  }
  return ev;
}

OrderEvent SyntheticFeed::generate_cancel(uint64_t id) {
  auto ev = OrderEvent();
  ev.action = Action::Cancel;
  ev.ts_ns = current_time_;
  ev.order_id = id;
  ev.price = 0;         // price doesn't matter
  ev.qty = 0;           // qty doesn't matter
  ev.side = Side::Ask;  // side doesn't matter

  return ev;
}

// We won't care about the actual side
OrderEvent SyntheticFeed::generate_modify(uint64_t id) {
  // generate new qty
  std::uniform_int_distribution<uint64_t> qty_gen(1, params_.max_quantity);
  uint64_t qty = qty_gen(rng_);

  // attempt to generate new price
  std::uniform_int_distribution<> even_odd(0, 1);
  int64_t px;
  for (int attempt = 0; attempt <= 1000; ++attempt) {
    px = (even_odd(rng_)) ? (params_.ref_px + offset_dist_(rng_))
                          : (params_.ref_px - offset_dist_(rng_));

    if (px >= min_px_ && px <= max_px_) break;
    if (attempt == 1000)
      throw std::runtime_error("SyntheticFeed: price window unreachable");
  }

  // build the result
  auto ev = OrderEvent();
  ev.action = Action::Modify;
  ev.order_id = id;
  ev.ts_ns = current_time_;
  ev.side = Side::Ask;
  ev.price = px;
  ev.qty = qty;
  return ev;
}

OrderEvent SyntheticFeed::generate_add() {
  // generate qty
  std::uniform_int_distribution<uint64_t> qty_gen(1, params_.max_quantity);
  uint64_t qty = qty_gen(rng_);

  // generate a side
  std::uniform_int_distribution<> even_odd(0, 1);
  Side side = (even_odd(rng_) == 1) ? Side::Bid : Side::Ask;

  // attempt to generate price (remebering the marketable fraction)
  int64_t px;
  for (int attempt = 0; attempt <= 1000; ++attempt) {
    px = draw_price(side);
    if (px >= min_px_ && px <= max_px_) break;
    if (attempt == 1000)
      throw std::runtime_error("SyntheticFeed: price window unreachable");
  }

  // push new id back
  issued_ids_.push_back(idCounter_);

  // build result
  auto ev = OrderEvent();
  ev.action = Action::Add;
  ev.order_id = idCounter_++;
  ev.ts_ns = current_time_;
  ev.side = side;
  ev.price = px;
  ev.qty = qty;

  return ev;
}

}  // namespace lob
