#include <pybind11/pybind11.h>
#include <pybind11/stl.h>  // for std::optional -> Python None conversion

#include <cstdint>

#include "lob/spsc_ring_buffer.hpp"

namespace py = pybind11;

// pybind11 can't bind a class template directly, so we bind one concrete
// instantiation. As the engine grows, this file's job stays narrow: it's a
// thin translation layer, not where LOB logic lives. Keep book-building,
// matching, and market-model logic in cpp/src/ so it stays testable and
// benchmarkable without paying Python's GIL/marshalling overhead in the
// hot path - only cross the C++/Python boundary at message/batch
// granularity (e.g. per L2 snapshot or per decision-layer step), never
// per-tick in a loop.
using RingBufferU64 = lob::SpscRingBuffer<std::uint64_t, 4096>;

PYBIND11_MODULE(lob_engine, m) {
  m.doc() = "Bridge module exposing the C++ LOB engine primitives to Python";

  py::class_<RingBufferU64>(m, "RingBufferU64")
      .def(py::init<>())
      // Bound via a lambda rather than py::overload_cast: GCC 13 hits an
      // internal compiler error resolving overload_cast against the
      // noexcept-qualified try_push overloads here (template argument
      // deduction through a noexcept member-function-pointer type). A
      // lambda sidesteps the overload resolution entirely and is one line.
      .def("try_push",
           [](RingBufferU64& rb, std::uint64_t value) {
             return rb.try_push(value);
           },
           py::arg("value"))
      .def("try_pop", &RingBufferU64::try_pop)
      .def("size_approx", &RingBufferU64::size_approx)
      .def_static("capacity", &RingBufferU64::capacity)
      .def("__repr__", [](const RingBufferU64& rb) {
        return "<RingBufferU64 size_approx=" +
               std::to_string(rb.size_approx()) + " capacity=" +
               std::to_string(rb.capacity()) + ">";
      });
}
