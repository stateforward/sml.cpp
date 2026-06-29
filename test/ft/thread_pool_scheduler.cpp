//
// Copyright (c) 2026 stateforward
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#include <atomic>
#include <boost/sml/utility/thread_pool_scheduler.hpp>
#include <cstddef>

#if BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_ENABLED
namespace policy = boost::sml::utility::policy;

using pool_t = policy::thread_pool_scheduler<8>;
using pool_ref_t = policy::thread_pool_scheduler_ref<pool_t>;

test thread_pool_scheduler_runs_task_inline_when_idle = [] {
  pool_t pool{};
  pool_ref_t scheduler{pool};
  int calls = 0;
  const bool ran = scheduler.run_or_schedule_and_wait([&calls] { ++calls; });
  expect(ran);
  expect(1 == calls);
  // An idle pool runs the work on the calling thread rather than a worker.
  expect(1u == scheduler.immediate_run_count());
  expect(0u == scheduler.worker_run_count());
};

test thread_pool_scheduler_fork_join_runs_every_lane = [] {
  pool_t pool{};
  pool_ref_t scheduler{pool};
  std::atomic<int> calls{0};
  pool_ref_t::join_group group{};
  for (std::size_t lane = 0; lane < 8u; ++lane) {
    scheduler.try_submit(group, [&calls] { calls.fetch_add(1, std::memory_order_relaxed); });
  }
  const bool accepted = group.wait();
  expect(accepted);
  expect(8 == calls.load(std::memory_order_acquire));
};

// Regression guard for the join-latch deadlock: under rapid, repeated fork/join
// with lane_count == worker_count, a Dekker-race close/complete handshake plus a
// destroy-during-release semaphore could strand a wakeup. The lifetime-safe
// spin-join must complete every round without hanging. (Reproduced reliably with
// thousands of rounds; kept here as a standing guard.)
test thread_pool_scheduler_ref_fork_join_survives_rapid_repeated_rounds = [] {
  constexpr std::size_t lanes = 8u;
  constexpr int rounds = 20000;
  pool_t pool{};
  pool_ref_t scheduler{pool};
  std::atomic<long> calls{0};
  for (int round = 0; round < rounds; ++round) {
    pool_ref_t::join_group group{};
    for (std::size_t lane = 0; lane < lanes; ++lane) {
      scheduler.try_submit(group, [&calls] { calls.fetch_add(1, std::memory_order_relaxed); });
    }
    expect(group.wait());
  }
  expect(static_cast<long>(lanes) * rounds == calls.load(std::memory_order_acquire));
};

#else
test thread_pool_scheduler_disabled_without_cxx20_and_semaphore = [] { expect(true); };
#endif
