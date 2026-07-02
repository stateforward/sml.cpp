//
// Copyright (c) 2026 stateforward
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_SML_UTILITY_EXTERNAL_COMPLETION_HPP
#define BOOST_SML_UTILITY_EXTERNAL_COMPLETION_HPP

#include "boost/sml.hpp"

#if defined(_MSVC_LANG)
#define BOOST_SML_UTILITY_EXTERNAL_COMPLETION_LANG _MSVC_LANG
#else
#define BOOST_SML_UTILITY_EXTERNAL_COMPLETION_LANG __cplusplus
#endif

// Opt-in module: external completion delivery needs C++20 coroutines plus a
// hosted std::counting_semaphore, so it compiles to nothing on freestanding or
// pre-C++20 toolchains, keeping the header-only core dependency-free.
#if BOOST_SML_UTILITY_EXTERNAL_COMPLETION_LANG >= 202002L && (defined(__cpp_impl_coroutine) || defined(__cpp_coroutines))
#if defined(__has_include)
#if __has_include(<semaphore>)
#define BOOST_SML_UTILITY_EXTERNAL_COMPLETION_ENABLED 1
#endif
#endif
#endif
#ifndef BOOST_SML_UTILITY_EXTERNAL_COMPLETION_ENABLED
#define BOOST_SML_UTILITY_EXTERNAL_COMPLETION_ENABLED 0
#endif

#if BOOST_SML_UTILITY_EXTERNAL_COMPLETION_ENABLED
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <semaphore>

BOOST_SML_NAMESPACE_BEGIN

namespace utility {

// Generic completion trigger delivered into a machine's transition table by a
// co_sm running an external-completion scheduler policy. source_index names the
// completion_source that fired; machines map it onto domain state (for example
// a buffer slot) via their own guards and actions.
struct completion {
  std::size_t source_index{};
};

namespace policy {

// One externally fired completion flag. Producers (worker threads, device
// callbacks) touch only fire(); every other member is single-consumer and runs
// on the dispatching thread. fire() never resumes a coroutine and never touches
// the state machine: it flips the flag and releases the scheduler's semaphore,
// so single-writer dispatch holds structurally no matter which thread fires.
class completion_source {
 public:
  // Trampoline compatible with thread_pool_scheduler::try_submit_with_completion.
  // Only the transition into fired releases a wakeup permit, so a misbehaving
  // producer that fires twice cannot accumulate permits or overflow the
  // semaphore's max count.
  static void fire(void* ctx) noexcept {
    auto* source = static_cast<completion_source*>(ctx);
    if (source->state_.exchange(state_fired, std::memory_order_release) != state_fired) {
      source->wakeup_->release();
    }
  }

  void arm() noexcept { state_.store(state_pending, std::memory_order_relaxed); }
  void reset() noexcept { state_.store(state_idle, std::memory_order_relaxed); }
  bool is_fired() const noexcept { return state_.load(std::memory_order_acquire) == state_fired; }
  bool is_idle() const noexcept { return state_.load(std::memory_order_acquire) == state_idle; }

 private:
  template <std::size_t>
  friend class external_completion_scheduler;

  static constexpr std::uint8_t state_idle = 0;
  static constexpr std::uint8_t state_pending = 1;
  static constexpr std::uint8_t state_fired = 2;

  std::atomic<std::uint8_t> state_{state_idle};
  std::counting_semaphore<>* wakeup_ = nullptr;
};

// Scheduler policy that lets a co_sm dispatch block on completions fired by
// other threads while still returning only when the whole dispatch is done:
//
//   1. commit sweep — completions that fired between dispatches are delivered
//      as explicit `utility::completion` events, ascending index order, before
//      the trigger event runs;
//   2. the trigger event dispatches; its actions may arm sources, hand them to
//      workers, and mark some of them required for this dispatch;
//   3. required-wait drain — the dispatching thread consumes each required
//      source (lowest fired index first) as a `utility::completion` event,
//      blocking on the semaphore between fires, until none remain.
//
// All bookkeeping is single-consumer on the dispatching thread; producers
// interact only through completion_source::fire. The run-to-completion
// guarantees are honored because nothing escapes the top-level dispatch:
// required completions are drained before it returns, and unrequired
// (background) fires persist only as passive flags swept by the next dispatch.
// The drive is a plain loop, so the no-completion fast path adds only one
// sweep scan and one mask check over an inline dispatch.
//
// Contract: a source marked required must have been handed to a producer that
// will eventually fire it, or the drain loop cannot finish. Destroying the
// scheduler while producers still hold source pointers is undefined; owners
// drain or join in-flight work first.
template <std::size_t SourceCount = 8>
class external_completion_scheduler {
 public:
  static_assert(SourceCount > 0, "external_completion_scheduler needs at least one source");
  static_assert(SourceCount <= 64, "required-source bookkeeping is a 64-bit mask");

  static constexpr bool guarantees_fifo = true;
  static constexpr bool single_consumer = true;
  static constexpr bool run_to_completion = true;
  static constexpr bool external_completion = true;
  static constexpr std::size_t npos = static_cast<std::size_t>(-1);

  using completion_event = completion;

  external_completion_scheduler() noexcept {
    for (auto& source : sources_) {
      source.wakeup_ = &wakeup_;
    }
  }

  external_completion_scheduler(const external_completion_scheduler&) = delete;
  external_completion_scheduler& operator=(const external_completion_scheduler&) = delete;
  external_completion_scheduler(external_completion_scheduler&&) = delete;
  external_completion_scheduler& operator=(external_completion_scheduler&&) = delete;

  // Continuations run inline on the dispatching thread.
  template <class TFn>
  void schedule(TFn&& fn) noexcept(noexcept(static_cast<TFn&&>(fn)())) {
    static_cast<TFn&&>(fn)();
  }

  completion_source& source(const std::size_t index) noexcept { return sources_[index]; }

  // Clears bookkeeping an aborted dispatch may have left behind (for example
  // a state-machine action that threw after require()): required bits are
  // meaningless outside the dispatch that created them, and stale bits would
  // otherwise block the next drain forever.
  void reset_dispatch_state() noexcept { required_ = 0; }

  // Marks a source as blocking the current dispatch. Consumer thread only
  // (machine actions via an injected scheduler pointer).
  void require(const std::size_t index) noexcept { required_ |= bit(index); }

  bool has_required() const noexcept { return required_ != 0u; }

  // Lowest fired source not marked required, consumed and returned for
  // delivery as a completion event; npos when none. Consumer thread only.
  std::size_t sweep_next_fired() noexcept {
    for (std::size_t index = 0; index < SourceCount; ++index) {
      if ((required_ & bit(index)) == 0u && sources_[index].is_fired()) {
        sources_[index].reset();
        return index;
      }
    }
    return npos;
  }

  // Lowest fired source marked required, consumed (required bit cleared,
  // source reset) and returned for delivery as a completion event; npos when
  // none has fired yet. Consumer thread only.
  std::size_t consume_next_fired_required() noexcept {
    const std::size_t index = lowest_required_fired();
    if (index == npos) {
      return npos;
    }
    required_ &= ~bit(index);
    sources_[index].reset();
    return index;
  }

  // Blocks the dispatching thread until some source fires. Permits accumulate,
  // so a fire that lands before the wait is never lost; a permit consumed for
  // a background (unrequired) fire simply re-checks and waits again, leaving
  // that fire set for a later sweep.
  void wait_any() noexcept { wakeup_.acquire(); }

 private:
  static constexpr std::uint64_t bit(const std::size_t index) noexcept { return std::uint64_t{1} << index; }

  std::size_t lowest_required_fired() const noexcept {
    for (std::size_t index = 0; index < SourceCount; ++index) {
      if ((required_ & bit(index)) != 0u && sources_[index].is_fired()) {
        return index;
      }
    }
    return npos;
  }

  std::array<completion_source, SourceCount> sources_{};
  std::counting_semaphore<> wakeup_{0};
  std::uint64_t required_ = 0;
};

}  // namespace policy
}  // namespace utility

BOOST_SML_NAMESPACE_END
#endif

#undef BOOST_SML_UTILITY_EXTERNAL_COMPLETION_LANG

#endif  // BOOST_SML_UTILITY_EXTERNAL_COMPLETION_HPP
