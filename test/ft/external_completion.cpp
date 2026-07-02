//
// Copyright (c) 2026 stateforward
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#include <array>
#include <atomic>
#include <boost/sml.hpp>
#include <boost/sml/utility/co_sm.hpp>
#include <boost/sml/utility/external_completion.hpp>
#include <boost/sml/utility/thread_pool_scheduler.hpp>
#include <chrono>
#include <cstddef>
#include <thread>

namespace sml = boost::sml;

#if BOOST_SML_UTILITY_EXTERNAL_COMPLETION_ENABLED && BOOST_SML_UTILITY_CO_SM_ENABLED && \
    BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_ENABLED
namespace utility = sml::utility;

using scheduler_t = utility::policy::external_completion_scheduler<8>;
using pool_t = utility::policy::thread_pool_scheduler<1>;

static_assert(utility::policy::external_completion_scheduler_contract<scheduler_t>,
              "external_completion_scheduler must satisfy the external completion contract");
static_assert(!utility::policy::external_completion_scheduler_contract<utility::policy::inline_scheduler>,
              "inline_scheduler must not satisfy the external completion contract");
static_assert(!utility::policy::external_completion_scheduler_contract<utility::policy::fifo_scheduler<>>,
              "fifo_scheduler must not satisfy the external completion contract");
static_assert(utility::policy::strict_ordering_scheduler_contract<scheduler_t>,
              "external completion drains before return, honoring the strict ordering contract");

namespace {

constexpr std::size_t k_probe_marker = 99;

struct test_context {
  scheduler_t* scheduler = nullptr;
  pool_t* pool = nullptr;
  std::array<std::size_t, 16> log{};
  std::size_t log_count = 0;
  std::thread::id completion_thread{};
  bool worker_delay = false;
};

struct e_require_via_worker {
  std::size_t count{};
};
struct e_require_fired_inline {
  std::size_t count{};
};
struct e_hand_to_worker_unrequired {
  std::size_t index{};
};
struct e_probe {};

void fire_after_optional_delay(test_context& context, const std::size_t index) {
  const bool delay = context.worker_delay;
  (void)context.pool->try_submit_with_completion(
      [delay]() noexcept {
        if (delay) {
          const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
          while (std::chrono::steady_clock::now() < until) {
          }
        }
      },
      &context.scheduler->source(index), &utility::policy::completion_source::fire);
}

struct machine {
  auto operator()() const {
    using namespace sml;

    const auto a_require_via_worker = [](const e_require_via_worker& ev, test_context& context) {
      for (std::size_t index = 0; index < ev.count; ++index) {
        context.scheduler->source(index).arm();
        fire_after_optional_delay(context, index);
      }
      for (std::size_t index = 0; index < ev.count; ++index) {
        context.scheduler->require(index);
      }
    };

    const auto a_require_fired_inline = [](const e_require_fired_inline& ev, test_context& context) {
      for (std::size_t index = 0; index < ev.count; ++index) {
        context.scheduler->source(index).arm();
      }
      // Fire in descending order on this thread: delivery must still ascend.
      for (std::size_t reverse = ev.count; reverse > 0; --reverse) {
        utility::policy::completion_source::fire(&context.scheduler->source(reverse - 1));
      }
      for (std::size_t index = 0; index < ev.count; ++index) {
        context.scheduler->require(index);
      }
    };

    const auto a_hand_to_worker_unrequired = [](const e_hand_to_worker_unrequired& ev, test_context& context) {
      context.scheduler->source(ev.index).arm();
      fire_after_optional_delay(context, ev.index);
    };

    const auto a_probe = [](const e_probe&, test_context& context) { context.log[context.log_count++] = k_probe_marker; };

    const auto a_record = [](const utility::completion& ev, test_context& context) {
      context.log[context.log_count++] = ev.source_index;
      context.completion_thread = std::this_thread::get_id();
    };

    // clang-format off
    return make_transition_table(
      *"ready"_s + event<e_require_via_worker> / a_require_via_worker
      , "ready"_s + event<e_require_fired_inline> / a_require_fired_inline
      , "ready"_s + event<e_hand_to_worker_unrequired> / a_hand_to_worker_unrequired
      , "ready"_s + event<e_probe> / a_probe
      , "ready"_s + event<utility::completion> / a_record
    );
    // clang-format on
  }
};

using co_sm_t = utility::co_sm<machine, utility::policy::coroutine_scheduler<scheduler_t>,
                               utility::policy::coroutine_allocator<utility::policy::pooled_coroutine_allocator<>>,
                               test_context>;

struct fixture {
  pool_t pool{};
  test_context context{};
  co_sm_t machine_instance;

  fixture() : machine_instance{context} {
    context.scheduler = &machine_instance.scheduler();
    context.pool = &pool;
  }
};

}  // namespace

test external_completion_required_drained_before_return = [] {
  fixture f{};
  f.context.worker_delay = true;  // force the dispatch coroutine to park

  expect(f.machine_instance.process_event(e_require_via_worker{3}));

  // Everything observable happened inside the dispatch: all three completions
  // were delivered, nothing remains required, and the sources are reusable.
  expect(3u == f.context.log_count);
  expect(!f.machine_instance.scheduler().has_required());
  for (std::size_t index = 0; index < 3; ++index) {
    expect(f.machine_instance.scheduler().source(index).is_idle());
  }
};

test external_completion_delivers_on_dispatching_thread = [] {
  fixture f{};
  f.context.worker_delay = true;

  expect(f.machine_instance.process_event(e_require_via_worker{1}));

  expect(1u == f.context.log_count);
  expect(std::this_thread::get_id() == f.context.completion_thread);
};

test external_completion_prefired_sources_deliver_ascending_without_park = [] {
  fixture f{};

  // Sources fire 2,1,0 on this thread before the drain runs: no suspension,
  // and delivery is deterministic ascending regardless of fire order.
  expect(f.machine_instance.process_event(e_require_fired_inline{3}));

  expect(3u == f.context.log_count);
  expect(0u == f.context.log[0]);
  expect(1u == f.context.log[1]);
  expect(2u == f.context.log[2]);
};

test external_completion_background_fire_swept_before_next_trigger = [] {
  fixture f{};

  // Dispatch 1 hands source 5 to a worker without requiring it; the dispatch
  // returns while the fire is still pending.
  expect(f.machine_instance.process_event(e_hand_to_worker_unrequired{5}));
  expect(0u == f.context.log_count);

  while (!f.machine_instance.scheduler().source(5).is_fired()) {
    std::this_thread::yield();
  }

  // Dispatch 2: the commit sweep delivers completion(5) before the trigger.
  expect(f.machine_instance.process_event(e_probe{}));
  expect(2u == f.context.log_count);
  expect(5u == f.context.log[0]);
  expect(k_probe_marker == f.context.log[1]);
  expect(f.machine_instance.scheduler().source(5).is_idle());
};

test external_completion_dispatch_without_requires_never_parks = [] {
  fixture f{};

  expect(f.machine_instance.process_event(e_probe{}));

  expect(1u == f.context.log_count);
  expect(k_probe_marker == f.context.log[0]);
};

test external_completion_async_wrapper_completes_inline = [] {
  fixture f{};
  f.context.worker_delay = true;

  utility::bool_task task = f.machine_instance.process_event_async(e_require_via_worker{2});

  expect(task.await_ready());
  expect(task.result());
  expect(2u == f.context.log_count);
};

test external_completion_sources_reusable_across_dispatches = [] {
  fixture f{};

  for (std::size_t round = 0; round < 3; ++round) {
    f.context.log_count = 0;
    expect(f.machine_instance.process_event(e_require_fired_inline{2}));
    expect(2u == f.context.log_count);
    expect(0u == f.context.log[0]);
    expect(1u == f.context.log[1]);
  }
};
#else
test external_completion_disabled_without_cxx20_coroutines_and_semaphore = [] { expect(true); };
#endif
