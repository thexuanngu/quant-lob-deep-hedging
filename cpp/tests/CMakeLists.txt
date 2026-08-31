find_package(Catch2 3 REQUIRED)

add_executable(unit_tests test_spsc_ring_buffer.cpp)
target_link_libraries(unit_tests PRIVATE lob_core Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(unit_tests)
