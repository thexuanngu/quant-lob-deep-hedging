find_package(benchmark REQUIRED)

add_executable(benchmarks 
bench_spsc_ring_buffer.cpp
bench_order_book bench_order_book.cpp)
target_link_libraries(benchmarks PRIVATE lob_core benchmark::benchmark)