# C++ Engine

## Build

```bash
cmake --preset release      # or debug / asan / tsan
cmake --build --preset release -j$(nproc)
ctest --preset release --output-on-failure
```

Run under sanitizers before you trust any change to `spsc_ring_buffer.hpp`
or anything else touching atomics:

```bash
cmake --preset asan && cmake --build --preset asan -j$(nproc) && ctest --preset asan
cmake --preset tsan && cmake --build --preset tsan -j$(nproc) && ctest --preset tsan
```

Use the Python bridge from the build directory:

```bash
cd build && python3 -c "import lob_engine; rb = lob_engine.RingBufferU64(); print(rb.try_push(1))"
```

## Layout

| Path | Purpose |
|---|---|
| `include/lob/` | Public headers. Header-only for now (`SpscRingBuffer`); add matching `.cpp` files under `src/` once book-building logic needs non-template translation units. |
| `src/` | Non-template implementation files (currently empty). |
| `bindings/` | `pybind11` module — translation layer only. No book-building or model logic belongs here. |
| `tests/` | Catch2 unit + concurrency tests, run via `ctest`. |
| `benchmarks/` | Google Benchmark micro-benchmarks for hot-path regressions. |

## Dependencies

All via `apt` (see `docker/Dockerfile` for the exact list) — `cmake`,
`ninja-build`, `catch2`, `libbenchmark-dev`, `python3-dev` — plus `pybind11`
via `pip install pybind11`. No vcpkg/Conan: the Docker image is the
reproducibility layer, so a second package manager on top would be
redundant.

---

### `cpp/include/lob/spsc_ring_buffer.hpp` -> The actual engine code
- Lock-free Single Producer, Single Consumer (SPSC) queue.
    - Not Multi Producer, Multi Consumer because a real feed-handler is naturally SPSC per venue link (prevents consumers observed an updated `tail_` before observing the data written to that slot)—one socket/kernel-bypass thread produces, one strategy thread consumes.

### `cpp/tests/test_spsc_ring_buffer.cpp` -> Catch2 tests

### `cpp/benchmarks/bench_spsc_ring_buffer.cpp` -> Google Benchmark

### `cpp/bindings/py_lob.cpp` => `pybind11` bridge
- Every `.def(...)` exposes one C++ method to build
    - After building, import lob_engine gives a `RingBufferU64` class that's a thin wrapper around the real C++ object — no copying into Python, calls go straight into C++ memory
    - **Rule as this grows**: keep this file a thin translation layer; real logic goes in `cpp/src/`.