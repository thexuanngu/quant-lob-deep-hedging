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
