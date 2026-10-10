#pragma once

#include <vector>

#include "lob/engine.hpp"
#include "lob/order_book.hpp"

namespace lob {
// TODO(ME): This feed can begin as synthetic, before I move onto a 'REAL' feed
class LOBFeed {
 public:
  LOBFeed(std::vector<uint64_t> order_tracker, std::vector<OrderEvent>)
      : order_tracker_(order_tracker) {}

 private:
  std::vector<uint64_t> order_tracker_;
};
}  // namespace lob
