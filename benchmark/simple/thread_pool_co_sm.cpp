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
#include <boost/sml/utility/thread_pool_scheduler.hpp>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <thread>

namespace sml = boost::sml;

struct tick {
  std::atomic<std::uint64_t>* calls = nullptr;
};

struct noop_child {
  auto operator()() const {
    using namespace sml;
    const auto record = [](const tick& event) noexcept { event.calls->fetch_add(1u, std::memory_order_relaxed); };
    // clang-format off
    return make_transition_table(
      *"ready"_s + event<tick> / record = "ready"_s
    );
    // clang-format on
  }
};

struct sleep_child {
  auto operator()() const {
    using namespace sml;
    const auto work = [](const tick& event) noexcept {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
      event.calls->fetch_add(1u, std::memory_order_relaxed);
    };
    // clang-format off
    return make_transition_table(
      *"ready"_s + event<tick> / work = "ready"_s
    );
    // clang-format on
  }
};

template <class fn>
std::chrono::nanoseconds measure(fn&& fn_in) noexcept {
  const auto start = std::chrono::high_resolution_clock::now();
  fn_in();
  const auto finish = std::chrono::high_resolution_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start);
}

void print_duration(const std::chrono::nanoseconds elapsed, const std::size_t dispatches) {
  const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  const auto ns_per_dispatch = dispatches == 0u ? 0u : static_cast<std::size_t>(elapsed.count()) / dispatches;
  std::cout << "execution speed: " << total_ms << "ms (" << ns_per_dispatch << "ns/dispatch)" << std::endl;
}

#if BOOST_SML_UTILITY_CO_SM_ENABLED && BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_ENABLED
template <class machine, std::size_t children_count, std::size_t rounds>
void run_case(const char* label) {
  using scheduler_t = sml::utility::policy::thread_pool_scheduler<1, 1024, 128>;
  using child_sm_t = sml::utility::co_sm<
      machine, sml::utility::policy::coroutine_scheduler<scheduler_t>,
      sml::utility::policy::coroutine_allocator<sml::utility::policy::pooled_coroutine_allocator<4096, children_count * 2>>>;

  constexpr std::size_t dispatches = children_count * rounds;

  std::array<std::atomic<std::uint64_t>, children_count> blocking_calls{};
  std::array<child_sm_t, children_count> blocking_children{};
  std::cout << label << " blocking run_or_schedule_and_wait: ";
  print_duration(
      measure([&] {
        for (std::size_t round = 0; round < rounds; ++round) {
          for (std::size_t i = 0u; i < children_count; ++i) {
            auto& child_sm = blocking_children[i];
            const bool submitted =
                child_sm.scheduler().run_or_schedule_and_wait([&child_sm, &blocking_calls, i]() noexcept {
                  (void)child_sm.process_event(tick{&blocking_calls[i]});
                });
            if (!submitted) {
              std::terminate();
            }
          }
        }
      }),
      dispatches);
  std::uint64_t blocking_total = 0u;
  for (const auto& calls : blocking_calls) {
    blocking_total += calls.load(std::memory_order_relaxed);
  }
  if (blocking_total != dispatches) {
    std::terminate();
  }

  std::array<std::atomic<std::uint64_t>, children_count> async_calls{};
  std::array<child_sm_t, children_count> async_children{};
  std::cout << label << " start-all await-later: ";
  print_duration(
      measure([&] {
        std::array<sml::utility::bool_task, children_count> tasks{};
        for (std::size_t round = 0; round < rounds; ++round) {
          for (std::size_t i = 0u; i < children_count; ++i) {
            tasks[i] = async_children[i].process_event_async(tick{&async_calls[i]});
          }
          for (auto& task : tasks) {
            while (!task.await_ready()) {
              sml::utility::policy::cpu_relax();
            }
            if (!task.result()) {
              std::terminate();
            }
          }
        }
      }),
      dispatches);
  std::uint64_t async_total = 0u;
  for (const auto& calls : async_calls) {
    async_total += calls.load(std::memory_order_relaxed);
  }
  if (async_total != dispatches) {
    std::terminate();
  }
}
#endif

int main() {
#if BOOST_SML_UTILITY_CO_SM_ENABLED && BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_ENABLED
  constexpr std::size_t children_count = 8u;
  run_case<noop_child, children_count, 10000>("noop");
  run_case<sleep_child, children_count, 1000>("10us work");
#endif
}
