cmake_minimum_required(VERSION 3.22)
project(quant_lob_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clangd / IDE tooling

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Sanitizer build options -------------------------------------------
# Lock-free / concurrent code is exactly where UB and data races hide.
# Run the test suite under these regularly, not just at the end:
#   cmake -S cpp -B build-asan -DLOB_SANITIZE=address,undefined
#   cmake -S cpp -B build-tsan -DLOB_SANITIZE=thread
# GCC/Clang syntax - works identically on Mac (Clang) and WSL/CI (GCC).
# MSVC's sanitizer support (ASan only, no UBSan/TSan, different flag syntax)
# isn't wired up here - not needed now that Windows uses WSL.
option(LOB_SANITIZE "Comma-separated sanitizers, e.g. address,undefined or thread" "")
if(LOB_SANITIZE)
  if(MSVC)
    message(WARNING "LOB_SANITIZE is not supported in this build under MSVC; ignoring. Use the asan/tsan presets via WSL or CI instead.")
  else()
    add_compile_options(-fsanitize=${LOB_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${LOB_SANITIZE})
  endif()
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

# --- Core header-only library target ------------------------------------
add_library(lob_core INTERFACE)
target_include_directories(lob_core INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(lob_core INTERFACE Threads::Threads)

# --- pybind11 module ------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
execute_process(
  COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
  OUTPUT_VARIABLE pybind11_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _pybind11_result
)
if(NOT _pybind11_result EQUAL 0)
  message(FATAL_ERROR
    "Could not locate pybind11's CMake config. Run: pip install pybind11")
endif()
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(lob_engine bindings/py_lob.cpp)
target_link_libraries(lob_engine PRIVATE lob_core)

# --- Tests ------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# --- Benchmarks ---------------------------------------------------------
option(LOB_BUILD_BENCHMARKS "Build Google Benchmark micro-benchmarks" ON)
if(LOB_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()