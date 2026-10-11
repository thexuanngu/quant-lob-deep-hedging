#pragma once

#include "lob/spsc_ring_buffer.hpp"
#include "lob/types.hpp"

namespace lob {

using OutputBuffer = SpscRingBuffer<ExecutionEvent, 4096>;
using InputBuffer = SpscRingBuffer<OrderEvent, 4096>;

}  // namespace lob