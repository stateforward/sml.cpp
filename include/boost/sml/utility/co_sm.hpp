//
// Copyright (c) 2016-2026 Kris Jusiak (kris at jusiak dot net)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_SML_UTILITY_CO_SM_HPP
#define BOOST_SML_UTILITY_CO_SM_HPP

#include "boost/sml.hpp"

#if defined(_MSVC_LANG)
#define BOOST_SML_UTILITY_CO_SM_LANG _MSVC_LANG
#else
#define BOOST_SML_UTILITY_CO_SM_LANG __cplusplus
#endif

#if BOOST_SML_UTILITY_CO_SM_LANG >= 202002L && (defined(__cpp_impl_coroutine) || defined(__cpp_coroutines))
#define BOOST_SML_UTILITY_CO_SM_ENABLED 1
#else
#define BOOST_SML_UTILITY_CO_SM_ENABLED 0
#endif

#if BOOST_SML_UTILITY_CO_SM_ENABLED
#include <array>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#if !BOOST_SML_DISABLE_EXCEPTIONS
#include <stdexcept>
#endif

BOOST_SML_NAMESPACE_BEGIN

namespace utility {
namespace policy {

struct inline_scheduler {
  static constexpr bool guarantees_fifo = true;
  static constexpr bool single_consumer = true;
  static constexpr bool run_to_completion = true;

  template <class TFn>
  void schedule(TFn&& fn) noexcept(noexcept(std::forward<TFn>(fn)())) {
    std::forward<TFn>(fn)();
  }
};

template <std::size_t Capacity = 1024, std::size_t InlineTaskBytes = 64>
class fifo_scheduler {
 public:
  static_assert(Capacity > 1, "fifo_scheduler capacity must be greater than 1");
  static_assert((Capacity & (Capacity - 1)) == 0, "fifo_scheduler capacity must be a power of two");
  static_assert(InlineTaskBytes > 0, "fifo_scheduler inline storage must be non-zero");

  static constexpr bool guarantees_fifo = true;
  static constexpr bool single_consumer = true;
  static constexpr bool run_to_completion = true;

  fifo_scheduler() = default;
  ~fifo_scheduler() { clear(); }

  fifo_scheduler(const fifo_scheduler&) = delete;
  fifo_scheduler& operator=(const fifo_scheduler&) = delete;

  fifo_scheduler(fifo_scheduler&&) = delete;
  fifo_scheduler& operator=(fifo_scheduler&&) = delete;

  template <class TFn>
  bool try_run_immediate(TFn&& fn) noexcept(noexcept(std::forward<TFn>(fn)())) {
    if (draining_ || !empty()) {
      return false;
    }

    draining_ = true;
    std::forward<TFn>(fn)();
    drain_pending();
    draining_ = false;
    return true;
  }

  template <class TFn>
  void schedule(TFn&& fn) noexcept {
    if (try_run_immediate(std::forward<TFn>(fn))) {
      return;
    }

    enqueue(std::forward<TFn>(fn));
    if (draining_) {
      return;
    }

    // GCOVR_EXCL_START
    // Queue non-empty with !draining_ cannot happen under the single-threaded contract.
    draining_ = true;
    drain_pending();
    draining_ = false;
    // GCOVR_EXCL_STOP
  }

 private:
  struct task_slot {
    using invoke_fn = void (*)(void*) noexcept;
    using destroy_fn = void (*)(void*) noexcept;

    alignas(std::max_align_t) std::array<unsigned char, InlineTaskBytes> storage{};
    invoke_fn invoke = nullptr;
    destroy_fn destroy = nullptr;

    template <class TFn>
    void set(TFn&& fn) noexcept {
      using fn_t = typename std::decay<TFn>::type;
      static_assert(sizeof(fn_t) <= InlineTaskBytes, "scheduled task exceeds inline storage capacity");
      static_assert(alignof(fn_t) <= alignof(std::max_align_t), "scheduled task alignment exceeds scheduler storage alignment");

      new (storage.data()) fn_t(std::forward<TFn>(fn));
      invoke = [](void* ptr) noexcept { (*static_cast<fn_t*>(ptr))(); };
      destroy = [](void* ptr) noexcept { static_cast<fn_t*>(ptr)->~fn_t(); };
    }

    void run() noexcept {
      invoke(storage.data());
      destroy(storage.data());
      invoke = nullptr;
      destroy = nullptr;
    }

    void reset() noexcept {  // GCOVR_EXCL_LINE
      // GCOVR_EXCL_START
      if (destroy != nullptr) {
        destroy(storage.data());
      }
      invoke = nullptr;
      destroy = nullptr;
      // GCOVR_EXCL_STOP
    }  // GCOVR_EXCL_LINE
  };

  static constexpr std::size_t next(const std::size_t index) noexcept { return (index + 1) & (Capacity - 1); }

  bool empty() const noexcept { return head_ == tail_; }
  bool full() const noexcept { return next(tail_) == head_; }

  template <class TFn>
  void enqueue(TFn&& fn) noexcept {
    if (full()) {
      std::terminate();  // GCOVR_EXCL_LINE
    }

    tasks_[tail_].set(std::forward<TFn>(fn));
    tail_ = next(tail_);
  }

  void drain_pending() noexcept {
    while (!empty()) {
      task_slot& task = tasks_[head_];
      head_ = next(head_);
      task.run();
    }
  }

  void clear() noexcept {
    // GCOVR_EXCL_START
    while (!empty()) {
      tasks_[head_].reset();
      head_ = next(head_);
    }
    // GCOVR_EXCL_STOP
    tail_ = head_;
    draining_ = false;
  }

  std::array<task_slot, Capacity> tasks_{};
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  bool draining_ = false;
};

template <class TScheduler>
struct coroutine_scheduler {
  using scheduler_type = TScheduler;
};

struct heap_coroutine_allocator {
  void* allocate(const std::size_t size, const std::size_t alignment) {
    return ::operator new(size, std::align_val_t(alignment));
  }

  void deallocate(void* ptr, const std::size_t size, const std::size_t alignment) noexcept {
    ::operator delete(ptr, size, std::align_val_t(alignment));
  }
};

template <std::size_t SlotSize = 1024, std::size_t SlotCount = 64>
class pooled_coroutine_allocator {
 public:
  static_assert(SlotSize > 0, "pooled_coroutine_allocator slot size must be non-zero");
  static_assert(SlotCount > 0, "pooled_coroutine_allocator slot count must be non-zero");

  pooled_coroutine_allocator() noexcept { reset_freelist(); }

  void* allocate(const std::size_t size, const std::size_t alignment) {
    if (size <= SlotSize && alignment <= alignof(pool_slot) && free_head_ != invalid_index) {
      const std::size_t slot_index = free_head_;
      free_head_ = next_free_[slot_index];
      return static_cast<void*>(slots_[slot_index].storage.data());
    }

    return ::operator new(size, std::align_val_t(alignment));
  }

  void deallocate(void* ptr, const std::size_t size, const std::size_t alignment) noexcept {
    if (ptr == nullptr) {
      return;
    }

    if (size <= SlotSize && alignment <= alignof(pool_slot) && is_pool_pointer(ptr)) {
      const std::size_t slot_index = slot_index_for(ptr);
      next_free_[slot_index] = free_head_;
      free_head_ = slot_index;
      return;
    }

    ::operator delete(ptr, size, std::align_val_t(alignment));
  }

 private:
  static constexpr std::size_t invalid_index = SlotCount;

  struct pool_slot {
    alignas(std::max_align_t) std::array<unsigned char, SlotSize> storage{};
  };

  bool is_pool_pointer(void* ptr) const noexcept {
    const auto* begin = reinterpret_cast<const unsigned char*>(slots_.data());
    const auto* end = begin + sizeof(slots_);
    const auto* candidate = static_cast<const unsigned char*>(ptr);
    if (candidate < begin || candidate >= end) {
      return false;
    }

    const std::size_t offset = static_cast<std::size_t>(candidate - begin);
    return (offset % sizeof(pool_slot)) == 0;
  }

  std::size_t slot_index_for(void* ptr) const noexcept {
    const auto* begin = reinterpret_cast<const unsigned char*>(slots_.data());
    const auto* candidate = static_cast<const unsigned char*>(ptr);
    const std::size_t offset = static_cast<std::size_t>(candidate - begin);
    return offset / sizeof(pool_slot);
  }

  void reset_freelist() noexcept {
    for (std::size_t i = 0; i + 1 < SlotCount; ++i) {
      next_free_[i] = i + 1;
    }
    next_free_[SlotCount - 1] = invalid_index;
    free_head_ = 0;
  }

  std::array<pool_slot, SlotCount> slots_{};
  std::array<std::size_t, SlotCount> next_free_{};
  std::size_t free_head_ = 0;
};

template <class TAllocator>
struct coroutine_allocator {
  using allocator_type = TAllocator;
};

template <class TSchedulerPolicy>
concept valid_coroutine_scheduler_policy = requires { typename TSchedulerPolicy::scheduler_type; };

template <class TScheduler>
concept valid_coroutine_scheduler = requires(TScheduler scheduler, void (*fn_ptr)()) { scheduler.schedule(fn_ptr); };

template <class TScheduler>
concept strict_ordering_scheduler_contract =
    requires {
      { TScheduler::guarantees_fifo } -> std::convertible_to<bool>;
      { TScheduler::single_consumer } -> std::convertible_to<bool>;
      { TScheduler::run_to_completion } -> std::convertible_to<bool>;
    } && static_cast<bool>(TScheduler::guarantees_fifo) && static_cast<bool>(TScheduler::single_consumer) &&
    static_cast<bool>(TScheduler::run_to_completion);

template <class TScheduler>
concept has_try_run_immediate = requires(TScheduler scheduler) {
  {
    scheduler.try_run_immediate(+[]() noexcept {})
  } -> std::same_as<bool>;
};

template <class TAllocatorPolicy>
concept valid_coroutine_allocator_policy = requires { typename TAllocatorPolicy::allocator_type; };

template <class TAllocator>
concept valid_coroutine_allocator = requires(TAllocator allocator, void* ptr, std::size_t size, std::size_t alignment) {
  { allocator.allocate(size, alignment) } -> std::same_as<void*>;
  { allocator.deallocate(ptr, size, alignment) } noexcept;
};

}  // namespace policy

class bool_task {
 public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  struct promise_type {
    using allocate_fn = void* (*)(void*, std::size_t, std::size_t);
    using deallocate_fn = void (*)(void*, void*, std::size_t, std::size_t) noexcept;

    struct frame_header {
      void* allocator_ctx = nullptr;
      deallocate_fn deallocate = nullptr;
      void* allocation_ptr = nullptr;
      std::size_t allocation_size = 0;
      std::size_t allocation_alignment = 0;
    };

    static void* allocate_frame(const std::size_t frame_size, void* allocator_ctx, allocate_fn allocate,
                                deallocate_fn deallocate) {
      constexpr std::size_t frame_align = alignof(promise_type);
      constexpr std::size_t header_align = alignof(frame_header);
      constexpr std::size_t alloc_align = frame_align > header_align ? frame_align : header_align;

      const std::size_t allocation_size = frame_size + sizeof(frame_header) + alloc_align - 1;
      void* raw = allocate(allocator_ctx, allocation_size, alloc_align);
      if (raw == nullptr) {
#if !BOOST_SML_DISABLE_EXCEPTIONS
        throw std::bad_alloc();
#else
        std::terminate();
#endif
      }

      void* aligned_frame = static_cast<unsigned char*>(raw) + sizeof(frame_header);
      std::size_t space = allocation_size - sizeof(frame_header);
      // GCOVR_EXCL_START
      if (std::align(frame_align, frame_size, aligned_frame, space) == nullptr) {
        deallocate(allocator_ctx, raw, allocation_size, alloc_align);
#if !BOOST_SML_DISABLE_EXCEPTIONS
        throw std::bad_alloc();
#else
        std::terminate();
#endif
      }
      // GCOVR_EXCL_STOP

      auto* header = reinterpret_cast<frame_header*>(static_cast<unsigned char*>(aligned_frame) - sizeof(frame_header));
      header->allocator_ctx = allocator_ctx;
      header->deallocate = deallocate;
      header->allocation_ptr = raw;
      header->allocation_size = allocation_size;
      header->allocation_alignment = alloc_align;
      return aligned_frame;
    }

    template <class TAllocator>
    static void* allocate_frame_with_allocator(std::size_t frame_size, TAllocator& allocator) {
      static_assert(policy::valid_coroutine_allocator<TAllocator>,
                    "coroutine allocator must provide allocate(size, alignment) and noexcept deallocate(ptr, size, alignment)");

      return allocate_frame(
          frame_size, &allocator,
          [](void* ctx, const std::size_t size, const std::size_t alignment) -> void* {
            return static_cast<TAllocator*>(ctx)->allocate(size, alignment);
          },
          [](void* ctx, void* ptr, const std::size_t size, const std::size_t alignment) noexcept {
            static_cast<TAllocator*>(ctx)->deallocate(ptr, size, alignment);
          });
    }

    static void deallocate_frame(void* frame_ptr) noexcept {
      if (frame_ptr == nullptr) {
        return;
      }

      auto* header = reinterpret_cast<frame_header*>(static_cast<unsigned char*>(frame_ptr) - sizeof(frame_header));
      header->deallocate(header->allocator_ctx, header->allocation_ptr, header->allocation_size, header->allocation_alignment);
    }

    static void* operator new(std::size_t frame_size) {
      return allocate_frame(
          frame_size, nullptr,
          [](void*, const std::size_t size, const std::size_t alignment) -> void* {
            return ::operator new(size, std::align_val_t(alignment));
          },
          [](void*, void* ptr, const std::size_t size, const std::size_t alignment) noexcept {
            ::operator delete(ptr, size, std::align_val_t(alignment));
          });
    }

    template <class TAllocator, class... TArgs>
    static void* operator new(std::size_t frame_size, std::allocator_arg_t, TAllocator& allocator, TArgs&&...) {
      return allocate_frame_with_allocator(frame_size, allocator);
    }

    static void operator delete(void* frame_ptr) noexcept { deallocate_frame(frame_ptr); }
    static void operator delete(void* frame_ptr, std::size_t) noexcept { deallocate_frame(frame_ptr); }

    template <class TAllocator, class... TArgs>
    static void operator delete(void* frame_ptr, std::allocator_arg_t, TAllocator&, TArgs&&...) noexcept {
      deallocate_frame(frame_ptr);
    }

    bool value = false;
    std::exception_ptr exception = nullptr;
    std::coroutine_handle<> continuation = std::noop_coroutine();

    bool_task get_return_object() noexcept { return bool_task{handle_type::from_promise(*this)}; }
    std::suspend_never initial_suspend() noexcept { return {}; }

    auto final_suspend() noexcept {
      struct continuation_awaiter {
        bool await_ready() const noexcept { return false; }
        std::coroutine_handle<> await_suspend(handle_type h) const noexcept { return h.promise().continuation; }
        void await_resume() const noexcept {}
      };
      return continuation_awaiter{};
    }

    void return_value(const bool v) noexcept { value = v; }
    void unhandled_exception() noexcept { exception = std::current_exception(); }
  };

  bool_task() = default;
  explicit bool_task(handle_type handle) noexcept : handle_(handle) {}

  static bool_task from_value(const bool value) noexcept {
    bool_task task{};
    task.has_immediate_value_ = true;
    task.immediate_value_ = value;
    return task;
  }

  ~bool_task() {
    if (handle_) {
      handle_.destroy();
    }
  }

  bool_task(const bool_task&) = delete;
  bool_task& operator=(const bool_task&) = delete;

  bool_task(bool_task&& other) noexcept
      : handle_(std::exchange(other.handle_, {})),
        has_immediate_value_(other.has_immediate_value_),
        immediate_value_(other.immediate_value_) {
    other.has_immediate_value_ = false;
    other.immediate_value_ = false;
  }

  bool_task& operator=(bool_task&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    if (handle_) {
      handle_.destroy();
    }
    handle_ = std::exchange(other.handle_, {});
    has_immediate_value_ = other.has_immediate_value_;
    immediate_value_ = other.immediate_value_;
    other.has_immediate_value_ = false;
    other.immediate_value_ = false;
    return *this;
  }

  bool await_ready() const noexcept {
    if (has_immediate_value_) {
      return true;
    }
    return (handle_ == nullptr) || handle_.done();
  }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
    if (handle_ == nullptr) {
      return std::noop_coroutine();
    }
    handle_.promise().continuation = awaiting;
    return handle_;
  }

  bool await_resume() { return result(); }

  bool result() {
    if (has_immediate_value_) {
      return immediate_value_;
    }
    if (handle_ == nullptr) {
      return false;
    }
    if (!handle_.done()) {
#if !BOOST_SML_DISABLE_EXCEPTIONS
      throw std::logic_error("bool_task result() called before coroutine completion");
#else
      std::terminate();
#endif
    }
    if (handle_.promise().exception) {
#if !BOOST_SML_DISABLE_EXCEPTIONS
      std::rethrow_exception(handle_.promise().exception);
#else
      std::terminate();
#endif
    }
    return handle_.promise().value;
  }

 private:
  handle_type handle_{};
  bool has_immediate_value_ = false;
  bool immediate_value_ = false;
};

template <class T, class TSchedulerPolicy = policy::coroutine_scheduler<policy::fifo_scheduler<>>,
          class TAllocatorPolicy = policy::coroutine_allocator<policy::pooled_coroutine_allocator<>>, class... TPolicies>
class co_sm {
 public:
  static_assert(policy::valid_coroutine_scheduler_policy<TSchedulerPolicy>, "scheduler_policy must define scheduler_type");
  static_assert(policy::valid_coroutine_allocator_policy<TAllocatorPolicy>, "allocator_policy must define allocator_type");

  using model_type = T;
  using scheduler_policy_type = TSchedulerPolicy;
  using scheduler_type = typename scheduler_policy_type::scheduler_type;
  using allocator_policy_type = TAllocatorPolicy;
  using allocator_type = typename allocator_policy_type::allocator_type;
  using state_machine_type = sm<T, TPolicies...>;

  static_assert(policy::valid_coroutine_scheduler<scheduler_type>, "scheduler_type must provide schedule(fn)");
  static_assert(policy::strict_ordering_scheduler_contract<scheduler_type>,
                "scheduler_type must guarantee FIFO ordering, single-consumer dispatch, and run-to-completion");
  static_assert(policy::valid_coroutine_allocator<allocator_type>,
                "allocator_type must provide allocate(size, alignment) and noexcept deallocate(ptr, size, alignment)");

  co_sm() = default;
  ~co_sm() = default;

  co_sm(const co_sm&) = default;
  co_sm(co_sm&&) = default;
  co_sm& operator=(const co_sm&) = default;
  co_sm& operator=(co_sm&&) = default;

  explicit co_sm(const scheduler_type& scheduler) : scheduler_(scheduler) {}
  explicit co_sm(const allocator_type& allocator) : allocator_(allocator) {}

  co_sm(const scheduler_type& scheduler, const allocator_type& allocator) : scheduler_(scheduler), allocator_(allocator) {}

  template <class... TArgs>
  explicit co_sm(TArgs&&... args) : state_machine_(std::forward<TArgs>(args)...) {}

  template <class... TArgs>
  co_sm(const scheduler_type& scheduler, TArgs&&... args)
      : state_machine_(std::forward<TArgs>(args)...), scheduler_(scheduler) {}

  template <class... TArgs>
  co_sm(const allocator_type& allocator, TArgs&&... args)
      : state_machine_(std::forward<TArgs>(args)...), allocator_(allocator) {}

  template <class... TArgs>
  co_sm(const scheduler_type& scheduler, const allocator_type& allocator, TArgs&&... args)
      : state_machine_(std::forward<TArgs>(args)...), scheduler_(scheduler), allocator_(allocator) {}

  template <class TEvent>
  bool process_event(const TEvent& event) {
    return state_machine_.process_event(event);
  }

  template <class TEvent>
  bool_task process_event_async(TEvent&& event) {
    using event_t = typename std::decay<TEvent>::type;
    event_t event_copy(static_cast<TEvent&&>(event));

    if constexpr (std::is_same_v<scheduler_type, policy::inline_scheduler>) {
      return bool_task::from_value(state_machine_.process_event(event_copy));
    }

    if constexpr (policy::has_try_run_immediate<scheduler_type>) {
      bool accepted = false;
      if (scheduler_.try_run_immediate(
              [this, &event_copy, &accepted]() { accepted = state_machine_.process_event(event_copy); })) {
        return bool_task::from_value(accepted);
      }
    }

    return process_event_async_impl(std::allocator_arg, allocator_, *this, static_cast<event_t&&>(event_copy));
  }

  template <class TState>
  bool is(const TState& state = TState{}) const {
    return state_machine_.is(state);
  }

  template <class TVisitor>
  void visit_current_states(TVisitor&& visitor) {
    state_machine_.visit_current_states(std::forward<TVisitor>(visitor));
  }

  scheduler_type& scheduler() noexcept { return scheduler_; }
  const scheduler_type& scheduler() const noexcept { return scheduler_; }
  allocator_type& allocator() noexcept { return allocator_; }
  const allocator_type& allocator() const noexcept { return allocator_; }

 protected:
  state_machine_type& raw_sm() { return state_machine_; }
  const state_machine_type& raw_sm() const { return state_machine_; }

 private:
  template <class TEvent>
  static bool_task process_event_async_impl(std::allocator_arg_t, allocator_type& allocator, co_sm& self, TEvent&& event) {
    (void)allocator;
    co_return co_await process_event_awaitable<typename std::decay<TEvent>::type>{self, static_cast<TEvent&&>(event)};
  }  // GCOVR_EXCL_LINE

  template <class TEvent>
  struct process_event_awaitable {
    co_sm& self;
    TEvent event_value;
    bool accepted = false;

    bool await_ready() noexcept {
      if constexpr (std::is_same_v<scheduler_type, policy::inline_scheduler>) {
        accepted = self.state_machine_.process_event(event_value);
        return true;
      }
      return false;
    }

    void await_suspend(std::coroutine_handle<> handle) {
      self.scheduler_.schedule([this, handle]() mutable {
        accepted = self.state_machine_.process_event(event_value);
        handle.resume();
      });
    }

    bool await_resume() const noexcept { return accepted; }
  };

  state_machine_type state_machine_{};
  scheduler_type scheduler_{};
  allocator_type allocator_{};
};

}  // namespace utility

BOOST_SML_NAMESPACE_END
#endif

#undef BOOST_SML_UTILITY_CO_SM_LANG

#endif  // BOOST_SML_UTILITY_CO_SM_HPP
