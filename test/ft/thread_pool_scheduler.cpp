//
// Copyright (c) 2026 stateforward
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#include <atomic>
#include <cstdlib>
#define BOOST_SML_THREAD_POOL_SCHEDULER_TEST_HOOKS 1
#include <boost/sml/utility/thread_pool_scheduler.hpp>
#include <cstddef>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>
#if !BOOST_SML_DISABLE_EXCEPTIONS
#include <stdexcept>
#endif
namespace {
std::atomic<bool> count_allocations{false};
std::atomic<std::size_t> allocation_count{0u};
}

void* operator new(const std::size_t size) {
  if (count_allocations.load(std::memory_order_relaxed)) {
    allocation_count.fetch_add(1u, std::memory_order_relaxed);
  }
  if (void* const ptr = std::malloc(size)) {
    return ptr;
  }
  std::abort();
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

class allocation_scope {
 public:
  allocation_scope() noexcept {
    allocation_count.store(0u, std::memory_order_relaxed);
    count_allocations.store(true, std::memory_order_release);
  }
  ~allocation_scope() { count_allocations.store(false, std::memory_order_release); }
  std::size_t count() const noexcept { return allocation_count.load(std::memory_order_acquire); }

  allocation_scope(const allocation_scope&) = delete;
  allocation_scope& operator=(const allocation_scope&) = delete;
};


#if BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_ENABLED
namespace policy = boost::sml::utility::policy;
namespace rollback_probe {
std::atomic<bool> pause{false};
std::atomic<bool> reserved{false};
std::atomic<bool> release{false};

void hook(const std::size_t claimed, const std::size_t requested) noexcept {
  if (!pause.load(std::memory_order_acquire) || claimed == 0u || claimed == requested) {
    return;
  }
  reserved.store(true, std::memory_order_release);
  while (!release.load(std::memory_order_acquire)) {
    policy::cpu_relax();
  }
}
}  // namespace rollback_probe


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
using budget_pool_t = policy::thread_pool_scheduler<4, 16, 128>;
static_assert(noexcept(budget_pool_t::try_worker_budget(1u)), "worker budget validation must not throw");
static_assert(std::is_nothrow_invocable_v<decltype([]() noexcept {})&>, "test tasks must satisfy batch contract");
budget_pool_t::worker_budget validated_budget(const std::size_t requested) noexcept {
  const auto budget = budget_pool_t::try_worker_budget(requested);
  expect(budget);
  return budget;
}



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
test thread_pool_scheduler_default_uses_static_worker_count = [] {
  budget_pool_t scheduler{};
  expect(budget_pool_t::static_worker_count == scheduler.active_worker_count());
};

test thread_pool_scheduler_runtime_worker_budget_is_validated = [] {
  const auto invalid_zero = budget_pool_t::try_worker_budget(0u);
  const auto invalid_large = budget_pool_t::try_worker_budget(budget_pool_t::static_worker_count + 1u);
  const auto valid = budget_pool_t::try_worker_budget(2u);
  expect(!invalid_zero);
  expect(!invalid_large);
  expect(valid);
  budget_pool_t scheduler{valid};
  expect(2u == scheduler.active_worker_count());
};

#if !BOOST_SML_DISABLE_EXCEPTIONS
test thread_pool_scheduler_runtime_worker_budget_constructor_rejects_invalid_values = [] {
  bool zero_rejected = false;
  bool large_rejected = false;
  try {
    budget_pool_t scheduler{0u};
  } catch (const std::invalid_argument&) {
    zero_rejected = true;
  }
  try {
    budget_pool_t scheduler{budget_pool_t::static_worker_count + 1u};
  } catch (const std::invalid_argument&) {
    large_rejected = true;
  }
  expect(zero_rejected);
  expect(large_rejected);
};
#endif

test thread_pool_scheduler_batch_uses_distinct_active_workers = [] {
  budget_pool_t scheduler{validated_budget(2u)};
  budget_pool_t::join_group group{};
  std::atomic<int> entered{0};
  std::atomic<bool> release{false};

  const auto submitted = scheduler.try_submit_batch(
      group,
      [&]() noexcept {
        entered.fetch_add(1, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) {
          policy::cpu_relax();
        }
      },
      [&]() noexcept {
        entered.fetch_add(1, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) {
          policy::cpu_relax();
        }
      });

  expect(2u == submitted);
  while (entered.load(std::memory_order_acquire) != 2) {
    policy::cpu_relax();
  }
  release.store(true, std::memory_order_release);
  expect(group.wait());
};

test thread_pool_scheduler_batch_rejects_all_when_workers_are_not_distinctly_idle = [] {
  budget_pool_t scheduler{validated_budget(2u)};
  budget_pool_t::join_group occupied{};
  std::atomic<bool> occupied_entered{false};
  std::atomic<bool> release_occupied{false};
  expect(scheduler.try_submit_batch(occupied, [&]() noexcept {
    occupied_entered.store(true, std::memory_order_release);
    while (!release_occupied.load(std::memory_order_acquire)) {
      policy::cpu_relax();
    }
  }) == 1u);
  while (!occupied_entered.load(std::memory_order_acquire)) {
    policy::cpu_relax();
  }

  budget_pool_t::join_group rejected{};
  std::atomic<int> rejected_calls{0};
  const auto rejected_count = scheduler.try_submit_batch(
      rejected, [&]() noexcept { rejected_calls.fetch_add(1, std::memory_order_relaxed); },
      [&]() noexcept { rejected_calls.fetch_add(1, std::memory_order_relaxed); });
  expect(0u == rejected_count);
  expect(!rejected.wait());
  expect(0 == rejected_calls.load(std::memory_order_acquire));

  release_occupied.store(true, std::memory_order_release);
  expect(occupied.wait());

  std::atomic<int> reused_calls{0};
  const auto reused_count = scheduler.try_submit_batch(
      rejected, [&]() noexcept { reused_calls.fetch_add(1, std::memory_order_relaxed); },
      [&]() noexcept { reused_calls.fetch_add(1, std::memory_order_relaxed); });
  expect(2u == reused_count);
  expect(rejected.wait());
  expect(2 == reused_calls.load(std::memory_order_acquire));
};

test thread_pool_scheduler_batch_rejects_same_pool_worker_reentry = [] {
  budget_pool_t scheduler{validated_budget(1u)};
  budget_pool_t::join_group outer{};
  std::atomic<std::size_t> nested_count{1u};
  std::atomic<bool> nested_wait{true};
  std::atomic<bool> nested_ran{false};

  expect(scheduler.try_submit_batch(outer, [&]() noexcept {
    budget_pool_t::join_group nested{};
    nested_count.store(
        scheduler.try_submit_batch(nested,
                                   [&]() noexcept { nested_ran.store(true, std::memory_order_release); }),
        std::memory_order_release);
    nested_wait.store(nested.wait(), std::memory_order_release);
  }) == 1u);
  expect(outer.wait());
  expect(0u == nested_count.load(std::memory_order_acquire));
  expect(!nested_wait.load(std::memory_order_acquire));
  expect(!nested_ran.load(std::memory_order_acquire));
};

test thread_pool_scheduler_batch_moves_only_after_full_reservation = [] {
  struct move_probe {
    int* moves;
    std::atomic<int>* calls;
    move_probe(int& moves_in, std::atomic<int>& calls_in) noexcept : moves(&moves_in), calls(&calls_in) {}
    move_probe(move_probe&& other) noexcept : moves(other.moves), calls(other.calls) { ++*moves; }
    move_probe(const move_probe&) = delete;
    void operator()() noexcept { calls->fetch_add(1, std::memory_order_relaxed); }
  };

  budget_pool_t scheduler{validated_budget(1u)};
  budget_pool_t::join_group group{};
  int moves = 0;
  std::atomic<int> calls{0};
  move_probe first{moves, calls};
  move_probe second{moves, calls};
  expect(0u == scheduler.try_submit_batch(group, std::move(first), std::move(second)));
  expect(!group.wait());
  expect(0 == moves);
  expect(0 == calls.load(std::memory_order_acquire));
};

test thread_pool_scheduler_batch_wait_returns_after_slots_are_reusable = [] {
  using single_pool_t = policy::thread_pool_scheduler<1, 16, 128>;
  single_pool_t scheduler{};
  std::atomic<int> calls{0};
  for (int round = 0; round < 20000; ++round) {
    single_pool_t::join_group group{};
    expect(1u == scheduler.try_submit_batch(
                       group, [&]() noexcept { calls.fetch_add(1, std::memory_order_relaxed); }));
    expect(group.wait());
  }
  expect(20000 == calls.load(std::memory_order_acquire));
};
test thread_pool_scheduler_single_task_api_interoperates_with_batch_api = [] {
  budget_pool_t scheduler{validated_budget(2u)};
  budget_pool_t::join_group queued{};
  budget_pool_t::join_group batch{};
  std::atomic<int> queued_calls{0};
  std::atomic<int> batch_calls{0};

  expect(scheduler.try_submit(queued, [&]() noexcept { queued_calls.fetch_add(1, std::memory_order_relaxed); }));
  expect(queued.wait());
  expect(2u == scheduler.try_submit_batch(
                   batch, [&]() noexcept { batch_calls.fetch_add(1, std::memory_order_relaxed); },
                   [&]() noexcept { batch_calls.fetch_add(1, std::memory_order_relaxed); }));
  expect(batch.wait());
  expect(1 == queued_calls.load(std::memory_order_acquire));
  expect(2 == batch_calls.load(std::memory_order_acquire));
};


test thread_pool_scheduler_batch_dispatch_does_not_allocate = [] {
  budget_pool_t scheduler{validated_budget(2u)};
  budget_pool_t::join_group group{};
  std::atomic<int> calls{0};
  std::size_t allocations = 0u;
  {
    allocation_scope scope{};
    expect(2u == scheduler.try_submit_batch(
                     group, [&]() noexcept { calls.fetch_add(1, std::memory_order_relaxed); },
                     [&]() noexcept { calls.fetch_add(1, std::memory_order_relaxed); }));
    expect(group.wait());
    allocations = scope.count();
  }
  expect(0u == allocations);
  expect(2 == calls.load(std::memory_order_acquire));
};

test thread_pool_scheduler_queue_wake_survives_batch_reservation_rollback = [] {
  using rollback_pool_t = policy::thread_pool_scheduler<2, 16, 128>;
  rollback_pool_t scheduler{};
  rollback_pool_t::join_group occupied{};
  std::atomic<bool> occupied_entered{false};
  std::atomic<bool> release_occupied{false};
  expect(1u == scheduler.try_submit_batch(occupied, [&]() noexcept {
    occupied_entered.store(true, std::memory_order_release);
    while (!release_occupied.load(std::memory_order_acquire)) {
      policy::cpu_relax();
    }
  }));
  while (!occupied_entered.load(std::memory_order_acquire)) {
    policy::cpu_relax();
  }

  rollback_probe::pause.store(true, std::memory_order_release);
  rollback_probe::reserved.store(false, std::memory_order_release);
  rollback_probe::release.store(false, std::memory_order_release);
  rollback_pool_t::test_batch_reservation_hook = rollback_probe::hook;

  rollback_pool_t::join_group rejected{};
  std::atomic<std::size_t> rejected_count{2u};
  std::thread submit_batch([&]() {
    rejected_count.store(
        scheduler.try_submit_batch(rejected, []() noexcept {}, []() noexcept {}), std::memory_order_release);
  });
  while (!rollback_probe::reserved.load(std::memory_order_acquire)) {
    policy::cpu_relax();
  }

  std::atomic<bool> queue_ran{false};
  expect(scheduler.try_submit([&]() noexcept { queue_ran.store(true, std::memory_order_release); }));
  rollback_probe::release.store(true, std::memory_order_release);
  submit_batch.join();
  rollback_pool_t::test_batch_reservation_hook = nullptr;
  rollback_probe::pause.store(false, std::memory_order_release);

  expect(0u == rejected_count.load(std::memory_order_acquire));
  expect(!rejected.wait());
  while (!queue_ran.load(std::memory_order_acquire)) {
    policy::cpu_relax();
  }
  release_occupied.store(true, std::memory_order_release);
  expect(occupied.wait());
};

#else
test thread_pool_scheduler_disabled_without_cxx20_and_semaphore = [] { expect(true); };
#endif
