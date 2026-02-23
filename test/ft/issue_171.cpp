//
// Copyright (c) 2016-2020 Kris Jusiak (kris at jusiak dot net)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#include <boost/sml.hpp>

namespace sml = boost::sml;

test issue_171_event_any_is_not_fired_twice = [] {
  struct e1 {};

  struct issue_171_counters {
    int init_calls = 0;
    int wildcard_calls = 0;
  };

  struct issue_171_transitions {
    auto operator()() const noexcept {
      using namespace sml;
      const auto issue_171_idle = sml::state<class issue_171_idle>;
      const auto issue_171_s1 = sml::state<class issue_171_s1>;
      const auto issue_171_s2 = sml::state<class issue_171_s2>;

      // clang-format off
      return make_transition_table(
          *issue_171_idle / [](issue_171_counters& counters) { ++counters.init_calls; } = issue_171_s1
        , issue_171_s1 + event<_> / [](issue_171_counters& counters) { ++counters.wildcard_calls; }
        , issue_171_s2 + event<e1> / [] {}
      );
      // clang-format on
    }
  };

  issue_171_counters counters{};
  sml::sm<issue_171_transitions> sm{counters};

  sm.process_event(e1{});

  expect(1 == counters.init_calls);
  expect(1 == counters.wildcard_calls);
};
