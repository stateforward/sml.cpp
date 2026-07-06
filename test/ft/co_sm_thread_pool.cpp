//
// Copyright (c) 2026 stateforward
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#include <atomic>
#include <boost/sml.hpp>
#include <boost/sml/utility/co_sm.hpp>
#include <boost/sml/utility/thread_pool_scheduler.hpp>
#include <chrono>
#include <cstddef>
#include <new>
#include <thread>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace sml = boost::sml;

#if BOOST_SML_UTILITY_CO_SM_ENABLED && BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_ENABLED
namespace utility = sml::utility;
namespace policy = utility::policy;

using scheduler_t = policy::thread_pool_scheduler<1, 64, 128>;

static_assert(policy::async_coroutine_scheduler_contract<scheduler_t>,
              "thread_pool_scheduler is the async coroutine scheduler policy");
static_assert(policy::valid_coroutine_scheduler<scheduler_t>, "thread_pool_scheduler provides schedule(fn)");

namespace {

struct e1 {};
const auto idle = sml::state<class idle>;
const auto s1 = sml::state<class s1>;

template <class predicate>
bool wait_until(predicate&& done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!done()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::yield();
  }
  return true;
}

void block_worker(scheduler_t& scheduler, std::atomic<bool>& entered, std::atomic<bool>& release) {
  scheduler.schedule([&entered, &release]() noexcept {
    entered.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) {
      policy::cpu_relax();
    }
  });
  expect(wait_until([&entered] { return entered.load(std::memory_order_acquire); }));
}

struct simple_context {
  std::atomic<int>* calls = nullptr;
};

struct simple_machine {
  auto operator()() const {
    using namespace sml;
    const auto record = [](simple_context& context) { context.calls->fetch_add(1, std::memory_order_acq_rel); };
    // clang-format off
    return make_transition_table(
      *idle + event<e1> / record = s1,
       s1 + event<e1> / record = s1
    );
    // clang-format on
  }
};

using simple_sm = utility::co_sm<simple_machine, policy::coroutine_scheduler<scheduler_t>,
                                 policy::coroutine_allocator<policy::pooled_coroutine_allocator<4096, 8>>, simple_context>;

utility::bool_task await_task(utility::bool_task& task, std::atomic<bool>& resumed, bool& accepted) {
  accepted = co_await task;
  resumed.store(true, std::memory_order_release);
  co_return accepted;
}

utility::bool_task await_then_dispatch_again(simple_sm& sm, utility::bool_task& task, std::atomic<bool>& resumed,
                                             bool& first_accepted, bool& second_accepted) {
  first_accepted = co_await task;
  auto second = sm.process_event_async(e1{});
  second_accepted = co_await second;
  resumed.store(true, std::memory_order_release);
  co_return first_accepted&& second_accepted;
}

struct fixed_allocator {
  std::size_t allocate_calls = 0;
  std::size_t deallocate_calls = 0;
  bool heap_used = false;
  bool used = false;
  alignas(std::max_align_t) unsigned char storage[4096]{};

  void* allocate(const std::size_t size, const std::size_t alignment) {
    ++allocate_calls;
    if (used || size > sizeof(storage) || alignment > alignof(std::max_align_t)) {
      heap_used = true;
      return nullptr;
    }
    used = true;
    return storage;
  }

  void deallocate(void* ptr, std::size_t, std::size_t) noexcept {
    ++deallocate_calls;
    expect(ptr == storage);
    used = false;
  }
};

using fixed_alloc_sm = utility::co_sm<simple_machine, policy::coroutine_scheduler<scheduler_t>,
                                      policy::coroutine_allocator<fixed_allocator>, simple_context>;

struct blocking_context {
  std::atomic<bool>* entered = nullptr;
  std::atomic<bool>* release = nullptr;
};

struct blocking_machine {
  auto operator()() const {
    using namespace sml;
    const auto block = [](blocking_context& context) {
      context.entered->store(true, std::memory_order_release);
      while (!context.release->load(std::memory_order_acquire)) {
        policy::cpu_relax();
      }
    };
    // clang-format off
    return make_transition_table(
      *idle + event<e1> / block = s1
    );
    // clang-format on
  }
};

using blocking_sm = utility::co_sm<blocking_machine, policy::coroutine_scheduler<scheduler_t>,
                                   policy::coroutine_allocator<policy::pooled_coroutine_allocator<4096, 8>>, blocking_context>;

#if !defined(_WIN32)
void destroy_incomplete_task_child() {
  std::atomic<int> calls{0};
  simple_context context{&calls};
  simple_sm sm{context};
  std::atomic<bool> worker_entered{false};
  std::atomic<bool> release_worker{false};
  block_worker(sm.scheduler(), worker_entered, release_worker);

  auto task = sm.process_event_async(e1{});
  expect(!task.await_ready());
}
#endif

}  // namespace

test thread_pool_co_sm_process_event_async_starts_now_and_awaits_later = [] {
  std::atomic<int> calls{0};
  simple_context context{&calls};
  simple_sm sm{context};
  std::atomic<bool> worker_entered{false};
  std::atomic<bool> release_worker{false};
  block_worker(sm.scheduler(), worker_entered, release_worker);

  auto task = sm.process_event_async(e1{});
  expect(!task.await_ready());

  int local_work = 0;
  for (int i = 0; i < 7; ++i) {
    ++local_work;
  }
  expect(7 == local_work);
  expect(0 == calls.load(std::memory_order_acquire));

  release_worker.store(true, std::memory_order_release);
  expect(wait_until([&task] { return task.await_ready(); }));
  expect(task.result());
  expect(1 == calls.load(std::memory_order_acquire));
  expect(sm.is(s1));
};

test thread_pool_co_sm_await_resumes_after_worker_completion = [] {
  std::atomic<int> calls{0};
  simple_context context{&calls};
  simple_sm sm{context};
  std::atomic<bool> worker_entered{false};
  std::atomic<bool> release_worker{false};
  block_worker(sm.scheduler(), worker_entered, release_worker);

  auto task = sm.process_event_async(e1{});
  std::atomic<bool> resumed{false};
  bool accepted = false;
  auto waiter = await_task(task, resumed, accepted);

  expect(!resumed.load(std::memory_order_acquire));
  expect(!waiter.await_ready());
  release_worker.store(true, std::memory_order_release);
  expect(wait_until([&waiter] { return waiter.await_ready(); }));
  expect(resumed.load(std::memory_order_acquire));
  expect(1 == calls.load(std::memory_order_acquire));
  expect(waiter.result());
  expect(accepted);
  expect(task.result());
};

test thread_pool_co_sm_await_can_dispatch_same_actor_again = [] {
  std::atomic<int> calls{0};
  simple_context context{&calls};
  simple_sm sm{context};
  std::atomic<bool> worker_entered{false};
  std::atomic<bool> release_worker{false};
  block_worker(sm.scheduler(), worker_entered, release_worker);

  auto task = sm.process_event_async(e1{});
  std::atomic<bool> resumed{false};
  bool first_accepted = false;
  bool second_accepted = false;
  auto waiter = await_then_dispatch_again(sm, task, resumed, first_accepted, second_accepted);

  expect(!resumed.load(std::memory_order_acquire));
  release_worker.store(true, std::memory_order_release);
  expect(wait_until([&waiter] { return waiter.await_ready(); }));
  expect(resumed.load(std::memory_order_acquire));
  expect(waiter.result());
  expect(first_accepted);
  expect(second_accepted);
  expect(2 == calls.load(std::memory_order_acquire));
};

test thread_pool_co_sm_incomplete_task_destruction_terminates = [] {
#if !defined(_WIN32)
  const pid_t pid = fork();
  expect(pid >= 0);
  if (pid == 0) {
    destroy_incomplete_task_child();
    _exit(0);
  }

  int status = 0;
  expect(waitpid(pid, &status, 0) == pid);
  expect(WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0));
#else
  expect(true);
#endif
};

test thread_pool_co_sm_fixed_allocator_avoids_heap_fallback = [] {
  std::atomic<int> calls{0};
  simple_context context{&calls};
  fixed_alloc_sm sm{context};

  {
    auto task = sm.process_event_async(e1{});
    expect(wait_until([&task] { return task.await_ready(); }));
    expect(task.result());
  }

  expect(1u == sm.allocator().allocate_calls);
  expect(1u == sm.allocator().deallocate_calls);
  expect(!sm.allocator().heap_used);
  expect(1 == calls.load(std::memory_order_acquire));
};

test thread_pool_co_sm_rejects_concurrent_dispatch_to_same_actor = [] {
  std::atomic<int> calls{0};
  simple_context context{&calls};
  simple_sm sm{context};
  std::atomic<bool> worker_entered{false};
  std::atomic<bool> release_worker{false};
  block_worker(sm.scheduler(), worker_entered, release_worker);

  auto first = sm.process_event_async(e1{});
  auto second = sm.process_event_async(e1{});

  expect(!first.await_ready());
  expect(second.await_ready());
  expect(!second.result());
  release_worker.store(true, std::memory_order_release);
  expect(wait_until([&first] { return first.await_ready(); }));
  expect(first.result());
  expect(1 == calls.load(std::memory_order_acquire));
};

test thread_pool_co_sm_different_actors_run_concurrently = [] {
  std::atomic<bool> entered1{false};
  std::atomic<bool> entered2{false};
  std::atomic<bool> release{false};
  blocking_context context1{&entered1, &release};
  blocking_context context2{&entered2, &release};
  blocking_sm sm1{context1};
  blocking_sm sm2{context2};

  auto task1 = sm1.process_event_async(e1{});
  auto task2 = sm2.process_event_async(e1{});

  expect(wait_until([&] { return entered1.load(std::memory_order_acquire) && entered2.load(std::memory_order_acquire); }));
  expect(!task1.await_ready());
  expect(!task2.await_ready());

  release.store(true, std::memory_order_release);
  expect(wait_until([&] { return task1.await_ready() && task2.await_ready(); }));
  expect(task1.result());
  expect(task2.result());
  expect(sm1.is(s1));
  expect(sm2.is(s1));
};

#else
test thread_pool_co_sm_disabled_without_cxx20_coroutines_and_semaphore = [] { expect(true); };
#endif
