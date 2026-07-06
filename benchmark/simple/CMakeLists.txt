#
# Copyright (c) 2016-2020 Jean Davy
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
set(CURRENT_CXX_STANDARD ${CMAKE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD 11)
add_example(simple_euml benchmark_simple_euml euml.cpp)
set(CMAKE_CXX_STANDARD ${CURRENT_CXX_STANDARD})

add_example(simple_sc benchmark_simple_sc sc.cpp)

if (NOT IS_MSVC_2015)
  add_example(simple_co_sm benchmark_simple_co_sm co_sm.cpp)
  add_example(simple_thread_pool_co_sm benchmark_simple_thread_pool_co_sm thread_pool_co_sm.cpp)
  find_package(Threads REQUIRED)
  target_link_libraries(simple_thread_pool_co_sm PRIVATE Threads::Threads)
  if (IS_COMPILER_OPTION_MSVC_LIKE)
    target_compile_options(simple_thread_pool_co_sm PRIVATE /UCHECK_COMPILE_TIME)
  else()
    target_compile_options(simple_thread_pool_co_sm PRIVATE -UCHECK_COMPILE_TIME)
  endif()

  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sml_player_sm.hpp")
    add_example(simple_sml benchmark_simple_sml sml.cpp)

    # Same source as simple_sml; undefine CHECK_COMPILE_TIME so benchmark.hpp measures runtime (pairs with smlang_player).
    add_executable(sml_player sml.cpp)
    target_link_libraries(sml_player PUBLIC sml)
    if (IS_COMPILER_OPTION_MSVC_LIKE)
      target_compile_options(sml_player PRIVATE /UCHECK_COMPILE_TIME)
    else()
      target_compile_options(sml_player PRIVATE -UCHECK_COMPILE_TIME)
    endif()
  endif()

  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sml_gbench.cpp")
    add_executable(sml_player_gbench sml_gbench.cpp)
    target_link_libraries(sml_player_gbench PRIVATE sml benchmark::benchmark)
  endif()
endif()
