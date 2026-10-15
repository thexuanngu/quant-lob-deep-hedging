
#include <optional>
#include <cstdint>
#include <vector>

#include "lob/types.hpp"


namespace lob {
    class SyntheticFeed {
        public:

        std::optional<OrderEvent> next();

        private:
        std::vector<uint64_t> issued_ids_;
        std::uint_64t current_time_
        
    };
}