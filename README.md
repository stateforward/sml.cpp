# SML (State Machine Language) - StateForward Fork

A C++14 single-header state machine library. This is an actively maintained fork of [Boost.ext SML](https://github.com/boost-ext/sml) with bug fixes and new features.

## What this fork adds

On top of upstream SML, this fork includes:

- **Completion transitions** (`completion<T>`) for post-event transitions with origin event propagation
- **Coroutine state machines** (`co_sm`) with configurable policies
- **`sm_pool`** utility with batch dispatch and locality-optimized benchmarks
- **Generic pooled dispatch** with batch optimization
- **Bug fixes** for upstream issues:
  - #171: wildcard event double invocation
  - #253: nested defer queue leak on sub-SM exit
  - #400: terminal state propagation
- **Quality gate script** (`./scripts/quality_gates.sh`) for end-to-end validation

## Quick start

```cpp
#include <boost/sml.hpp>
namespace sml = boost::sml;
```

### Events and guards

```cpp
struct release {};
struct ack { bool valid{}; };
struct fin { int id{}; bool valid{}; };
struct timeout {};

constexpr auto is_valid = [](const auto& event) { return event.valid; };
```

### Transition table

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

### Usage

```cpp
sender s{};
sml::sm<tcp_release> sm{s};

sm.process_event(release{});    // O(1) dispatch
sm.process_event(ack{true});    // prints 'send: 0'
sm.process_event(fin{42, true}); // prints 'send: 42'
sm.process_event(timeout{});
assert(sm.is(sml::X));
```

### Compile

```sh
$CXX -std=c++14 -O2 -fno-exceptions -Wall -Wextra -Werror -pedantic tcp_release.cpp
```

### Quality gates

```sh
./scripts/quality_gates.sh
./scripts/quality_gates.sh --skip-benchmarks --skip-sanitizers
```

## Documentation

Full SML documentation is available at the upstream project: [boost-ext.github.io/sml](https://boost-ext.github.io/sml/index.html)

## Upstream

This is a fork of [boost-ext/sml](https://github.com/boost-ext/sml). Original work by Kris Jusiak and contributors, licensed under the Boost Software License 1.0.

## License

[Boost Software License 1.0](http://www.boost.org/LICENSE_1_0.txt)
