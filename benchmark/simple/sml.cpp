//
// Copyright (c) 2016-2020 Kris Jusiak (kris at jusiak dot net)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#include "benchmark.hpp"
#include "sml_player_sm.hpp"

int main() {
  sml::sm<player> sm;

  benchmark_execution_speed([&] { run_player_one_million(sm); });
  benchmark_memory_usage(sm);
}
