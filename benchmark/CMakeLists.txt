#
# Copyright (c) 2016-2020 Jean Davy
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
project(benchmark LANGUAGES CXX)

include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.2
)
FetchContent_MakeAvailable(googlebenchmark)

include_directories(AFTER
  ${CMAKE_CURRENT_SOURCE_DIR}
  header)

add_definitions(
  -DCHECK_COMPILE_TIME
  -DBOOST_ALL_NO_LIB
  -DBUILD_SHARED_LIBS=OFF)

if (IS_COMPILER_GCC_LIKE)
  add_compile_options(
    "-Wno-unused-local-typedefs"
    "-Wno-unused-parameter")
endif ()

add_subdirectory(complex)
add_subdirectory(composite)
add_subdirectory(header)
add_subdirectory(simple)
