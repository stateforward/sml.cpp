//
// Copyright (c) 2016-2020 Kris Jusiak (kris at jusiak dot net)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#include <boost/sml.hpp>

namespace sml = boost::sml;

struct context {};

struct e1 {};
struct e2 {};
struct e3 {};

auto idle = sml::state<class idle>;
auto error = sml::state<class error>;
auto s1 = sml::state<class s1>;
auto s2 = sml::state<class s2>;

struct exception1 {};
struct exception2 {};
struct exception_data {
  int value = 0;
};

#if !defined(_MSC_VER)
test exception_minimal = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> / [] { throw exception1{}; } = s1
        , s1 + exception<exception1> = X
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  sm.process_event(e1{});  // throws exception1
  expect(sm.is(sml::X));
};

test exception_no_transition = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> / [] { throw exception1{}; }
        , idle + exception<exception1> = X
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  sm.process_event(e1{});  // throws exception1
  expect(sm.is(sml::X));
};

test exception_data_minimal = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      auto guard = [](const exception_data& ex) { return ex.value == 42; };
      // clang-format on

      // clang-format off
      return make_transition_table(
         *idle + event<e1> / [] { throw exception_data{42}; } = s1
        , s1 + exception<exception_data> [ guard ] = X
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  sm.process_event(e1{});  // throws exception1
  expect(sm.is(sml::X));
};

test exception_many = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> / [] { throw exception2{}; } = s1
        , s1 + exception<exception1> = s2
        , s1 + exception<exception2> = X
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  sm.process_event(e1{});  // throws exception2
  expect(sm.is(sml::X));
};

test generic_exception_handler = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> / [] { throw int{}; } = s1
        , s1 + exception<exception1> = s2
        , s1 + exception<_> = X // generic handler
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  sm.process_event(e1{});  // throws int
  expect(sm.is(sml::X));
};

test generic_exception_handler_priority = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> / [] { throw exception1{}; } = s1
        , s1 + exception<exception1> = s2
        , s1 + exception<_> = X // generic handler
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  sm.process_event(e1{});  // throws exception1
  expect(sm.is(s2));
};

test exception_not_handled = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> / [] { throw exception1{}; } = s1
       , *error + exception<_> / [] {}
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  sm.process_event(e1{});
  expect(sm.is(s1));
};

test exception_orthogonal_handler = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
          *idle + event<e1> / [] { throw exception1{}; } = s1
        , *error + exception<exception1> = X
        , *error + exception<exception2> = X
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  sm.process_event(e1{});  // throws exception1
  expect(sm.is(s1, sml::X));
};

test exception_orthogonal_handler_generic_handler = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> / [] { throw exception1{}; } = s1
       , *error + exception<_> = X
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  sm.process_event(e1{});  // throws exception1
  expect(sm.is(s1, sml::X));
};

test exception_orthogonal_handler_generic_handler_with_exception = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
          *idle + event<e1> / [] { throw exception1{}; } = s1
        , *error + exception<exception1> / [] { throw exception2{}; }
        , *error + exception<_> = X
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  sm.process_event(e1{});  // throws exception1
  expect(sm.is(s1, sml::X));
};

test exception_and_unexpected_event = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + unexpected_event<e1> / [] { throw exception1{}; } = s1
       , *error + exception<_> / [] {}
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  sm.process_event(e1{});
  expect(sm.is(s1));
};

test exception_and_context = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
        return make_transition_table(
           *idle + event<e1> / [] { throw exception1{}; } = s1
          , s1 + exception<exception1> / [](const context &, context&, const auto &){} = X
        );
      // clang-format on
    }
  };

  context ctx{};
  sml::sm<c> sm{ctx};
  sm.process_event(e1{});  // throws exception1
  expect(sm.is(sml::X));
};

test propage_exception_if_no_handled = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
        return make_transition_table(
           *idle + event<e1> / [] { throw exception1{}; } = X
        );
      // clang-format on
    }
  };

  sml::sm<c> sm{};
  auto exception = false;
  try {
    sm.process_event(e1{});  // throws exception1
    // clang-format off
  } catch (const exception1&) {
    // clang-format on
    expect(sm.is(sml::X));
    exception = true;
  }

  expect(exception);
};
test exception_not_matched_in_child_reaches_parent = [] {
  struct child {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> / [] { throw exception1{}; } = s1
        , s1 + exception<exception2> = X
      );
      // clang-format on
    }
  };
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *state<child> + exception<exception1> = s2
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  expect(sm.process_event(e1{}));  // child throws exception1, child rows only catch exception2
  expect(sm.is(s2));
};

test exception_not_matched_rethrows = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> / [] { throw exception1{}; } = s1
        , s1 + exception<exception2> = X
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  auto rethrown = false;
  try {
    sm.process_event(e1{});
    // clang-format off
  } catch (const exception1&) {
    // clang-format on
    rethrown = true;
  }
  expect(rethrown);
  expect(sm.is(s1));
};

test exception_not_matched_in_child_or_parent_rethrows = [] {
  struct child {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> / [] { throw exception1{}; } = s1
        , s1 + exception<exception2> = X
      );
      // clang-format on
    }
  };
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *state<child> + exception<exception2> = s2
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  auto rethrown = false;
  try {
    sm.process_event(e1{});
    // clang-format off
  } catch (const exception1&) {
    // clang-format on
    rethrown = true;
  }
  expect(rethrown);
  expect(sm.is<decltype(sml::state<child>)>(s1));
};

test exception_typed_row_not_active_falls_back_to_generic = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> / [] { throw exception1{}; } = s1
        , s2 + exception<exception1> = X
        , s1 + exception<_> = s2
      );
      // clang-format on
    }
  };

  sml::sm<c> sm;
  expect(sm.process_event(e1{}));
  expect(sm.is(s2));
};

struct faults {
  int attempts = 0;
  int recoveries = 0;
};

// The anonymous transition's guard throws, so the machine stays in idle and the same step would fault again.
auto faulting_guard = [](faults& f) -> bool {
  if (++f.attempts > 8) {
    return false;
  }
  throw exception1{};
};

test exception_internal_row_ends_faulting_anonymous_step_at_start = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         (*idle) [ faulting_guard ] = s1
        , idle + exception<exception1> / [](faults &f) { ++f.recoveries; }
      );
      // clang-format on
    }
  };

  faults f{};
  sml::sm<c> sm{f};
  expect(1 == f.attempts);
  expect(1 == f.recoveries);
  expect(sm.is(idle));
};

test exception_internal_row_ends_faulting_anonymous_step_at_process_event = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *s1 + event<e1> = idle
        , idle [ faulting_guard ] = s2
        , idle + exception<exception1> / [](faults &f) { ++f.recoveries; }
      );
      // clang-format on
    }
  };

  faults f{};
  sml::sm<c> sm{f};
  expect(0 == f.attempts);
  expect(sm.process_event(e1{}));
  expect(1 == f.attempts);
  expect(1 == f.recoveries);
  expect(sm.is(idle));
};

test exception_self_targeting_row_ends_faulting_anonymous_step = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         (*idle) [ faulting_guard ] = s1
        , idle + exception<exception1> / [](faults &f) { ++f.recoveries; } = idle
      );
      // clang-format on
    }
  };

  faults f{};
  sml::sm<c> sm{f};
  expect(1 == f.attempts);
  expect(1 == f.recoveries);
  expect(sm.is(idle));
};

test exception_moving_row_continues_stabilizing = [] {
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         (*idle) [ faulting_guard ] = s1
        , idle + exception<exception1> / [](faults &f) { ++f.recoveries; } = error
        , error = s2
      );
      // clang-format on
    }
  };

  faults f{};
  sml::sm<c> sm{f};
  expect(1 == f.attempts);
  expect(1 == f.recoveries);
  expect(sm.is(s2));
};

test exception_child_row_exits_child_and_parent_completion_runs = [] {
  struct child {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         (*idle) [ faulting_guard ] = s1
        , idle + exception<exception1> / [](faults &f) { ++f.recoveries; } = X
      );
      // clang-format on
    }
  };
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> = state<child>
        , state<child> = s2
      );
      // clang-format on
    }
  };

  faults f{};
  sml::sm<c> sm{f};
  expect(sm.process_event(e1{}));
  expect(1 == f.attempts);
  expect(1 == f.recoveries);
  expect(sm.is(s2));
};
test exception_child_completion_fault_reaches_parent_once = [] {
  struct child {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         (*idle) [ faulting_guard ] = s1
        , idle + exception<exception2> = X
      );
      // clang-format on
    }
  };
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> = state<child>
        , state<child> + exception<exception1> / [](faults &f) { ++f.recoveries; } = s2
      );
      // clang-format on
    }
  };

  faults f{};
  sml::sm<c> sm{f};
  expect(sm.process_event(e1{}));
  expect(1 == f.attempts);
  expect(1 == f.recoveries);
  expect(sm.is(s2));
};
test exception_offered_to_child_without_composite_row_does_not_restabilize_child = [] {
  struct child {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         (*idle) [ faulting_guard ] = s1
        , idle + exception<exception2> = X
      );
      // clang-format on
    }
  };
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> = state<child>
        , *error + exception<exception1> / [](faults &f) { ++f.recoveries; }
      );
      // clang-format on
    }
  };

  faults f{};
  sml::sm<c> sm{f};
  expect(sm.process_event(e1{}));
  expect(1 == f.attempts);
  expect(1 == f.recoveries);
  expect(sm.is(sml::state<child>, error));
};

test exception_child_fault_skips_inactive_typed_parent_row = [] {
  struct child {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         (*idle) [ faulting_guard ] = s1
        , idle + exception<exception2> = X
      );
      // clang-format on
    }
  };
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> = state<child>
        , s2 + exception<exception1> = X
        , state<child> + exception<_> / [](faults &f) { ++f.recoveries; } = s1
      );
      // clang-format on
    }
  };

  faults f{};
  sml::sm<c> sm{f};
  expect(sm.process_event(e1{}));
  expect(1 == f.attempts);
  expect(1 == f.recoveries);
  expect(sm.is(s1));
};

test exception_grandchild_fault_walks_to_root = [] {
  struct grandchild {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         (*idle) [ faulting_guard ] = s1
        , idle + exception<exception2> = X
      );
      // clang-format on
    }
  };
  struct child {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> = state<grandchild>
        , s1 + exception<exception1> = X
      );
      // clang-format on
    }
  };
  struct c {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *idle + event<e1> = state<child>
        , state<child> + exception<exception1> / [](faults &f) { ++f.recoveries; } = s2
      );
      // clang-format on
    }
  };

  faults f{};
  sml::sm<c> sm{f};
  expect(sm.process_event(e1{}));
  expect(sm.process_event(e1{}));
  expect(1 == f.attempts);
  expect(1 == f.recoveries);
  expect(sm.is(s2));
};

#endif
