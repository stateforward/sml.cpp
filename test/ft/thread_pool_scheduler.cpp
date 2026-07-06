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
#include <thread>
#include <utility>

#if BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_ENABLED
namespace policy = boost::sml::utility::policy;

using pool_t = policy::thread_pool_scheduler<8>;

// A non-noexcept callable: the scheduled wait/schedule paths must stay
// unconditionally noexcept (their worker thunk cannot propagate), while the
// pure-inline try_run_immediate stays conditionally noexcept and may propagate.
struct may_throw_callable {
  void operator()() const {}
};
static_assert(noexcept(std::declval<pool_t&>().run_or_schedule_and_wait(may_throw_callable{})),
              "run_or_schedule_and_wait must be unconditionally noexcept");
static_assert(noexcept(std::declval<pool_t&>().schedule(may_throw_callable{})), "schedule must be unconditionally noexcept");
static_assert(!noexcept(std::declval<pool_t&>().try_run_immediate(may_throw_callable{})),
              "try_run_immediate stays conditionally noexcept (inline path propagates)");

test thread_pool_scheduler_runs_task_inline_when_idle = [] {
  pool_t scheduler{};
  int calls = 0;
  const auto caller = std::this_thread::get_id();
  std::thread::id ran_on{};
  const bool ran = scheduler.run_or_schedule_and_wait([&] {
    ++calls;
    ran_on = std::this_thread::get_id();
  });
  expect(ran);
  expect(1 == calls);
  // An idle pool runs the work on the calling thread rather than a worker.
  expect(caller == ran_on);
};

test thread_pool_scheduler_fork_join_runs_every_lane = [] {
  pool_t scheduler{};
  std::atomic<int> calls{0};
  pool_t::join_group group{};
  for (std::size_t lane = 0; lane < 8u; ++lane) {
    scheduler.try_submit(group, [&calls] { calls.fetch_add(1, std::memory_order_relaxed); });
  }
  const bool accepted = group.wait();
  expect(accepted);
  expect(8 == calls.load(std::memory_order_acquire));
};

// A stack-reused join_group must start each round clean: a rejection in one round
// must not make a later all-success round report failure. (try_submit from inside
// a worker is rejected, since a worker cannot fork into its own pool.)
test thread_pool_scheduler_reused_join_group_clears_prior_rejection = [] {
  pool_t scheduler{};
  pool_t::join_group group{};

  // Round 1: drive a worker that tries to submit into `group` -> rejected.
  std::atomic<bool> rejected_on_worker{false};
  pool_t::join_group driver{};
  scheduler.try_submit(driver,
                       [&] { rejected_on_worker.store(!scheduler.try_submit(group, [] {}), std::memory_order_release); });
  driver.wait();
  expect(rejected_on_worker.load(std::memory_order_acquire));
  expect(!group.wait());  // the rejection is observed once...

  // Round 2: same group, all lanes succeed -> must report accepted again.
  std::atomic<int> calls{0};
  for (std::size_t lane = 0; lane < 4u; ++lane) {
    scheduler.try_submit(group, [&calls] { calls.fetch_add(1, std::memory_order_relaxed); });
  }
  expect(group.wait());  // ...and must not stick to false on the next round
  expect(4 == calls.load(std::memory_order_acquire));
};

// Regression guard for the join-latch deadlock: under rapid, repeated fork/join
// with lane_count == worker_count, a Dekker-race close/complete handshake plus a
// destroy-during-release semaphore could strand a wakeup. The lifetime-safe
// spin-join must complete every round without hanging. (Reproduced reliably with
// thousands of rounds; kept here as a standing guard.)
test thread_pool_scheduler_fork_join_survives_rapid_repeated_rounds = [] {
  constexpr std::size_t lanes = 8u;
  constexpr int rounds = 20000;
  pool_t scheduler{};
  std::atomic<long> calls{0};
  for (int round = 0; round < rounds; ++round) {
    pool_t::join_group group{};
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
