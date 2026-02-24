//
// Copyright (c) 2016-2020 Kris Jusiak (kris at jusiak dot net)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#include <boost/sml.hpp>
#include <deque>
#include <iostream>
#include <queue>
#include <string>

#include "static_deque.h"
#include "static_queue.h"

namespace sml = boost::sml;

struct e1 {};
struct e2 {};
struct e3 {};

const auto s1 = sml::state<class s1>;
const auto s2 = sml::state<class s2>;
const auto s3 = sml::state<class s3>;
const auto s4 = sml::state<class s4>;
const auto s5 = sml::state<class s5>;
const auto s6 = sml::state<class s6>;
const auto s7 = sml::state<class s7>;

test mix_process_n_defer_at_init = [] {
  struct c {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        * s1 + on_entry<sml::initial> / process(e1{})
        , s1 + event<e1> / defer = s2
        , s2 / defer = s3
        , s3 + event<e1> / process(e2{})
        , s3 + event<e2> / defer = s4
        , s4 + event<e2> = s5
        , s5 = s6
        , s6 + on_entry<_> / process(e3{})
        , s6 + event<e3> = s7
        , s7 = X
      );
      // clang-format on
    }
  };

  sml::sm<c, sml::process_queue<std::queue>, sml::defer_queue<std::deque>> sm{};
  expect(sm.is(sml::X));
};

test mix_process_n_defer = [] {
  struct c {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        * s1 + event<e1> / defer = s2
        , s2 / defer = s3
        , s3 + event<e1> / process(e2{})
        , s3 + event<e2> / defer = s4
        , s4 + event<e2> = s5
        , s5 = s6
        , s6 + on_entry<_> / (process(e1{}), process(e3{}))  // e1 is not handled
        , s6 + event<e3> = s7
        , s7 = X
      );
      // clang-format on
    }
  };  // internal, defer, process, defer, internal, process, internal
  sml::sm<c, sml::process_queue<std::queue>, sml::defer_queue<std::deque>> sm{};
  expect(!sm.process_event(e1{}));
  expect(sm.is(sml::X));
};

test process_n_defer_again = [] {
  struct sub {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        * s2 + event<e1> / defer
        , s2 + event<e3> / defer
        , s2 + event<e2> = s3
        , s3 + on_entry<_> / [](std::string & calls){calls+="|s3_entry";}
        , s3 + event<e1> / [](std::string & calls){calls+="|e1";}
      );
      // clang-format on
    }
  };

  struct c {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        * s1 = state<sub>
        // Check that deferred events are only propagated inside sub state machine.
        , state<sub> + event<e3> / [](std::string & calls){calls+="|e3";}
      );
      // clang-format on
    }
  };

  std::string calls;
  sml::sm<c, sml::process_queue<std::queue>, sml::defer_queue<std::deque>> sm{calls};
  expect(sm.process_event(e1{}));
  expect(sm.process_event(e1{}));
  expect(sm.process_event(e1{}));
  expect(sm.process_event(e3{}));
  expect(calls == "");
  sm.process_event(e2{});
  std::cout << calls << "\n";
  expect(calls == "|s3_entry|e1|e1|e1");
};

test process_queue_runs_completion_for_popped_event_type = [] {
  struct trigger {};
  struct queued1 {};
  struct queued2 {};
  struct q0 {};
  struct q1 {};
  struct q2 {};
  struct q3 {};
  struct done {};
  struct wrong {};

  struct c {
    auto operator()() const {
      using namespace sml;
      auto q0_state = state<q0>;
      auto q1_state = state<q1>;
      auto q2_state = state<q2>;
      auto q3_state = state<q3>;
      auto done_state = state<done>;
      auto wrong_state = state<wrong>;
      // clang-format off
      return make_transition_table(
        *q0_state + event<trigger> / (process(queued1{}), process(queued2{})) = q1_state
        , q1_state + event<queued1> = q2_state
        , q2_state + completion<queued1> = q3_state
        , q2_state + event<queued2> = wrong_state
        , q3_state + event<queued2> = done_state
      );
      // clang-format on
    }
  };

  sml::sm<c, sml::process_queue<std::queue>> sm{};
  expect(sm.process_event(trigger{}));
  expect(sm.is(sml::state<done>));
  expect(!sm.is(sml::state<wrong>));
};

test defer_queue_runs_completion_for_popped_event_type = [] {
  struct deferred {};
  struct release {};
  struct d0 {};
  struct d1 {};
  struct d2 {};
  struct done {};
  struct wrong {};

  struct c {
    auto operator()() const {
      using namespace sml;
      auto d0_state = state<d0>;
      auto d1_state = state<d1>;
      auto d2_state = state<d2>;
      auto done_state = state<done>;
      auto wrong_state = state<wrong>;
      // clang-format off
      return make_transition_table(
        *d0_state + event<deferred> / defer
        , d0_state + event<release> = d1_state
        , d1_state + event<deferred> = d2_state
        , d2_state + completion<deferred> = done_state
        , d2_state + completion<release> = wrong_state
      );
      // clang-format on
    }
  };

  sml::sm<c, sml::process_queue<std::queue>, sml::defer_queue<std::deque>> sm{};
  expect(sm.process_event(deferred{}));
  expect(sm.process_event(release{}));
  expect(sm.is(sml::state<done>));
  expect(!sm.is(sml::state<wrong>));
};

template <typename T>
using MinimalStaticDeque10 = MinimalStaticDeque<T, 10>;

template <typename T>
using MinimalStaticQueue10 = MinimalStaticQueue<T, 10>;

test mix_process_n_defer_at_init_static_queue = [] {
  struct c {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        * s1 + on_entry<sml::initial> / process(e1{})
        , s1 + event<e1> / defer = s2
        , s2 / defer = s3
        , s3 + event<e1> / process(e2{})
        , s3 + event<e2> / defer = s4
        , s4 + event<e2> = s5
        , s5 = s6
        , s6 + on_entry<_> / process(e3{})
        , s6 + event<e3> = s7
        , s7 = X
      );
      // clang-format on
    }
  };

  sml::sm<c, sml::process_queue<MinimalStaticQueue10>, sml::defer_queue<MinimalStaticDeque10>> sm{};
  expect(sm.is(sml::X));
};
