//
// Copyright (c) 2016-2026 Kris Jusiak (kris at jusiak dot net)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#include <boost/sml.hpp>
#include <boost/sml/utility/co_sm.hpp>
#include <cstddef>
#include <functional>
#include <new>
#include <stdexcept>
#include <utility>

namespace sml = boost::sml;

#if BOOST_SML_UTILITY_CO_SM_ENABLED
namespace utility = sml::utility;

struct e1 {};
const auto idle = sml::state<class idle>;
const auto s1 = sml::state<class s1>;

struct c {
  auto operator()() const {
    using namespace sml;
    // clang-format off
    return make_transition_table(
      *idle + event<e1> = s1
    );
    // clang-format on
  }
};

test inline_scheduler_runs_immediately = [] {
  utility::policy::inline_scheduler scheduler{};
  int calls = 0;
  scheduler.schedule([&calls] { ++calls; });
  expect(1 == calls);
};

test fifo_scheduler_preserves_fifo_order = [] {
  utility::policy::fifo_scheduler<8, 64> scheduler{};
  int order = 0;
  bool nested_immediate = true;

  const bool immediate = scheduler.try_run_immediate([&] {
    order = (order * 10) + 1;
    scheduler.schedule([&] { order = (order * 10) + 2; });
    nested_immediate = scheduler.try_run_immediate([&] {});
    scheduler.schedule([&] { order = (order * 10) + 3; });
  });

  expect(immediate);
  expect(!nested_immediate);
  expect(123 == order);
};

test fifo_scheduler_schedule_runs_inline_when_idle = [] {
  utility::policy::fifo_scheduler<8, 64> scheduler{};
  int calls = 0;
  scheduler.schedule([&calls] { ++calls; });
  expect(1 == calls);
};

test pooled_allocator_pool_and_heap_paths = [] {
  utility::policy::pooled_coroutine_allocator<64, 2> allocator{};
  void* p1 = allocator.allocate(32, alignof(std::max_align_t));
  void* p2 = allocator.allocate(32, alignof(std::max_align_t));
  void* p3 = allocator.allocate(128, alignof(std::max_align_t));

  expect(nullptr != p1);
  expect(nullptr != p2);
  expect(nullptr != p3);

  allocator.deallocate(p1, 32, alignof(std::max_align_t));
  allocator.deallocate(p2, 32, alignof(std::max_align_t));
  allocator.deallocate(p3, 128, alignof(std::max_align_t));

  void* external_ptr = ::operator new(32, std::align_val_t(alignof(std::max_align_t)));
  allocator.deallocate(external_ptr, 32, alignof(std::max_align_t));

  allocator.deallocate(nullptr, 0, alignof(std::max_align_t));
};

test bool_task_default_and_immediate_paths = [] {
  utility::bool_task empty{};
  expect(empty.await_ready());
  expect(empty.await_suspend(std::noop_coroutine()) == std::noop_coroutine());
  expect(!empty.result());

  auto immediate = utility::bool_task::from_value(true);
  expect(immediate.await_ready());
  expect(immediate.await_resume());
};

test bool_task_move_paths = [] {
  auto src = utility::bool_task::from_value(true);
  utility::bool_task dst = std::move(src);
  expect(dst.result());

  auto src2 = utility::bool_task::from_value(false);
  dst = std::move(src2);
  expect(!dst.result());

  auto as_rvalue = [](utility::bool_task& task) -> utility::bool_task&& { return static_cast<utility::bool_task&&>(task); };
  dst = as_rvalue(dst);
  expect(!dst.result());
};

utility::bool_task ready_bool_task(const bool value) { co_return value; }

test bool_task_move_assignment_releases_existing_coroutine_handle = [] {
  auto has_handle = ready_bool_task(true);
  auto replacement = utility::bool_task::from_value(false);
  has_handle = std::move(replacement);
  expect(!has_handle.result());
};

test bool_task_promise_helpers = [] {
  utility::bool_task::promise_type promise{};
  promise.return_value(true);
  expect(promise.value);
  promise.final_suspend().await_resume();
  utility::bool_task::promise_type::deallocate_frame(nullptr);
};

test co_sm_inline_scheduler_path = [] {
  using sm_t = utility::co_sm<c, utility::policy::coroutine_scheduler<utility::policy::inline_scheduler>>;

  sm_t sm{};
  auto task = sm.process_event_async(e1{});

  expect(task.await_ready());
  expect(task.result());
  expect(sm.is(s1));
};

test co_sm_sync_process_event_and_visitor = [] {
  utility::co_sm<c> sm{};
  expect(sm.is(idle));
  expect(sm.process_event(e1{}));
  expect(sm.is(s1));

  int visits = 0;
  sm.visit_current_states([&visits](auto) { ++visits; });
  expect(1 == visits);
};

struct scheduler_without_try_run_immediate {
  static constexpr bool guarantees_fifo = true;
  static constexpr bool single_consumer = true;
  static constexpr bool run_to_completion = true;

  template <class TFn>
  void schedule(TFn&& fn) {
    std::forward<TFn>(fn)();
  }
};

struct counting_allocator {
  std::size_t allocate_calls = 0;
  std::size_t deallocate_calls = 0;

  void* allocate(const std::size_t size, const std::size_t alignment) {
    ++allocate_calls;
    return ::operator new(size, std::align_val_t(alignment));
  }

  void deallocate(void* ptr, const std::size_t size, const std::size_t alignment) noexcept {
    ++deallocate_calls;
    ::operator delete(ptr, size, std::align_val_t(alignment));
  }
};

test co_sm_coroutine_path_uses_allocator = [] {
  using sm_t = utility::co_sm<c, utility::policy::coroutine_scheduler<scheduler_without_try_run_immediate>,
                              utility::policy::coroutine_allocator<counting_allocator>>;

  sm_t sm{};

  {
    auto task = sm.process_event_async(e1{});
    expect(task.result());
  }

  expect(1u == sm.allocator().allocate_calls);
  expect(1u == sm.allocator().deallocate_calls);
  expect(sm.is(s1));
};

test co_sm_default_fifo_try_run_immediate_path = [] {
  using sm_t = utility::co_sm<c, utility::policy::coroutine_scheduler<utility::policy::fifo_scheduler<8, 64>>,
                              utility::policy::coroutine_allocator<counting_allocator>>;

  sm_t sm{};
  auto task = sm.process_event_async(e1{});

  expect(task.await_ready());
  expect(task.result());
  expect(0u == sm.allocator().allocate_calls);
  expect(0u == sm.allocator().deallocate_calls);
  expect(sm.is(s1));
};

struct deferred_scheduler {
  static constexpr bool guarantees_fifo = true;
  static constexpr bool single_consumer = true;
  static constexpr bool run_to_completion = true;

  std::function<void()> pending{};

  template <class TFn>
  void schedule(TFn&& fn) {
    pending = std::forward<TFn>(fn);
  }

  void run_pending() {
    auto task = std::move(pending);
    pending = {};
    if (task) {
      task();
    }
  }
};

test co_sm_deferred_scheduler_not_done_then_done = [] {
  using sm_t = utility::co_sm<c, utility::policy::coroutine_scheduler<deferred_scheduler>,
                              utility::policy::coroutine_allocator<counting_allocator>>;

  sm_t sm{};
  auto task = sm.process_event_async(e1{});

  expect(!task.await_ready());

#if !BOOST_SML_DISABLE_EXCEPTIONS
  bool got_logic_error = false;
  try {
    (void)task.result();
  } catch (const std::logic_error&) {
    got_logic_error = true;
  }
  expect(got_logic_error);
#endif

  sm.scheduler().run_pending();
  expect(task.result());
  expect(sm.is(s1));
};

utility::bool_task throwing_bool_task() {
  throw std::runtime_error("test");
  co_return true;
}

test bool_task_exception_rethrow = [] {
#if !BOOST_SML_DISABLE_EXCEPTIONS
  auto task = throwing_bool_task();
  bool got_runtime_error = false;
  try {
    (void)task.result();
  } catch (const std::runtime_error&) {
    got_runtime_error = true;
  }
  expect(got_runtime_error);
#endif
};

test bool_task_await_suspend_with_non_empty_handle = [] {
  using sm_t = utility::co_sm<c, utility::policy::coroutine_scheduler<deferred_scheduler>,
                              utility::policy::coroutine_allocator<counting_allocator>>;

  sm_t sm{};
  auto task = sm.process_event_async(e1{});
  const auto resumed_handle = task.await_suspend(std::noop_coroutine());
  expect(resumed_handle != std::noop_coroutine());
  sm.scheduler().run_pending();
  expect(task.result());
  expect(sm.is(s1));
};

test bool_task_promise_unhandled_exception_sets_ptr = [] {
#if !BOOST_SML_DISABLE_EXCEPTIONS
  utility::bool_task::promise_type promise{};
  try {
    throw std::runtime_error("test");
  } catch (...) {
    promise.unhandled_exception();
  }
  expect(promise.exception != nullptr);
#endif
};

struct null_allocator {
  void* allocate(std::size_t, std::size_t) { return nullptr; }
  void deallocate(void*, std::size_t, std::size_t) noexcept {}
};

test co_sm_allocator_failure_throws_bad_alloc = [] {
#if !BOOST_SML_DISABLE_EXCEPTIONS
  using sm_t = utility::co_sm<c, utility::policy::coroutine_scheduler<scheduler_without_try_run_immediate>,
                              utility::policy::coroutine_allocator<null_allocator>>;

  sm_t sm{};
  bool got_bad_alloc = false;
  try {
    auto task = sm.process_event_async(e1{});
    (void)task;
  } catch (const std::bad_alloc&) {
    got_bad_alloc = true;
  }
  expect(got_bad_alloc);
#endif
};

#else

test co_sm_disabled_without_coroutines = [] { expect(true); };

#endif
