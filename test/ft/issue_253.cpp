#include <boost/sml.hpp>
#include <deque>
#include <queue>

namespace sml = boost::sml;

test issue_253_nested_defer_queue_is_cleared_on_exit = [] {
  struct issue_253_enter {};
  struct issue_253_exit {};
  struct issue_253_to_s2 {};
  struct issue_253_to_s1 {};
  struct issue_253_e1 {};
  struct issue_253_outer_idle {};

  struct issue_253_counters {
    int nested_calls = 0;
    int outer_calls = 0;
  };

  struct issue_253_nested {
    auto operator()() const {
      using namespace sml;
      const auto issue_253_s1 = sml::state<class issue_253_s1>;
      const auto issue_253_s2 = sml::state<class issue_253_s2>;
      // clang-format off
      return make_transition_table(
          *issue_253_s1 + event<issue_253_to_s2> = issue_253_s2
        , issue_253_s2 + event<issue_253_e1> / defer
        , issue_253_s2 + event<issue_253_to_s1> = issue_253_s1
        , issue_253_s1 + event<issue_253_e1> / [](issue_253_counters& counters) { ++counters.nested_calls; }
      );
      // clang-format on
    }
  };

  struct issue_253_outer {
    auto operator()() const {
      using namespace sml;
      const auto issue_253_idle = sml::state<issue_253_outer_idle>;
      const auto issue_253_nested_state = sml::state<issue_253_nested>;
      // clang-format off
      return make_transition_table(
          *issue_253_idle + event<issue_253_enter> = issue_253_nested_state
        , issue_253_nested_state + event<issue_253_exit> = issue_253_idle
        , issue_253_idle + event<issue_253_e1> / [](issue_253_counters& counters) { ++counters.outer_calls; }
      );
      // clang-format on
    }
  };

  issue_253_counters counters{};
  sml::sm<issue_253_outer, sml::defer_queue<std::deque>, sml::process_queue<std::queue>> sm{counters};

  expect(sm.process_event(issue_253_enter{}));
  expect(sm.process_event(issue_253_to_s2{}));
  expect(sm.process_event(issue_253_e1{}));
  expect(sm.process_event(issue_253_to_s1{}));
  expect(sm.process_event(issue_253_exit{}));
  expect(1 == counters.nested_calls);
  expect(0 == counters.outer_calls);

  expect(sm.process_event(issue_253_e1{}));
  expect(1 == counters.outer_calls);

  expect(sm.process_event(issue_253_enter{}));
  expect(sm.process_event(issue_253_to_s2{}));
  expect(sm.process_event(issue_253_to_s1{}));
  expect(sm.process_event(issue_253_exit{}));
  expect(1 == counters.nested_calls);

  expect(sm.process_event(issue_253_e1{}));
  expect(2 == counters.outer_calls);
};
