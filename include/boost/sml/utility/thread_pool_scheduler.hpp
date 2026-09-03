//
// Copyright (c) 2026 stateforward
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_HPP
#define BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_HPP

#include "boost/sml.hpp"

#if defined(_MSVC_LANG)
#define BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_LANG _MSVC_LANG
#else
#define BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_LANG __cplusplus
#endif

// Opt-in module: unlike the header-only, dependency-free SML core, the thread
// pool scheduler requires C++20 (std::counting_semaphore) and a hosted
// threading runtime. It compiles to nothing when those are unavailable so the
// freestanding core stays thread-free by default.
// Nest the __has_include probe under defined(__has_include) so toolchains
// without the extension (or below C++20) never have to parse it.
#if BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_LANG >= 202002L
#if defined(__has_include)
#if __has_include(<semaphore>)
#define BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_ENABLED 1
#endif
#endif
#endif
#ifndef BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_ENABLED
#define BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_ENABLED 0
#endif

#if BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_ENABLED
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <semaphore>
#include <thread>
#include <type_traits>
#include <utility>
#if !BOOST_SML_DISABLE_EXCEPTIONS
#include <stdexcept>
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>  // _mm_pause / __yield
#endif

BOOST_SML_NAMESPACE_BEGIN

namespace utility {
namespace policy {

// Architecture-appropriate spin hint for short busy-waits (lock-free join /
// queue retries). Reduces contention and power on hyperthreads without yielding
// the scheduler.
inline void cpu_relax() noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
#if defined(_M_X64) || defined(_M_IX86)
  _mm_pause();
#elif defined(_M_ARM64) || defined(_M_ARM)
  __yield();
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
#elif defined(__x86_64__) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
  __asm__ __volatile__("yield" ::: "memory");
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

template <std::size_t worker_count = 2, std::size_t capacity = 1024, std::size_t inline_task_bytes = 64>
class thread_pool_scheduler {
 public:
  static_assert(worker_count > 0, "thread_pool_scheduler worker count must be non-zero");
  static_assert(capacity > 1, "thread_pool_scheduler capacity must be greater than one");
  static_assert((capacity & (capacity - 1)) == 0, "thread_pool_scheduler capacity must be a power of two");
  static_assert(inline_task_bytes > 0, "thread_pool_scheduler inline storage must be non-zero");

  static constexpr bool guarantees_fifo = false;
  static constexpr bool single_consumer = false;
  static constexpr bool multi_consumer = true;
  static constexpr bool owns_workers = true;
  static constexpr bool run_to_completion = false;
  static constexpr std::size_t static_worker_count = worker_count;
  static constexpr std::size_t static_capacity = capacity;
#if defined(BOOST_SML_THREAD_POOL_SCHEDULER_TEST_HOOKS)
  using batch_reservation_hook = void (*)(std::size_t, std::size_t) noexcept;
  inline static batch_reservation_hook test_batch_reservation_hook = nullptr;
#endif

  class worker_budget {
   public:
    explicit operator bool() const noexcept { return value_ != 0u; }
    std::size_t value() const noexcept { return value_; }

   private:
    friend class thread_pool_scheduler;
    worker_budget() = default;
    explicit worker_budget(const std::size_t value) noexcept : value_(value) {}

    std::size_t value_ = 0u;
  };

  // Initialization-only validation for requested active workers in
  // [1, static_worker_count]. An invalid request returns an empty status token.
  static worker_budget try_worker_budget(const std::size_t requested) noexcept {
    return requested > 0u && requested <= worker_count ? worker_budget{requested} : worker_budget{};
  }

  // std::thread may allocate OS/runtime resources here; dispatch uses only the
  // fixed task storage below. The default starts every statically provisioned
  // worker.
  thread_pool_scheduler() : active_worker_count_(worker_count) { start_workers(); }

  // Initialization-only. Precondition: budget is a successful result from
  // try_worker_budget(); an invalid token terminates without starting workers.
  explicit thread_pool_scheduler(const worker_budget budget) : active_worker_count_(budget.value()) {
    if (!budget) {
      std::terminate();
    }
    start_workers();
  }

#if !BOOST_SML_DISABLE_EXCEPTIONS
  explicit thread_pool_scheduler(const std::size_t active_worker_count)
      : active_worker_count_(active_worker_count) {
    if (active_worker_count_ == 0u || active_worker_count_ > worker_count) {
      throw std::invalid_argument{"thread_pool_scheduler worker count"};
    }
    start_workers();
  }
#endif

  ~thread_pool_scheduler() { stop_workers(); }

  thread_pool_scheduler(const thread_pool_scheduler&) = delete;
  thread_pool_scheduler& operator=(const thread_pool_scheduler&) = delete;
  thread_pool_scheduler(thread_pool_scheduler&&) = delete;
  thread_pool_scheduler& operator=(thread_pool_scheduler&&) = delete;

  class join_group {
   public:
    join_group() = default;
    ~join_group() = default;

    join_group(const join_group&) = delete;
    join_group& operator=(const join_group&) = delete;
    join_group(join_group&&) = delete;
    join_group& operator=(join_group&&) = delete;

    bool wait() noexcept {
      // Spin-join on pending_ rather than blocking on a per-group semaphore.
      // The group is caller-owned and typically stack-reused across fork/joins,
      // so a notify-based wakeup is unsafe: the waiter could observe completion,
      // return, and destroy the group before the last completer finishes its
      // release()/notify, faulting on freed semaphore state. With a plain spin,
      // a completer's final touch of the group is its pending_ decrement, and
      // wait() returns only after observing pending_ == 0 (all decrements done),
      // so nothing accesses the group after the caller may destroy it. The wait
      // is a bounded RTC fork/join over already-submitted lanes, so the producer
      // core would otherwise be idle; spinning gives the lowest join latency.
      while (pending_.load(std::memory_order_acquire) != 0u) {
        cpu_relax();
      }
      // pending_ == 0 means every completer is done touching the group, so the
      // owning thread can now read and clear accepted_ with no contention. The
      // clear lets a stack-reused join_group start the next round clean: without
      // it, a single rejected lane would make wait() return false on every later
      // round even when all lanes succeed.
      const bool accepted = accepted_.load(std::memory_order_acquire);
      accepted_.store(true, std::memory_order_release);
      return accepted;
    }

   private:
    friend class thread_pool_scheduler;

    void start_one() noexcept { pending_.fetch_add(1u, std::memory_order_acq_rel); }

    void reject_one() noexcept {
      accepted_.store(false, std::memory_order_release);
      complete_one();
    }

    void reject() noexcept { accepted_.store(false, std::memory_order_release); }

    void complete_one() noexcept { pending_.fetch_sub(1u, std::memory_order_acq_rel); }

    static void complete_one(void* ctx) noexcept { static_cast<join_group*>(ctx)->complete_one(); }

    std::atomic<std::uint32_t> pending_ = 0u;
    std::atomic<bool> accepted_ = true;
  };

  template <class fn>
  bool try_run_immediate(fn&& fn_in) noexcept(noexcept(std::forward<fn>(fn_in)())) {
    if (queued_or_running_.load(std::memory_order_acquire) != 0u) {
      return false;
    }

    bool expected = false;
    if (!inline_active_.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
      return false;
    }

    struct reset_inline {
      std::atomic<bool>& active;
      ~reset_inline() noexcept { active.store(false, std::memory_order_release); }
    } reset{inline_active_};

    if (queued_or_running_.load(std::memory_order_acquire) != 0u) {
      return false;
    }

    std::forward<fn>(fn_in)();
    return true;
  }

  template <class fn>
  bool try_submit(fn&& fn_in) noexcept {
    return try_submit_with_completion(std::forward<fn>(fn_in), nullptr, nullptr);
  }

  template <class fn>
  bool try_submit(join_group& group, fn&& fn_in) noexcept {
    if (is_current_thread_worker()) {
      group.reject();
      return false;
    }

    group.start_one();
    const bool submitted = try_submit_with_completion(std::forward<fn>(fn_in), &group, join_group::complete_one);
    if (!submitted) {
      group.reject_one();
      return false;
    }
    return true;
  }
  // Runtime-safe for external producer threads. Atomically reserves one
  // distinct active worker for every callable. Either the full batch is
  // published, or no callable is moved or executed and the group reports
  // rejection. Callables are moved into fixed scheduler-owned storage and
  // destroyed by their worker; callers must wait before reusing the group.
  template <class... fns>
  std::size_t try_submit_batch(join_group& group, fns&&... fns_in) noexcept {
    constexpr std::size_t batch_size = sizeof...(fns);
    static_assert(batch_size > 0u, "thread_pool_scheduler batch must contain tasks");
    if (running_on_this_worker() || stopping_.load(std::memory_order_acquire) ||
        batch_size > active_worker_count_) {
      group.reject();
      return 0u;
    }
    queued_or_running_.fetch_add(batch_size, std::memory_order_acq_rel);

    std::array<std::size_t, batch_size> claimed_workers{};
    std::size_t claimed_count = 0u;
    const std::size_t start = next_worker_.fetch_add(batch_size, std::memory_order_relaxed) % active_worker_count_;
    for (std::size_t offset = 0u; offset < active_worker_count_ && claimed_count < batch_size; ++offset) {
      const std::size_t worker_index = (start + offset) % active_worker_count_;
      worker_state expected = worker_state::idle;
      if (worker_slots_[worker_index].state.compare_exchange_strong(expected, worker_state::reserved,
                                                                    std::memory_order_acq_rel,
                                                                    std::memory_order_acquire)) {
        claimed_workers[claimed_count++] = worker_index;
      }
    }
#if defined(BOOST_SML_THREAD_POOL_SCHEDULER_TEST_HOOKS)
    if (test_batch_reservation_hook != nullptr) {
      test_batch_reservation_hook(claimed_count, batch_size);
    }
#endif
    if (claimed_count != batch_size) {
      for (std::size_t index = 0u; index < claimed_count; ++index) {
        worker_slots_[claimed_workers[index]].state.store(worker_state::idle, std::memory_order_release);
        worker_slots_[claimed_workers[index]].state.notify_all();
      }
      queued_or_running_.fetch_sub(batch_size, std::memory_order_acq_rel);
      group.reject();
      return 0u;
    }

    std::size_t task_index = 0u;
    (set_batch_task(group, claimed_workers[task_index++], std::forward<fns>(fns_in)), ...);
    for (const std::size_t worker_index : claimed_workers) {
      worker_slots_[worker_index].state.store(worker_state::committed, std::memory_order_release);
      worker_slots_[worker_index].state.notify_one();
    }
    for (const std::size_t worker_index : claimed_workers) {
      worker_slots_[worker_index].ready.release();
    }
    return batch_size;
  }

  template <class fn>
  void schedule(fn&& fn_in) noexcept {
    if (!try_submit(std::forward<fn>(fn_in))) {
      std::terminate();
    }
  }

  // Detached hard-contract wrapper for call sites that have already proven
  // scheduler lifetime and queue capacity. RTC call sites use
  // run_or_schedule_and_wait or thread_pool_scheduler::join_group.
  template <class fn>
  void submit(fn&& fn_in) noexcept {
    schedule(std::forward<fn>(fn_in));
  }

  // Unconditionally noexcept (like fifo_scheduler::schedule): when this falls to
  // the scheduled path the task runs through a noexcept worker thunk, which
  // cannot propagate an exception back to the waiter, so the API is consistently
  // no-throw rather than propagate-or-terminate by pool load. Submit noexcept
  // work (the RTC actor contract); a throwing task calls std::terminate.
  template <class fn>
  bool run_or_schedule_and_wait(fn&& fn_in) noexcept {
    if (try_run_immediate(std::forward<fn>(fn_in))) {
      return true;
    }
    if (running_on_this_worker()) {
      return false;
    }

    // Spin-join on a local flag rather than blocking on a semaphore: the worker
    // sets done last, so the waiter returns only after the worker's final write
    // and can safely let the flag go out of scope (no destroy-during-notify
    // fault). The wait is a bounded RTC join over one already-submitted task.
    std::atomic<bool> done{false};
    const bool scheduled = try_submit_and_signal([&fn_in]() noexcept { fn_in(); }, done);
    if (!scheduled) {
      return false;
    }
    while (!done.load(std::memory_order_acquire)) {
      cpu_relax();
    }
    return true;
  }

  bool is_current_thread_worker() const noexcept { return running_on_this_worker(); }
  std::size_t active_worker_count() const noexcept { return active_worker_count_; }

  template <class fn>
  bool try_submit_with_completion(fn&& fn_in, void* completion_ctx, void (*completion_fn)(void*) noexcept) noexcept {
    if (stopping_.load(std::memory_order_acquire)) {
      return false;
    }

    queued_or_running_.fetch_add(1u, std::memory_order_acq_rel);
    const bool enqueued = enqueue(std::forward<fn>(fn_in), completion_ctx, completion_fn);
    if (!enqueued) {
      queued_or_running_.fetch_sub(1u, std::memory_order_acq_rel);
      return false;
    }
    const std::size_t worker_index = next_worker_.fetch_add(1u, std::memory_order_relaxed) % active_worker_count_;
    worker_slots_[worker_index].ready.release();
    return true;
  }

 private:
  struct task_slot {
    using invoke_fn = void (*)(void*) noexcept;
    using destroy_fn = void (*)(void*) noexcept;

    alignas(std::max_align_t) std::array<unsigned char, inline_task_bytes> storage{};
    std::atomic<std::size_t> sequence = 0u;
    invoke_fn invoke = nullptr;
    destroy_fn destroy = nullptr;
    void* completion_ctx = nullptr;
    void (*completion_fn)(void*) noexcept = nullptr;

    template <class fn>
    void set(fn&& fn_in, void* completion_ctx_in, void (*completion_fn_in)(void*) noexcept) noexcept {
      using fn_type = std::decay_t<fn>;
      static_assert(sizeof(fn_type) <= inline_task_bytes, "scheduled task exceeds inline storage capacity");
      static_assert(alignof(fn_type) <= alignof(std::max_align_t),
                    "scheduled task alignment exceeds scheduler storage alignment");

      new (storage.data()) fn_type(std::forward<fn>(fn_in));
      invoke = [](void* ptr) noexcept { (*static_cast<fn_type*>(ptr))(); };
      destroy = [](void* ptr) noexcept { static_cast<fn_type*>(ptr)->~fn_type(); };
      completion_ctx = completion_ctx_in;
      completion_fn = completion_fn_in;
    }

    void run() noexcept {
      invoke(storage.data());
      destroy(storage.data());
      invoke = nullptr;
      destroy = nullptr;
    }

    void reset() noexcept {
      if (destroy != nullptr) {
        destroy(storage.data());
      }
      invoke = nullptr;
      destroy = nullptr;
      completion_ctx = nullptr;
      completion_fn = nullptr;
    }
  };
  enum class worker_state : unsigned char { idle, queue_running, reserved, committed };

  struct worker_slot {
    task_slot batch_task{};
    std::counting_semaphore<> ready{0};
    std::thread thread{};
    std::atomic<worker_state> state = worker_state::idle;
  };

  static constexpr std::size_t index_mask = capacity - 1u;

  void start_workers() {
    for (std::size_t i = 0; i < capacity; ++i) {
      tasks_[i].sequence.store(i, std::memory_order_relaxed);
    }
#if BOOST_SML_DISABLE_EXCEPTIONS
    // No-exceptions builds cannot observe a failed std::thread spawn (it calls
    // std::terminate), so there is nothing to roll back; spawn directly.
    for (std::size_t started = 0u; started < active_worker_count_; ++started) {
      worker_slots_[started].thread = std::thread([this, started]() noexcept { worker_loop(started); });
    }
#else
    std::size_t started = 0u;
    try {
      for (; started < active_worker_count_; ++started) {
        worker_slots_[started].thread = std::thread([this, started]() noexcept { worker_loop(started); });
      }
    } catch (...) {
      stopping_.store(true, std::memory_order_release);
      for (std::size_t i = 0; i < started; ++i) {
        worker_slots_[i].state.notify_all();
        worker_slots_[i].ready.release();
      }
      for (std::size_t i = 0; i < started; ++i) {
        if (worker_slots_[i].thread.joinable()) {
          worker_slots_[i].thread.join();
        }
      }
      throw;
    }
#endif
  }

  void stop_workers() noexcept {
    const bool was_stopping = stopping_.exchange(true, std::memory_order_acq_rel);
    if (was_stopping) {
      return;
    }

    for (std::size_t i = 0; i < active_worker_count_; ++i) {
      worker_slots_[i].state.notify_all();
      worker_slots_[i].ready.release();
    }

    for (std::size_t i = 0; i < active_worker_count_; ++i) {
      if (worker_slots_[i].thread.joinable()) {
        worker_slots_[i].thread.join();
      }
      worker_slots_[i].batch_task.reset();
    }

    clear_unrun_tasks();
  }

  template <class fn>
  bool try_submit_and_signal(fn&& fn_in, std::atomic<bool>& done) noexcept {
    return try_submit_with_completion(std::forward<fn>(fn_in), &done, signal_done_flag);
  }

  template <class fn>
  void set_batch_task(join_group& group, const std::size_t worker_index, fn&& fn_in) noexcept {
    using fn_type = std::decay_t<fn>;
    static_assert(std::is_nothrow_constructible_v<fn_type, fn&&>,
                  "thread_pool_scheduler batch task construction must not throw");
    static_assert(std::is_nothrow_invocable_v<fn_type&>,
                  "thread_pool_scheduler batch task invocation must not throw");
    group.start_one();
    worker_slots_[worker_index].batch_task.set(std::forward<fn>(fn_in), &group, join_group::complete_one);
  }

  template <class fn>
  bool enqueue(fn&& fn_in, void* completion_ctx, void (*completion_fn)(void*) noexcept) noexcept {
    task_slot* slot = nullptr;
    std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
    for (;;) {
      slot = &tasks_[pos & index_mask];
      const std::size_t seq = slot->sequence.load(std::memory_order_acquire);
      const auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
      if (diff == 0) {
        if (enqueue_pos_.compare_exchange_weak(pos, pos + 1u, std::memory_order_relaxed, std::memory_order_relaxed)) {
          break;
        }
      } else if (diff < 0) {
        return false;
      } else {
        pos = enqueue_pos_.load(std::memory_order_relaxed);
      }
    }

    slot->set(std::forward<fn>(fn_in), completion_ctx, completion_fn);
    slot->sequence.store(pos + 1u, std::memory_order_release);
    return true;
  }

  bool try_dequeue_and_run(worker_slot* executing_worker = nullptr) noexcept {
    task_slot* slot = nullptr;
    std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
    for (;;) {
      slot = &tasks_[pos & index_mask];
      const std::size_t seq = slot->sequence.load(std::memory_order_acquire);
      const auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1u);
      if (diff == 0) {
        if (dequeue_pos_.compare_exchange_weak(pos, pos + 1u, std::memory_order_relaxed, std::memory_order_relaxed)) {
          break;
        }
      } else if (diff < 0) {
        return false;
      } else {
        pos = dequeue_pos_.load(std::memory_order_relaxed);
      }
    }

    slot->run();
    void* completion_ctx = slot->completion_ctx;
    void (*completion_fn)(void*) noexcept = slot->completion_fn;
    slot->completion_ctx = nullptr;
    slot->completion_fn = nullptr;
    queued_or_running_.fetch_sub(1u, std::memory_order_acq_rel);
    slot->sequence.store(pos + capacity, std::memory_order_release);
    if (executing_worker != nullptr) {
      executing_worker->state.store(worker_state::idle, std::memory_order_release);
    }
    if (completion_fn != nullptr) {
      completion_fn(completion_ctx);
    }
    return true;
  }

  static void signal_done_flag(void* ctx) noexcept {
    static_cast<std::atomic<bool>*>(ctx)->store(true, std::memory_order_release);
  }

  void worker_loop(const std::size_t index) noexcept {
    struct worker_scope {
      const thread_pool_scheduler* previous;
      explicit worker_scope(const thread_pool_scheduler* current) noexcept : previous(active_worker_scheduler_) {
        active_worker_scheduler_ = current;
      }
      ~worker_scope() noexcept { active_worker_scheduler_ = previous; }
    } scope{this};

    worker_slot& worker = worker_slots_[index];
    for (;;) {
      worker.ready.acquire();

      for (;;) {
        worker_state state = worker.state.load(std::memory_order_acquire);
        if (state == worker_state::reserved) {
          worker.state.wait(worker_state::reserved, std::memory_order_acquire);
          continue;
        }
        if (state == worker_state::committed) {
          run_batch_task(worker);
          break;
        }
        if (stopping_.load(std::memory_order_acquire)) {
          return;
        }

        worker_state expected = worker_state::idle;
        if (!worker.state.compare_exchange_strong(expected, worker_state::queue_running, std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
          continue;
        }
        if (!try_dequeue_and_run(&worker)) {
          worker.state.store(worker_state::idle, std::memory_order_release);
        }
        break;
      }
    }
  }

  void run_batch_task(worker_slot& worker) noexcept {
    worker.batch_task.run();
    void* completion_ctx = worker.batch_task.completion_ctx;
    void (*completion_fn)(void*) noexcept = worker.batch_task.completion_fn;
    worker.batch_task.completion_ctx = nullptr;
    worker.batch_task.completion_fn = nullptr;
    worker.state.store(worker_state::idle, std::memory_order_release);
    queued_or_running_.fetch_sub(1u, std::memory_order_acq_rel);
    if (completion_fn != nullptr) {
      completion_fn(completion_ctx);
    }
  }


  bool running_on_this_worker() const noexcept { return active_worker_scheduler_ == this; }

  void clear_unrun_tasks() noexcept {
    while (try_dequeue_and_run()) {
    }
    for (auto& task : tasks_) {
      task.reset();
    }
  }

  std::array<task_slot, capacity> tasks_{};
  std::array<worker_slot, worker_count> worker_slots_{};
  const std::size_t active_worker_count_;
  std::atomic<std::size_t> enqueue_pos_ = 0u;
  std::atomic<std::size_t> dequeue_pos_ = 0u;
  std::atomic<std::size_t> queued_or_running_ = 0u;
  std::atomic<std::size_t> next_worker_ = 0u;
  std::atomic<bool> inline_active_ = false;
  std::atomic<bool> stopping_ = false;
  inline static thread_local const thread_pool_scheduler* active_worker_scheduler_ = nullptr;
};

}  // namespace policy
}  // namespace utility

BOOST_SML_NAMESPACE_END
#endif  // BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_ENABLED

#endif  // BOOST_SML_UTILITY_THREAD_POOL_SCHEDULER_HPP
