<a href="http://www.boost.org/LICENSE_1_0.txt" target="_blank">![Boost Licence](http://img.shields.io/badge/license-boost-blue.svg)</a>
<a href="https://github.com/stateforward/sml.cpp/releases" target="_blank">![Version](https://img.shields.io/github/v/release/stateforward/sml.cpp)</a>
<a href="https://github.com/stateforward/sml.cpp/actions/workflows/quality_gates.yml" target="_blank">![Quality Gates](https://github.com/stateforward/sml.cpp/actions/workflows/quality_gates.yml/badge.svg)</a>

---------------------------------------

# SML (State Machine Language) - stateforward fork

> A C++14 **header-only** State Machine Library with no dependencies, forked from [boost-ext/sml](https://github.com/boost-ext/sml) (v1.1.13) with bug fixes, new utilities, and production-grade quality gates.

---

## What's different from upstream

This fork addresses critical bugs, adds utilities for high-throughput and async state machine workloads, and introduces a full CI quality pipeline.

### Bug fixes

| Issue | Description |
|-------|-------------|
| [#171](https://github.com/boost-ext/sml/issues/171) | Wildcard `event<_>` handlers fired twice per event |
| [#253](https://github.com/boost-ext/sml/issues/253) | Nested sub-SM exit leaked deferred event queue state |
| [#400](https://github.com/boost-ext/sml/issues/400) | Terminal state propagation |
| -- | `unexpected_event<>` catch-all dispatch failed to route correctly |
| -- | Undefined behavior in `zero_wrapper` / `always` / `none` under ASAN/UBSAN |
| -- | `__has_feature` usage broke compilation on GCC and other non-clang compilers |

### New features

- **Completion transitions** (`completion<T>`) -- post-event transitions with origin event propagation
- **Coroutine state machines** (`co_sm`) -- C++20 coroutine-driven SM with configurable scheduler and allocator policies
- **`sm_pool`** -- pool-based container for managing thousands of SM instances with indexed and batch dispatch
- **`dispatch_table`** -- compile-time dispatch table for ID-based event routing at zero runtime overhead

### Quality gates (CI + local)

Automated pipeline covering clang-format, clang-tidy, code coverage (90% minimum), ASAN/UBSAN sanitizers, no-exceptions build, and benchmark smoke tests.

---

## Quick start

### Download

> Requires only one file. Get the latest header from [`include/boost/sml.hpp`](include/boost/sml.hpp).

### Include

```cpp
#include <boost/sml.hpp>
namespace sml = boost::sml;
```

### Define events, guards, and actions

```cpp
struct release {};
struct ack { bool valid{}; };
struct fin { int id{}; bool valid{}; };
struct timeout {};

struct sender {
  template<class TMsg>
  constexpr void send(const TMsg& msg) { std::printf("send: %d\n", msg.id); }
};

constexpr auto is_valid = [](const auto& event) { return event.valid; };
constexpr auto send_fin = [](sender& s) { s.send(fin{0}); };
constexpr auto send_ack = [](const auto& event, sender& s) { s.send(event); };
```

### Create a state machine

```cpp
struct tcp_release {
  auto operator()() const {
    using namespace sml;
    return make_transition_table(
      *"established"_s + event<release>          / send_fin  = "fin wait 1"_s,
       "fin wait 1"_s  + event<ack> [ is_valid ]             = "fin wait 2"_s,
       "fin wait 2"_s  + event<fin> [ is_valid ] / send_ack  = "timed wait"_s,
       "timed wait"_s  + event<timeout>                      = X
    );
  }
};
```

### Use it

```cpp
int main() {
  using namespace sml;

  sender s{};
  sm<tcp_release> sm{s};
  assert(sm.is("established"_s));

  sm.process_event(release{});
  assert(sm.is("fin wait 1"_s));

  sm.process_event(ack{true});
  assert(sm.is("fin wait 2"_s));

  sm.process_event(fin{42, true});
  assert(sm.is("timed wait"_s));

  sm.process_event(timeout{});
  assert(sm.is(X));
}
```

### Compile

```sh
# GCC/Clang
$CXX -std=c++14 -O2 -fno-exceptions -Wall -Wextra -Werror -pedantic tcp_release.cpp

# MSVC
cl /std:c++14 /Ox /W3 tcp_release.cpp
```

> **MSVC-2015 note:** use `state<class state_name>` instead of `"state_name"_s`, and explicitly state lambda result types: `auto action = [] -> void {}`.

---

## Utilities

### co_sm (C++20)

Coroutine-driven state machine wrapper with configurable scheduling and allocation policies. Requires C++20 with coroutine support.

```cpp
#include <boost/sml/utility/co_sm.hpp>

namespace utility = sml::utility;

// Default: inline scheduler, pooled coroutine allocator
utility::co_sm<my_fsm> sm{};

// Synchronous dispatch (same as regular sm)
sm.process_event(my_event{});

// Asynchronous dispatch via coroutine
auto task = sm.process_event_async(my_event{});
bool accepted = task.result();
```

**Scheduler policies:**

| Policy | Description |
|--------|-------------|
| `inline_scheduler` | Runs tasks immediately (default) |
| `fifo_scheduler<Capacity, InlineBytes>` | FIFO queue with bounded inline storage |
| `coroutine_scheduler<TScheduler>` | Wraps any scheduler for the coroutine path |

**Allocator policies:**

| Policy | Description |
|--------|-------------|
| `pooled_coroutine_allocator<Size, Count>` | Fixed-size pool with heap fallback |

### sm_pool

A pool container for managing large numbers of state machine instances with efficient indexed and batch dispatch.

```cpp
#include <boost/sml/utility/sm_pool.hpp>

using namespace sml::utility;

// Create a pool of 10,000 state machines
sm_pool<std::vector<State>, MyFSM> pool(10000);

// Process event for a single instance
pool.process_indexed(42, MyEvent{});

// Batch: same event to multiple instances
std::vector<size_t> ids{1, 3, 5, 7};
size_t handled = pool.process_indexed_batch(ids, MyEvent{});

// Batch: heterogeneous indexed events
std::vector<indexed_event<MyEvent>> events{
  with_id(0, MyEvent{}),
  with_id(5, MyEvent{}),
};
pool.process_event_batch(events);
```

**Key API:**

| Method | Description |
|--------|-------------|
| `process_indexed(id, event)` | Dispatch event to a single instance by index |
| `process_indexed_batch(range, event)` | Dispatch the same event to multiple instances |
| `process_event_batch(range)` | Dispatch heterogeneous `indexed_event`s |
| `storage()` | Access the underlying storage container |
| `reset()` | Reset all instances |

### dispatch_table

Compile-time dispatch table for routing runtime event IDs to typed event handlers.

```cpp
#include <boost/sml/utility/dispatch_table.hpp>

struct event1 { static constexpr int id = 1; };
struct event2 { static constexpr int id = 2; };
struct event3 { static constexpr int id = 3; };

auto dispatch = sml::utility::make_dispatch_table<event1, 1, 4>(my_sm);
dispatch(raw_event, runtime_id);  // Routes to correct handler at O(1)
```

---

## Completion transitions

Completion transitions fire after an event has been fully processed, carrying the origin event forward. This enables post-processing or chained transitions that depend on which event caused the state change.

```cpp
struct my_fsm {
  auto operator()() const {
    using namespace sml;
    return make_transition_table(
      *"idle"_s   + event<e1>          = "step1"_s,
       "step1"_s  + completion<e1>     = "step2"_s   // fires after e1 completes
    );
  }
};
```

The `completion<TEvent>` transition only fires when `TEvent` was the origin event. The original event data is accessible in guards and actions.

---

## Unexpected event handling

Handle events that have no normal transition in the current state, enabling explicit error paths instead of silent drops.

```cpp
struct error_handler {
  auto operator()() const {
    using namespace sml;
    return make_transition_table(
      *"idle"_s + event<e1>                        = "running"_s,
       "idle"_s + unexpected_event<e2>              = "error"_s,    // specific event
       "idle"_s + unexpected_event<_>               = "error"_s,    // wildcard: any unhandled event
       "error"_s + event<reset>                     = "idle"_s
    );
  }
};
```

- `unexpected_event<TEvent>` fires only when no normal transition exists for `TEvent` in the current state
- `unexpected_event<_>` catches any unhandled event (wildcard)
- Supports guards and actions like normal transitions

---

## Benchmarks

The benchmark suite covers compilation time, execution throughput, memory footprint, and cache locality across multiple implementations.

### Complex test (50 states, 50 events)

|                      | Enum/Switch | std::variant | **SML** | Boost.MSM-eUML | Boost.Statechart |
|----------------------|-------------|--------------|---------|-----------------|------------------|
| **Compilation time** | 0.132s      | 15.321s      | 0.582s  | 1m15.935s       | 5.671s           |
| **Execution time**   | 679ms       | 827ms        | 622ms   | 664ms           | 2282ms           |
| **Memory usage**     | 1b          | 2b/8b        | 1b      | 120b            | 224b             |
| **Executable size**  | 15K         | 187K         | 34K     | 611K            | 211K             |

### Pool dispatch (10k actors, 50k events)

| Pattern | Throughput |
|---------|-----------|
| Direct array baseline | ~0.425 ns/event |
| `sm_pool` batch (sequential) | ~0.472 ns/event |
| `sm_pool` batch (random access) | ~0.472 ns/event |

### Build benchmarks

```sh
cmake -B build -DSML_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Cross-language comparison

```sh
./benchmark/compare_player.sh  # SML (C++) vs smlang (Rust)
```

---

## Quality gates

Run the full quality pipeline locally before pushing:

```sh
./scripts/quality_gates.sh
```

**What it checks:**
- **clang-format** -- code style consistency
- **clang-tidy** -- static analysis
- **Code coverage** -- 90% minimum threshold (gcov/lcov)
- **Sanitizers** -- ASAN, UBSAN (with `--experimental-sanitizers` for TSan/MSan)
- **No-exceptions build** -- verifies `-fno-exceptions` compilation
- **Benchmark smoke test** -- ensures benchmark targets build

**Options:**

```sh
./scripts/quality_gates.sh --help
./scripts/quality_gates.sh --skip-benchmarks --skip-sanitizers
./scripts/quality_gates.sh --coverage-min 85 --jobs 8
```

The same checks run automatically in CI via [GitHub Actions](.github/workflows/quality_gates.yml) on every push and pull request.

---

## ASM output (tcp_release)

```asm
main:
  pushq %rax
  movl $.L.str, %edi
  xorl %esi, %esi
  xorl %eax, %eax
  callq printf
  movl $.L.str, %edi
  movl $42, %esi
  xorl %eax, %eax
  callq printf
  xorl %eax, %eax
  popq %rcx
  retq
.L.str:
  .asciz "send: %d\n"
```

> The entire TCP release example compiles down to two `printf` calls. [See it on Godbolt.](https://godbolt.org/z/y99L50)

---

## Documentation

Full upstream documentation remains applicable:

* [Introduction](https://boost-ext.github.io/sml/index.html)
* [Overview](https://boost-ext.github.io/sml/overview.html) -- dependencies, compilers, configuration, thread safety
* [Tutorial/Workshop](https://boost-ext.github.io/sml/tutorial.html)
* [UML vs SML](https://boost-ext.github.io/sml/uml_vs_sml.html)
* [User Guide](https://boost-ext.github.io/sml/user_guide.html)
* [Examples](https://boost-ext.github.io/sml/examples.html) -- hello world, composites, orthogonal regions, history, logging, testing
* [FAQ](https://boost-ext.github.io/sml/faq.html)
* [Changelog](https://boost-ext.github.io/sml/CHANGELOG.html)

---

## Building from source

```sh
cmake -B build -DCMAKE_CXX_STANDARD=14
cmake --build build
ctest --test-dir build
```

**CMake options:**

| Option | Default | Description |
|--------|---------|-------------|
| `SML_BUILD_BENCHMARKS` | OFF | Build benchmark targets |
| `SML_BUILD_EXAMPLES` | OFF | Build example targets |
| `SML_BUILD_TESTS` | OFF | Build test targets |

**Supported compilers:** GCC 6+, Clang 3.5+, MSVC 2015+. Requires C++14 minimum (C++20 for `co_sm`).

---

## Upstream

This is a fork of [boost-ext/sml](https://github.com/boost-ext/sml). Original work by Kris Jusiak and contributors.

## License

[Boost Software License 1.0](http://www.boost.org/LICENSE_1_0.txt)
