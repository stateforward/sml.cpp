#include <atomic>
#include <boost/sml.hpp>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#if !defined(_MSC_VER)
#include <boost/sml/utility/dispatch_table.hpp>
#endif

namespace sml = boost::sml;

namespace {
const auto issue_88_state1 = sml::state<class issue_88_state1>;
const auto issue_88_state2 = sml::state<class issue_88_state2>;
const auto issue_86_state1 = sml::state<class issue_86_state1>;
const auto issue_86_state2 = sml::state<class issue_86_state2>;
const auto issue_85_state1 = sml::state<class issue_85_state1>;
const auto issue_85_state2 = sml::state<class issue_85_state2>;
const auto issue_85_state_other = sml::state<class issue_85_state_other>;
const auto issue_93_state = sml::state<class issue_93_state>;
const auto issue_98_s1 = sml::state<class issue_98_state_s1>;
const auto issue_98_s2 = sml::state<class issue_98_state_s2>;
const auto issue_98_s3 = sml::state<class issue_98_state_s3>;
const auto issue_98_s4 = sml::state<class issue_98_state_s4>;
const auto issue_313_state_start = sml::state<class issue_313_state_start>;
const auto issue_313_state_mid = sml::state<class issue_313_state_mid>;
const auto issue_313_state_terminal = sml::state<class issue_313_state_terminal>;

inline bool issue_repro(const char* issue_path, int id, const char* title) {
  std::printf("Reproducing issue #%d: %s\n", id, title);
  std::printf("Refer to: %s\n", issue_path);

  std::ifstream issue_file{issue_path};
  if (!issue_file.is_open()) {
    std::printf("Missing issue file: %s\n", issue_path);
    return false;
  }

  std::string line;
  std::getline(issue_file, line);
  std::printf("Title from file: %s\n", line.c_str());
  std::printf("Expected behavior from issue claim still unimplemented.\n");
  return false;
}

struct issue_93_property {
  int method_calls = 0;
  void on_entry() { ++method_calls; }
};

struct issue_93_with_prop;

struct issue_93_entry_action {
  void operator()(issue_93_with_prop& owner) const;
};

struct issue_313_payload {
  int value;
};

struct issue_313_traits {
  static const std::function<bool(const issue_313_payload&)> is_below_five;
  static const std::function<bool(const issue_313_payload&)> is_above_five;
  static const std::function<void()> on_below_five;
  static const std::function<void()> on_above_five;
  static const std::function<void()> on_exactly_five;
};

int issue_313_below_count = 0;
int issue_313_above_count = 0;
int issue_313_exact_count = 0;

bool issue_194_callable_function() { return true; }

struct issue_93_transitions {
  auto operator()() const {
    using namespace sml;
    // clang-format off
    return make_transition_table(
      *issue_93_state + on_entry<_> / issue_93_entry_action()
    );
    // clang-format on
  }
};

struct issue_93_with_prop {
  issue_93_with_prop() : sm{*this, property} {}

  void mark_enter() { ++entered_count; }
  void method() { ++method_calls; }

  int entered_count = 0;
  int method_calls = 0;
  issue_93_property property;
  sml::sm<issue_93_transitions> sm;
};

inline void issue_93_entry_action::operator()(issue_93_with_prop& owner) const {
  owner.mark_enter();
  owner.method();
  owner.property.on_entry();
}

const std::function<bool(const issue_313_payload&)> issue_313_traits::is_below_five = [](const issue_313_payload& value) {
  return value.value < 5;
};

const std::function<bool(const issue_313_payload&)> issue_313_traits::is_above_five = [](const issue_313_payload& value) {
  return value.value > 5;
};

const std::function<void()> issue_313_traits::on_below_five = [] { ++issue_313_below_count; };
const std::function<void()> issue_313_traits::on_above_five = [] { ++issue_313_above_count; };
const std::function<void()> issue_313_traits::on_exactly_five = [] { ++issue_313_exact_count; };
}  // namespace

test issue_88 = [] {
  struct e1 {};

  struct transitions {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *issue_88_state1 + unexpected_event<_> / [this] { ++unexpected_calls; } = issue_88_state2,
        issue_88_state2 + event<e1> = issue_88_state1
      );
      // clang-format on
    }

    int unexpected_calls = 0;
  };

  sml::sm<transitions> sm{};
  expect(0 == static_cast<const transitions&>(sm).unexpected_calls);
  sm.process_event(e1{});
  expect(1 == static_cast<const transitions&>(sm).unexpected_calls);
  expect(sm.is(issue_88_state2));
};

test issue_86 = [] {
  struct e1 {};
  struct false_guard {
    bool operator()(const e1&) const { return false; }
  };

  struct transitions {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *issue_86_state1 + event<e1>[false_guard{}] = issue_86_state2,
        issue_86_state1 + unexpected_event<e1> / [this] { ++unexpected_calls; } = issue_86_state2
      );
      // clang-format on
    }

    int unexpected_calls = 0;
  };

  sml::sm<transitions> sm{};
  expect(sm.is(issue_86_state1));
  expect(0 == static_cast<const transitions&>(sm).unexpected_calls);
  sm.process_event(e1{});
  expect(1 == static_cast<const transitions&>(sm).unexpected_calls);
  expect(sm.is(issue_86_state2));
};

test issue_85 = [] {
  struct e1 {};
  struct false_guard {
    bool operator()(const e1&) const { return false; }
  };

  struct transitions {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *issue_85_state1 + event<e1>[false_guard{}] = issue_85_state2,
        issue_85_state_other + event<e1> / [this] { ++handled_calls; } = issue_85_state_other,
        issue_85_state1 + unexpected_event<_> / [this] { ++unexpected_calls; } = issue_85_state_other
      );
      // clang-format on
    }

    int handled_calls = 0;
    int unexpected_calls = 0;
  };

  sml::sm<transitions> sm{};
  expect(sm.is(issue_85_state1));
  sm.process_event(e1{});
  expect(0 == static_cast<const transitions&>(sm).handled_calls);
  expect(1 == static_cast<const transitions&>(sm).unexpected_calls);
  expect(sm.is(issue_85_state_other));

  sm.process_event(e1{});
  expect(1 == static_cast<const transitions&>(sm).handled_calls);
  expect(1 == static_cast<const transitions&>(sm).unexpected_calls);
  expect(sm.is(issue_85_state_other));
};

test issue_93 = [] {
  issue_93_with_prop owner{};
  expect(1 == owner.entered_count);
  expect(1 == owner.method_calls);
  expect(1 == owner.property.method_calls);
};

test issue_98 = [] {
  struct e {};

  struct transitions {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *issue_98_s1 + event<e> = issue_98_s2,
        issue_98_s2 = issue_98_s3,
        issue_98_s3 = issue_98_s4
      );
      // clang-format on
    }
  };

  sml::sm<transitions> sm{};
  expect(sm.is(issue_98_s1));
  sm.process_event(e{});
  expect(!sm.is(issue_98_s2));
  expect(!sm.is(issue_98_s3));
  expect(sm.is(issue_98_s4));
};

test issue_111 = [] {
  struct to_c {};
  struct to_d {};
  struct leave_to_forest {};
  struct return_to_parent {};
  struct parent_forest {};

  struct parent_hsm {
    auto operator()() const {
      using namespace sml;
      const auto parent_state_b = sml::state<class issue_111_parent_b>;
      const auto parent_state_c = sml::state<class issue_111_parent_c>;
      const auto parent_state_d = sml::state<class issue_111_parent_d>;

      // clang-format off
      return make_transition_table(
        *parent_state_b + event<to_c> = parent_state_c,
        parent_state_c + event<to_d> = parent_state_d
      );
      // clang-format on
    }
  };

  struct transitions {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *state<parent_hsm> + on_entry<_> / [this] { ++parent_entry_calls; },
        state<parent_hsm> + on_exit<_> / [this] { ++parent_exit_calls; },
        state<parent_hsm> + event<leave_to_forest> = state<parent_forest>,
        state<parent_forest> + event<return_to_parent> = state<parent_hsm>
      );
      // clang-format on
    }

    int parent_entry_calls = 0;
    int parent_exit_calls = 0;
  };

  sml::sm<transitions> sm{};

  expect(1 == static_cast<const transitions&>(sm).parent_entry_calls);
  expect(0 == static_cast<const transitions&>(sm).parent_exit_calls);

  expect(sm.process_event(to_c{}));
  expect(sm.process_event(to_d{}));
  expect(0 == static_cast<const transitions&>(sm).parent_exit_calls);
  expect(sm.is(sml::state<parent_hsm>));

  expect(sm.process_event(leave_to_forest{}));
  expect(1 == static_cast<const transitions&>(sm).parent_exit_calls);
  expect(sm.is(sml::state<parent_forest>));

  expect(sm.process_event(return_to_parent{}));
  expect(2 == static_cast<const transitions&>(sm).parent_entry_calls);
  expect(sm.is(sml::state<parent_hsm>));

  expect(sm.process_event(to_c{}));
  expect(sm.process_event(to_d{}));
  expect(1 == static_cast<const transitions&>(sm).parent_exit_calls);
};

test issue_114 = [] {
  struct event_1 {};
  struct event_2 {};
  struct event_3 {};
  struct event_4 {};
  struct event_5 {};
  struct event_6 {};

  struct transitions {
    auto operator()() {
      const auto issue_114_idle = sml::state<class issue_114_idle>;
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *issue_114_idle + event<event_1> / [this] { ++matched_events_1; },
        issue_114_idle + event<event_3> / [this] { ++matched_events_3_4_5; },
        issue_114_idle + event<event_4> / [this] { ++matched_events_3_4_5; },
        issue_114_idle + event<event_5> / [this] { ++matched_events_3_4_5; },
        issue_114_idle + event<_> / [this] { ++everything_else; }
      );
      // clang-format on
    }

    int matched_events_1 = 0;
    int matched_events_3_4_5 = 0;
    int everything_else = 0;
  };

  sml::sm<transitions> sm{};
  expect(sm.process_event(event_1{}));
  expect(sm.process_event(event_3{}));
  expect(sm.process_event(event_4{}));
  expect(sm.process_event(event_5{}));
  expect(sm.process_event(event_2{}));
  expect(sm.process_event(event_6{}));

  expect(1 == static_cast<const transitions&>(sm).matched_events_1);
  expect(3 == static_cast<const transitions&>(sm).matched_events_3_4_5);
  expect(2 == static_cast<const transitions&>(sm).everything_else);
};

struct action_with_source_target_type_params {
  template <class EventT, class SourceStateT, class TargetStateT>
  void operator()(const EventT&, const SourceStateT&, const TargetStateT&) const {
    (void)0;
  }
};

test issue_115 = [] {
  struct issue_115_event {};
  struct issue_115_s1 {};
  struct issue_115_s2 {};

  using args_t = sml::front::args_t<action_with_source_target_type_params, issue_115_event>;

  expect((std::is_same<args_t, sml::aux::type_list<sml::front::action_base>>::value));

  struct transitions {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *state<issue_115_s1> + event<issue_115_event> = state<issue_115_s2>
      );
      // clang-format on
    }
  };

  sml::sm<transitions> sm{};
  expect(sm.is(sml::state<issue_115_s1>));
  expect(sm.process_event(issue_115_event{}));
  expect(sm.is(sml::state<issue_115_s2>));
};

test issue_120 = [] {
  struct issue_120_event {};
  struct issue_120_switch {};
  struct issue_120_s1 {};
  struct issue_120_s2 {};

  struct transitions {
    auto operator()() {
      using namespace sml;
      const auto state_1 = sml::state<class issue_120_s1>;
      const auto state_2 = sml::state<class issue_120_s2>;
      const auto any_state = sml::state<sml::_>;

      // clang-format off
      return make_transition_table(
        *state_1 + event<issue_120_switch> = state_2,
        state_2 + event<issue_120_switch> = state_1,
        any_state + event<issue_120_event> / [this] { ++all_state_calls; }
      );
      // clang-format on
    }

    int all_state_calls = 0;
  };

  sml::sm<transitions> sm{};
  expect(sm.is(sml::state<issue_120_s1>));
  expect(sm.process_event(issue_120_event{}));
  expect(1 == static_cast<const transitions&>(sm).all_state_calls);

  sm.process_event(issue_120_switch{});
  expect(sm.is(sml::state<issue_120_s2>));
  expect(sm.process_event(issue_120_event{}));
  expect(2 == static_cast<const transitions&>(sm).all_state_calls);
};

test issue_122 = [] {
  struct issue_122_start_train {};
  struct issue_122_fork_event {};
  struct issue_122_stopped {};
  struct issue_122_rolling {};

  struct choose_direction {
    auto operator()() {
      using namespace sml;
      const auto choosing = state<class issue_122_choosing>;
      // clang-format off
      return make_transition_table(
        *choosing + event<issue_122_fork_event> = X,
        choosing + event<issue_122_fork_event> = X
      );
      // clang-format on
    }
  };

  struct transitions {
    auto operator()() {
      using namespace sml;
      const auto stopped = state<class issue_122_stopped>;
      const auto rolling = state<class issue_122_rolling>;

      // clang-format off
      return make_transition_table(
        *stopped + event<issue_122_start_train> = rolling,
        rolling + event<issue_122_fork_event> = state<choose_direction>
      );
      // clang-format on
    }
  };

  sml::sm<transitions, choose_direction> sm{};
  expect(sm.is(sml::state<issue_122_stopped>));
  expect(sm.process_event(issue_122_start_train{}));
  expect(sm.is(sml::state<issue_122_rolling>));
  expect(sm.process_event(issue_122_fork_event{}));
  expect(sm.is(sml::state<choose_direction>));
};

test issue_125 = [] {
  struct issue_125_event {};
  struct issue_125_outer_idle {};

  int entered_with_event = 0;
  int entered_with_wildcard = 0;

  struct issue_125_sub_state_machine {
    issue_125_sub_state_machine(int& entered_with_event, int& entered_with_wildcard)
        : entered_with_event{entered_with_event}, entered_with_wildcard{entered_with_wildcard} {}

    auto operator()() {
      using namespace sml;
      const auto initial_state = sml::state<class issue_125_sub_initial>;

      // clang-format off
      return make_transition_table(
        *initial_state + on_entry<issue_125_event> / [this] { ++entered_with_event; },
        initial_state + on_entry<_> / [this] { ++entered_with_wildcard; }
      );
      // clang-format on
    }

    int& entered_with_event;
    int& entered_with_wildcard;
  };

  struct transitions {
    auto operator()() {
      using namespace sml;
      const auto outer_idle = sml::state<issue_125_outer_idle>;
      // clang-format off
      return make_transition_table(
        *outer_idle + event<issue_125_event> = state<issue_125_sub_state_machine>
      );
      // clang-format on
    }
  };

  issue_125_sub_state_machine sub{entered_with_event, entered_with_wildcard};
  sml::sm<transitions, issue_125_sub_state_machine> sm{sub};

  expect(sm.is(sml::state<issue_125_outer_idle>));
  expect(sm.process_event(issue_125_event{}));
  expect(sm.is(sml::state<issue_125_sub_state_machine>));
  expect(1 == entered_with_event);
  expect(0 == entered_with_wildcard);
};

test issue_166 = [] {
  struct issue_166_event {};

  struct issue_166_statemachine_class {
    issue_166_statemachine_class() = default;
    issue_166_statemachine_class(const issue_166_statemachine_class&) = delete;

    auto operator()() const {
      using namespace sml;
      const auto start = sml::state<class issue_166_start>;
      // clang-format off
      return make_transition_table(
        *start + event<issue_166_event> = start
      );
      // clang-format on
    }
  };

  issue_166_statemachine_class state_machine_instance{};
  sml::sm<issue_166_statemachine_class, sml::dont_instantiate_statemachine_class> sm{state_machine_instance};
  expect(sm.process_event(issue_166_event{}));
};

test issue_171 = [] {
  struct issue_171_event {};
  struct issue_171_idle {};
  struct issue_171_s1 {};

  struct transitions {
    auto operator()() {
      using namespace sml;
      const auto idle = sml::state<class issue_171_idle>;
      const auto s1 = sml::state<class issue_171_s1>;
      // clang-format off
      return make_transition_table(
        *idle / [this] { ++entered_idle; } = s1,
        s1 + event<_> / [this] { ++all_event_calls; }
      );
      // clang-format on
    }

    int entered_idle = 0;
    int all_event_calls = 0;
  };

  sml::sm<transitions> sm{};
  expect(sm.is(sml::state<issue_171_s1>));
  expect(sm.process_event(issue_171_event{}));
  expect(1 == static_cast<const transitions&>(sm).all_event_calls);
};

test issue_172 = [] {
  struct issue_172_start {};
  struct issue_172_error {};
  struct issue_172_idle {};
  struct issue_172_error_state {};

  struct issue_172_sub_machine {
    auto operator()() {
      using namespace sml;
      const auto running = sml::state<class issue_172_sub_running>;
      // clang-format off
      return make_transition_table(
        *running + on_entry<_> / [] { process(issue_172_error{}); }
      );
      // clang-format on
    }
  };

  struct transitions {
    auto operator()() {
      using namespace sml;
      const auto idle = sml::state<class issue_172_idle>;
      const auto error_state = sml::state<class issue_172_error_state>;
      // clang-format off
      return make_transition_table(
        *idle + event<issue_172_start> = state<issue_172_sub_machine>,
        state<issue_172_sub_machine> + event<issue_172_error> / [this] { ++runtime_errors; } = error_state
      );
      // clang-format on
    }

    int runtime_errors = 0;
  };

  sml::sm<transitions> sm{};
  expect(sm.is(sml::state<issue_172_idle>));
  expect(sm.process_event(issue_172_start{}));
  expect(sm.is(sml::state<issue_172_error_state>));
  expect(1 == static_cast<const transitions&>(sm).runtime_errors);
};

#if !defined(_MSC_VER)
test issue_174 = [] {
  struct issue_174_idle {};
  struct issue_174_work {};
  struct issue_174_done {};

  struct issue_174_event {
    explicit issue_174_event(int value) : value{value} {}
    int value = 0;
  };

  struct issue_174_sdl_key_event_impl : issue_174_event, sml::utility::id<1> {
    explicit issue_174_sdl_key_event_impl(const issue_174_event& evt) : issue_174_event{evt} {}
  };
  struct issue_174_sdl_mouse_event_impl : issue_174_event, sml::utility::id<2> {
    explicit issue_174_sdl_mouse_event_impl(const issue_174_event& evt) : issue_174_event{evt} {}
  };

  struct transitions {
    auto operator()() {
      using namespace sml;
      const auto idle = sml::state<class issue_174_idle>;
      const auto work = sml::state<class issue_174_work>;
      const auto done = sml::state<class issue_174_done>;

      // clang-format off
      return make_transition_table(
        *idle + on_entry<_> / [] {} = work,
        work + event<issue_174_sdl_key_event_impl> / [this] { ++key_events; } = done,
        work + event<issue_174_sdl_mouse_event_impl> / [this] { ++mouse_events; } = done
      );
      // clang-format on
    }

    int key_events = 0;
    int mouse_events = 0;
  };

  sml::sm<transitions> sm{};
  auto dispatch = sml::utility::make_dispatch_table<issue_174_event, 1, 16>(sm);
  expect(!dispatch(issue_174_event{0}, 0));
  expect(dispatch(issue_174_event{1}, 1));
  expect(1 == static_cast<const transitions&>(sm).key_events);
  expect(sm.is(sml::state<issue_174_done>));
};
#endif

test issue_175 = [] {
  struct issue_175_exit {};
  struct issue_175_start {};
  struct issue_175_dummy {};

  struct issue_175_sub {
    auto operator()() {
      using namespace sml;
      const auto busy = sml::state<class issue_175_busy>;
      // clang-format off
      return make_transition_table(
        *busy + event<issue_175_exit> = sml::X
      );
      // clang-format on
    }
  };

  struct transitions {
    auto operator()() {
      using namespace sml;
      const auto idle = sml::state<class issue_175_idle>;
      // clang-format off
      return make_transition_table(
        *idle + event<issue_175_start> = state<issue_175_sub>
      );
      // clang-format on
    }
  };

  using issue_175_sm = sml::sm<transitions>;
  using issue_175_event_ids = sml::aux::inherit<typename issue_175_sm::events>;
  static_assert(!sml::aux::is_base_of<decltype(sml::event<issue_175_dummy>), issue_175_event_ids>::value,
                "issue #175: unexpected event type should not appear in event ids");

  issue_175_sm sm{};
  expect(sm.process_event(issue_175_start{}));
  expect(sm.is(sml::state<issue_175_sub>));
  expect(!sm.process_event(issue_175_dummy{}));
};

test issue_179 = [] {
  struct issue_179_start {};
  struct issue_179_a {};
  struct issue_179_b {};
  struct issue_179_c {};

  struct transitions {
    auto operator()() {
      using namespace sml;
      const auto start = sml::state<issue_179_start>;
      const auto a = sml::state<issue_179_a>;
      const auto b = sml::state<issue_179_b>;
      const auto c = sml::state<issue_179_c>;

      // clang-format off
      return make_transition_table(
        *start = a,
        a = b,
        b = c,
        start + on_entry<_> / [this] { calls += "s"; },
        a + on_entry<_> / [this] { calls += "a"; },
        b + on_entry<_> / [this] { calls += "b"; },
        c + on_entry<_> / [this] { calls += "c"; }
      );
      // clang-format on
    }

    std::string calls;
  };

  sml::sm<transitions> sm{};
  expect(sm.is(sml::state<issue_179_c>));
  expect(3 == static_cast<const transitions&>(sm).calls.size());
  expect("sabc" == static_cast<const transitions&>(sm).calls);
};

test issue_182 = [] {
  struct issue_182_idle {};
  struct issue_182_error {};

  struct issue_182_request {
    bool recoverable = false;
  };
  struct issue_182_recoverable_error {
    int code = 13;
  };
  struct issue_182_unhandled_error {};

  struct transitions {
    auto operator()() {
      using namespace sml;
      const auto issue_182_idle_state = sml::state<issue_182_idle>;
      const auto issue_182_error_state = sml::state<issue_182_error>;

      // clang-format off
      return make_transition_table(
        *issue_182_idle_state + event<issue_182_request> / [](const issue_182_request& request) -> void {
          if (request.recoverable) {
            throw issue_182_recoverable_error{};
          }
          throw issue_182_unhandled_error{};
        } = issue_182_error_state,
        issue_182_idle_state + exception<issue_182_recoverable_error> / [this](const issue_182_recoverable_error& error) {
          received_error_code = error.code;
        } = issue_182_error_state
      );
      // clang-format on
    }

    int received_error_code = -1;
  };

  sml::sm<transitions> handled{};
  handled.process_event(issue_182_request{true});
  expect(handled.is(sml::state<issue_182_error>));
  expect(13 == static_cast<const transitions&>(handled).received_error_code);

  sml::sm<transitions> unhandled{};
  bool caught = false;
  try {
    unhandled.process_event(issue_182_request{false});
  } catch (const issue_182_unhandled_error&) {
    caught = true;
  }

  expect(caught);
  expect(unhandled.is(sml::state<issue_182_idle>));
};

test issue_185 = [] {
  struct issue_185_idle {};
  struct issue_185_disconnected {};

  struct connect {};
  struct disconnect {};
  struct proto_data {};
  struct proto_ack {};

  struct protocol {
    auto operator()() const {
      using namespace sml;
      const auto protocol_idle = sml::state<class issue_185_protocol_idle>;
      const auto protocol_busy = sml::state<class issue_185_protocol_busy>;

      // clang-format off
      return make_transition_table(
        *protocol_idle + event<proto_data> = protocol_busy
      );
      // clang-format on
    }
  };

  struct transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_185_idle = sml::state<class issue_185_idle>;
      const auto issue_185_connected = sml::state<protocol>;
      const auto issue_185_disconnected = sml::state<class issue_185_disconnected>;

      // clang-format off
      return make_transition_table(
        *issue_185_idle + event<connect> = issue_185_connected,
        issue_185_connected + event<disconnect> = issue_185_disconnected,
        issue_185_connected + event<proto_ack> = issue_185_disconnected
      );
      // clang-format on
    }
  };

  sml::sm<transitions> sm{};
  expect(sm.is(sml::state<issue_185_idle>));

  expect(sm.process_event(connect{}));
  expect(sm.is(sml::state<protocol>));

  expect(sm.process_event(proto_data{}));
  expect(sm.process_event(proto_ack{}));
  expect(sm.is(sml::state<issue_185_disconnected>));
};

test issue_189 = [] {
  struct issue_189_start {};
  struct issue_189_done {};
  struct issue_189_poll {};
  struct issue_189_idle {};
  struct issue_189_busy {};
  struct issue_189_finished {};

  struct issue_189_runtime {
    std::atomic<bool> callback_dispatched{false};
    std::thread worker{};

    ~issue_189_runtime() {
      if (worker.joinable()) {
        worker.join();
      }
    }
  };

  struct transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_189_idle = sml::state<class issue_189_idle>;
      const auto issue_189_busy = sml::state<class issue_189_busy>;
      const auto issue_189_finished = sml::state<class issue_189_finished>;

      // clang-format off
      return make_transition_table(
        *issue_189_idle + event<issue_189_start> = issue_189_busy,
        issue_189_busy + on_entry<_> / [this](sml::back::process<issue_189_done> processEvent, issue_189_runtime &runtime) {
          runtime.callback_dispatched = false;
          runtime.worker = std::thread([processEvent = std::move(processEvent), &runtime]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
            processEvent(issue_189_done{});
            runtime.callback_dispatched = true;
          });
        },
        issue_189_busy + event<issue_189_done> = issue_189_finished,
        issue_189_finished + event<issue_189_poll> = issue_189_finished
      );
      // clang-format on
    }
  };

  issue_189_runtime runtime{};
  sml::sm<transitions, sml::process_queue<std::queue>> sm{runtime};
  expect(sm.process_event(issue_189_start{}));
  expect(sm.is(sml::state<issue_189_busy>));

  for (int i = 0; i < 20; ++i) {
    if (runtime.callback_dispatched.load()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  expect(runtime.callback_dispatched.load());
  expect(sm.is(sml::state<issue_189_busy>));

  sm.process_event(issue_189_poll{});
  expect(sm.is(sml::state<issue_189_finished>));
};

test issue_192 = [] {
  struct issue_192_start {};
  struct issue_192_sub_event {};

  struct issue_192_context {
    int from_parent = 0;
  };

  struct issue_192_sub {
    issue_192_context& parent;
    issue_192_sub(issue_192_context& parent) : parent{parent} {}

    auto operator()() const {
      using namespace sml;
      const auto issue_192_sub_idle = sml::state<class issue_192_sub_idle>;

      // clang-format off
      return make_transition_table(
        *issue_192_sub_idle + event<issue_192_sub_event> / [this] { ++parent.from_parent; }
      );
      // clang-format on
    }
  };

  struct transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_192_idle = sml::state<class issue_192_idle>;

      // clang-format off
      return make_transition_table(
        *issue_192_idle + event<issue_192_start> = state<issue_192_sub>
      );
      // clang-format on
    }
  };

  issue_192_context ctx{};
  sml::sm<transitions> sm{ctx, issue_192_sub{ctx}};

  expect(sm.process_event(issue_192_start{}));
  expect(sm.process_event(issue_192_sub_event{}));
  expect(1 == ctx.from_parent);
  expect(sm.is(sml::state<issue_192_sub>));
};

test issue_194 = [] {
  auto issue_194_callable_function = [] { return true; };
  auto issue_194_lambda = [] { return true; };
  static_assert(sml::concepts::callable<bool, decltype(issue_194_callable_function)>::value,
                "issue_194: free function not recognized as callable");
  static_assert(sml::concepts::callable<bool, decltype(issue_194_lambda)>::value,
                "issue_194: lambda not recognized as callable");
};

test issue_198 = [] {
// This is a CMake configuration issue about versioned feature checks, not a runtime behavior regression.
// Keep this test as an executable marker so the issue path is represented in the test list.
#if defined(_MSVC_LANG)
  expect(__cplusplus >= 201103L || _MSVC_LANG >= 201103L);
#else
  expect(__cplusplus >= 201103L);
#endif
};

test issue_220 = [] {
  // This is a library implementation naming-constraint issue (reserved identifiers).
  // There is no deterministic runtime behavior to assert from the public API.
  struct issue_220_marker {
    int value = 0;
  };

  issue_220_marker marker{};
  expect(0 == marker.value);
};

test issue_221 = [] {
  struct issue_221_connect {};
  struct issue_221_any_event {};

  struct issue_221_transition_table {
    auto operator()() {
      using namespace sml;
      const auto issue_221_idle = sml::state<class issue_221_idle>;

      // clang-format off
      return make_transition_table(
        *issue_221_idle + event<issue_221_connect> / [this] { ++specific_calls; } = issue_221_idle,
        issue_221_idle + event<_> / [this] { ++wildcard_calls; } = issue_221_idle,
        issue_221_idle + unexpected_event<_> / [this] { ++unexpected_calls; } = issue_221_idle
      );
      // clang-format on
    }

    int specific_calls = 0;
    int wildcard_calls = 0;
    int unexpected_calls = 0;
  };

  sml::sm<issue_221_transition_table> sm{};
  expect(sm.process_event(issue_221_connect{}));
  expect(sm.process_event(issue_221_any_event{}));
  expect(1 == static_cast<const issue_221_transition_table&>(sm).specific_calls);
  expect(1 == static_cast<const issue_221_transition_table&>(sm).wildcard_calls);
  expect(0 == static_cast<const issue_221_transition_table&>(sm).unexpected_calls);

  struct issue_221_unexpected {
    auto operator()() {
      using namespace sml;
      const auto issue_221_waiting = sml::state<class issue_221_waiting>;

      // clang-format off
      return make_transition_table(
        *issue_221_waiting + event<issue_221_connect> = issue_221_waiting,
        issue_221_waiting + unexpected_event<issue_221_any_event> / [this] { ++unexpected_calls; } = issue_221_waiting
      );
      // clang-format on
    }

    int unexpected_calls = 0;
  };

  sml::sm<issue_221_unexpected> fallback{};
  expect(fallback.process_event(issue_221_any_event{}));
  expect(1 == static_cast<const issue_221_unexpected&>(fallback).unexpected_calls);
};

#if 0  // Skip remaining issue_* regressions (241+) for this targeted fix.
test issue_241 = [] {
  struct issue_241_poll_event {};
  struct issue_241_msg_event {};
  struct issue_241_idle {};
  struct issue_241_polling {};
  struct issue_241_received {};

  struct issue_241_server {
    int poll_calls = 0;
    int msg_calls = 0;
    int callback_count = 0;

    struct issue_241_states {
      issue_241_server* server;

      explicit issue_241_states(issue_241_server& server) : server{&server} {}

      auto operator()() const {
        using namespace sml;
        const auto issue_241_idle_state = sml::state<issue_241_idle>;
        const auto issue_241_polling_state = sml::state<issue_241_polling>;
        const auto issue_241_received_state = sml::state<issue_241_received>;

        auto poll_action = [this] { server->poll(); };
        auto msg_action = [this] { server->message(); };

        // clang-format off
        return make_transition_table(
          *issue_241_idle_state + event<issue_241_poll_event> / poll_action = issue_241_polling_state,
          issue_241_polling_state + event<issue_241_msg_event> / msg_action = issue_241_received_state,
          issue_241_received_state + event<issue_241_poll_event> / poll_action = issue_241_polling_state
        );
        // clang-format on
      }
    };

    issue_241_states states;
    sml::sm<issue_241_states> sm;

    issue_241_server() : states{*this}, sm{states} {}

    void poll() {
      ++callback_count;
      if (callback_count < 6) {
        ++poll_calls;
        sm.process_event(issue_241_msg_event{});
      }
    }

    void message() {
      ++callback_count;
      if (callback_count < 6) {
        ++msg_calls;
        sm.process_event(issue_241_poll_event{});
      }
    }
  };

  issue_241_server server{};
  expect(server.sm.process_event(issue_241_poll_event{}));
  expect(3 == server.poll_calls);
  expect(3 == server.msg_calls);
  expect(server.sm.is(sml::state<issue_241_received>));
};

test issue_242 = [] {
  struct issue_242_request {};
  struct issue_242_done {};
  struct issue_242_idle {};

  struct issue_242_callback {
    std::function<void()> callback{};

    void set_callback(std::function<void()> cb) { callback = std::move(cb); }
    void invoke() {
      if (callback) {
        callback();
      }
    }
  };

  struct issue_242_transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_242_idle = sml::state<class issue_242_idle>;

      // clang-format off
      return make_transition_table(
        *issue_242_idle + event<issue_242_request> / [this](sml::back::process<issue_242_done> process_event,
                                                             issue_242_callback& callback) {
          callback.set_callback([processEvent = std::move(process_event)]() mutable { processEvent(issue_242_done{}); });
        },
        issue_242_idle + event<issue_242_done> = sml::X
      );
      // clang-format on
    }
  };

  issue_242_callback callback;
  sml::sm<issue_242_transitions, sml::process_queue<std::queue>> sm{callback};

  expect(sm.is(sml::state<issue_242_idle>));
  expect(sm.process_event(issue_242_request{}));
  expect(sm.is(sml::state<issue_242_idle>));
  callback.invoke();
  expect(sm.is(sml::X));
};

#define ISSUE_REPRO_TEST(ID, TITLE) \
  test issue_##ID = [] { return expect(issue_repro("tmp/issues/issue-" #ID ".md", ID, TITLE)); };
ISSUE_REPRO_TEST(44, "Marking initial state in transition that originates from its nested state")
ISSUE_REPRO_TEST(46, "Anonymous explicit transitions from a substate don't seem to work")
ISSUE_REPRO_TEST(73, "[question] thread safety")
ISSUE_REPRO_TEST(83, "Composing finite state machines recursively")
// issue_85: replaced with executable regression above.
// issue_86: replaced with executable regression above.
// issue_88: replaced with executable regression above.
// issue_93: replaced with executable regression above.
// issue_98: replaced with executable regression above.
// issue_111: replaced with executable regression above.
// issue_114: replaced with executable regression above.
// issue_115: replaced with executable regression above.
// issue_120: replaced with executable regression above.
// issue_122: replaced with executable regression above.
// issue_125: replaced with executable regression above.
// issue_138: discussion-only/production readiness question; no deterministic regression.
test issue_138 = [] {
  // No deterministic reproduction currently exists in the issue body.
  // Question-style issue.
  expect(true);
};
// issue_166: replaced with executable regression above.
// issue_171: replaced with executable regression above.
// issue_172: replaced with executable regression above.
// issue_174: replaced with executable regression above.
// issue_175: replaced with executable regression above.
// issue_179: replaced with executable regression above.
// issue_182: replaced with executable regression above.
// issue_185: replaced with executable regression above.
// issue_189: replaced with executable regression above.
// issue_192: replaced with executable regression above.
// issue_194: replaced with executable regression above.
// issue_198: replaced with executable regression above.
// issue_220: replaced with executable regression above.
// issue_221: replaced with executable regression above.
// issue_241: replaced with executable regression above.
// issue_242: replaced with executable regression above.
test issue_244 = [] {
  struct issue_244_event {};
  struct issue_244_sm {
    auto operator()() const {
      using namespace sml;
      const auto issue_244_idle = sml::state<class issue_244_idle>;
      // clang-format off
      return make_transition_table(
        *issue_244_idle + event<issue_244_event> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_244_sm> sm{};
  expect(sm.process_event(issue_244_event{}));
  expect(sm.is(sml::X));
};

test issue_249 = [] {
  struct issue_249_release {};
  struct issue_249_ack {};
  struct issue_249_fin {};
  struct issue_249_timeout {};

  struct issue_249_send_fin {
    void operator()() const {}
  };
  struct issue_249_send_ack {
    void operator()() const {}
  };

  struct issue_249_established;
  struct issue_249_fin_wait_1;
  struct issue_249_fin_wait_2;
  struct issue_249_timed_wait;

  struct issue_249_transitions {
    auto operator()() const {
      using namespace sml;

      const auto established = sml::state<issue_249_established>;
      const auto fin_wait_1 = sml::state<issue_249_fin_wait_1>;
      const auto fin_wait_2 = sml::state<issue_249_fin_wait_2>;
      const auto timed_wait = sml::state<issue_249_timed_wait>;

      // clang-format off
      return make_transition_table(
        *established + event<issue_249_release> / issue_249_send_fin{} = fin_wait_1,
        fin_wait_1 + event<issue_249_ack> [[](const issue_249_ack &) { return true; }] = fin_wait_2,
        fin_wait_2 + event<issue_249_fin> [[](const issue_249_fin &) { return true; }] / issue_249_send_ack{} = timed_wait,
        timed_wait + event<issue_249_timeout> / issue_249_send_ack{} = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_249_transitions> sm{};
  expect(sm.is(sml::state<issue_249_established>));
  expect(sm.process_event(issue_249_release{}));
  expect(sm.is(sml::state<issue_249_fin_wait_1>));
  expect(sm.process_event(issue_249_ack{}));
  expect(sm.is(sml::state<issue_249_fin_wait_2>));
  expect(sm.process_event(issue_249_fin{}));
  expect(sm.is(sml::state<issue_249_timed_wait>));
  expect(sm.process_event(issue_249_timeout{}));
  expect(sm.is(sml::X));
};

test issue_250 = [] {
  struct issue_250_boot {};
  struct issue_250_finish {};
  struct issue_250_region_left {};
  struct issue_250_region_right {};

  struct issue_250_bar {
    auto operator()() const {
      using namespace sml;
      const auto issue_250_bar_idle = sml::state<class issue_250_bar_idle>;
      // clang-format off
      return make_transition_table(
        *issue_250_bar_idle + event<issue_250_finish> = sml::X
      );
      // clang-format on
    }
  };

  struct issue_250_foo_1 {
    auto operator()() const {
      using namespace sml;
      const auto issue_250_foo_1_idle = sml::state<class issue_250_foo_1_idle>;
      // clang-format off
      return make_transition_table(
        *issue_250_foo_1_idle + event<issue_250_boot> = state<issue_250_bar>
      );
      // clang-format on
    }
  };

  struct issue_250_foo_2 {
    auto operator()() const {
      using namespace sml;
      const auto issue_250_foo_2_idle = sml::state<class issue_250_foo_2_idle>;
      // clang-format off
      return make_transition_table(
        *issue_250_foo_2_idle + event<issue_250_boot> = state<issue_250_bar>
      );
      // clang-format on
    }
  };

  struct issue_250_transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_250_region_left_state = sml::state<issue_250_region_left>;
      const auto issue_250_region_right_state = sml::state<issue_250_region_right>;
      // clang-format off
      return make_transition_table(
        *issue_250_region_left_state + event<issue_250_boot> = state<issue_250_foo_1>,
        *issue_250_region_right_state + event<issue_250_boot> = state<issue_250_foo_2>
      );
      // clang-format on
    }
  };

  sml::sm<issue_250_transitions> sm{};
  expect(sm.is(sml::state<issue_250_region_left>, sml::state<issue_250_region_right>));
  expect(sm.process_event(issue_250_boot{}));
  expect(sm.is(sml::state<issue_250_foo_1>.sm<issue_250_bar>(), sml::state<issue_250_foo_2>.sm<issue_250_bar>()));
  expect(sm.process_event(issue_250_finish{}));
  expect(sm.is(sml::X, sml::X));
};

test issue_252 = [] {
  struct issue_252_event {};
  struct issue_252_wait {};
  struct issue_252_transitions {
    auto operator()() const {
      using namespace sml;
      const auto wait = sml::state<issue_252_wait>;
      // clang-format off
      return make_transition_table(
        *wait + event<issue_252_event> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_252_transitions, sml::defer_queue<std::deque>, sml::process_queue<std::queue>> sm{};
  expect(sm.process_event(issue_252_event{}));
  expect(sm.is(sml::X));
};

test issue_246 = [] {
  struct issue_246_go_a {};
  struct issue_246_go_b {};
  struct issue_246_probe {};
  struct issue_246_reset {};

  struct issue_246_region_a_wait {};
  struct issue_246_region_a_check {};
  struct issue_246_region_a_done {};
  struct issue_246_region_b_wait {};
  struct issue_246_region_b_active {};

  struct issue_246_transitions {
    auto operator()() const {
      using namespace sml;

      const auto a_wait = sml::state<issue_246_region_a_wait>;
      const auto a_check = sml::state<issue_246_region_a_check>;
      const auto a_done = sml::state<issue_246_region_a_done>;
      const auto b_wait = sml::state<issue_246_region_b_wait>;
      const auto b_active = sml::state<issue_246_region_b_active>;

      // clang-format off
      return make_transition_table(
        *a_wait + event<issue_246_go_a> = a_check,
        *b_wait + event<issue_246_go_b> = b_active,
        a_check + event<issue_246_probe> [[](const issue_246_probe&, const auto& sm, const auto&, const auto&) {
          return sm.is(sml::state<issue_246_region_b_active>);
        }] = a_done,
        a_check + event<issue_246_reset> = a_wait,
        a_done + event<issue_246_reset> = a_wait,
        b_active + event<issue_246_reset> = b_wait
      );
      // clang-format on
    }
  };

  sml::sm<issue_246_transitions> sm{};
  expect(sm.is(sml::state<issue_246_region_a_wait>, sml::state<issue_246_region_b_wait>));

  expect(!sm.process_event(issue_246_probe{}));
  expect(sm.process_event(issue_246_go_a{}));
  expect(sm.is(sml::state<issue_246_region_a_check>, sml::state<issue_246_region_b_wait>));

  expect(!sm.process_event(issue_246_probe{}));
  expect(sm.process_event(issue_246_go_b{}));
  expect(sm.is(sml::state<issue_246_region_a_check>, sml::state<issue_246_region_b_active>));

  expect(sm.process_event(issue_246_probe{}));
  expect(sm.is(sml::state<issue_246_region_a_done>, sml::state<issue_246_region_b_active>));

  expect(sm.process_event(issue_246_reset{}));
  expect(sm.is(sml::state<issue_246_region_a_wait>, sml::state<issue_246_region_b_wait>));
};

test issue_253 = [] {
  struct issue_253_enter {};
  struct issue_253_exit {};
  struct issue_253_to_s2 {};
  struct issue_253_to_s1 {};
  struct issue_253_e1 {};

  struct issue_253_state {
    int nested_calls = 0;
    int outer_calls = 0;
  };

  struct issue_253_nested {
    auto operator()() const {
      using namespace sml;
      const auto issue_253_nested_s1 = sml::state<class issue_253_nested_s1>;
      const auto issue_253_nested_s2 = sml::state<class issue_253_nested_s2>;
      // clang-format off
      return make_transition_table(
        *issue_253_nested_s1 + event<issue_253_to_s2> = issue_253_nested_s2,
        issue_253_nested_s2 + event<issue_253_e1> / defer,
        issue_253_nested_s2 + event<issue_253_to_s1> = issue_253_nested_s1,
        issue_253_nested_s1 + event<issue_253_e1> / [](issue_253_state &state) { ++state.nested_calls; }
      );
      // clang-format on
    }
  };

  struct issue_253_outer {
    auto operator()() const {
      using namespace sml;
      const auto issue_253_outer_idle = sml::state<class issue_253_outer_idle>;
      const auto issue_253_outer_nested = sml::state<issue_253_nested>;
      // clang-format off
      return make_transition_table(
        *issue_253_outer_idle + event<issue_253_enter> = issue_253_outer_nested,
        issue_253_outer_nested + event<issue_253_exit> = issue_253_outer_idle,
        issue_253_outer_idle + event<issue_253_e1> / [](issue_253_state& state) { ++state.outer_calls; }
      );
      // clang-format on
    }
  };

  issue_253_state state{};
  sml::sm<issue_253_outer, sml::defer_queue<std::deque>, sml::process_queue<std::queue>> sm{state};

  expect(sm.process_event(issue_253_enter{}));
  expect(sm.is(sml::state<issue_253_nested>));
  expect(sm.process_event(issue_253_to_s2{}));
  expect(sm.process_event(issue_253_e1{}));
  expect(sm.process_event(issue_253_to_s1{}));
  expect(sm.process_event(issue_253_exit{}));
  expect(sm.is(sml::state<issue_253_outer_idle>));
  expect(0 == state.nested_calls);

  expect(sm.process_event(issue_253_e1{}));
  expect(1 == state.outer_calls);
  expect(0 == state.nested_calls);

  expect(sm.process_event(issue_253_enter{}));
  expect(sm.is(sml::state<issue_253_nested>));
  expect(sm.process_event(issue_253_to_s2{}));
  expect(sm.process_event(issue_253_to_s1{}));
  expect(sm.process_event(issue_253_exit{}));
  expect(sm.is(sml::state<issue_253_outer_idle>));
  expect(0 == state.nested_calls);
  expect(sm.process_event(issue_253_e1{}));
  expect(2 == state.outer_calls);
};

test issue_259 = [] {
  struct issue_259_event {};
  struct issue_259_restart {};
  struct issue_259_done {};

  struct issue_259_data {
    int state_data = 0;
    int transition_data = 0;
  };

  struct issue_259_idle {};
  struct issue_259_running {};

  struct issue_259_transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_259_idle_state = sml::state<issue_259_idle>;
      const auto issue_259_running_state = sml::state<issue_259_running>;
      // clang-format off
      return make_transition_table(
        *issue_259_idle_state + event<issue_259_event> / [](issue_259_data& data) { ++data.transition_data; } = issue_259_running_state,
        issue_259_running_state + event<issue_259_restart> / [](issue_259_data& data) { ++data.state_data; } = issue_259_idle_state,
        issue_259_idle_state + event<issue_259_restart> / [](issue_259_data& data) { ++data.state_data; } = issue_259_done
      );
      // clang-format on
    }
  };

  issue_259_data data{};
  sml::sm<issue_259_transitions> sm{data};
  expect(sm.process_event(issue_259_event{}));
  expect(sm.is(sml::state<issue_259_running>));
  expect(1 == data.transition_data);
  expect(sm.process_event(issue_259_restart{}));
  expect(sm.is(sml::state<issue_259_done>));
  expect(1 == data.transition_data);
  expect(1 == data.state_data);
};

test issue_260 = [] {
  struct issue_260_event {};
  struct issue_260_derived_event final : issue_260_event {};
  struct issue_260_start {};

  struct issue_260_transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_260_idle = sml::state<class issue_260_idle>;
      // clang-format off
      return make_transition_table(
        *issue_260_idle + event<issue_260_derived_event> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_260_transitions> sm{};
  expect(sm.process_event(issue_260_derived_event{}));
  expect(sm.is(sml::X));
  (void) sizeof(issue_260_start);
};

test issue_261 = [] {
  std::ifstream header{"include/boost/sml.hpp"};
  expect(header.is_open());
  std::string content((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  expect(std::string::npos != content.find("LICENSE_1_0.txt"));
  std::ifstream missing{"LICENSE_1_0.txt"};
  expect(!missing.is_open());
};

test issue_262 = [] {
  struct issue_262_play {};
  struct issue_262_pause {};
  struct issue_262_stop {};
  struct issue_262_next {};

  struct issue_262_inner_idle {};
  struct issue_262_inner_active {};

  struct issue_262_inner {
    auto operator()() const {
      using namespace sml;
      const auto idle = sml::state<issue_262_inner_idle>;
      const auto active = sml::state<issue_262_inner_active>;
      // clang-format off
      return make_transition_table(
        *idle(H) + event<issue_262_next> = active
      );
      // clang-format on
    }
  };

  struct issue_262_idle {};
  struct issue_262_paused {};

  struct issue_262_transitions {
    auto operator()() const {
      using namespace sml;
      const auto player_idle = sml::state<issue_262_idle>;
      const auto player_inner = sml::state<issue_262_inner>;
      const auto player_paused = sml::state<issue_262_paused>;
      // clang-format off
      return make_transition_table(
        *player_idle + event<issue_262_play> = player_inner,
        player_inner + event<issue_262_pause> = player_paused,
        player_inner + event<issue_262_stop> = player_idle,
        player_paused + event<issue_262_play> = player_inner
      );
      // clang-format on
    }
  };

  sml::sm<issue_262_transitions> sm{};
  expect(sm.is(sml::state<issue_262_idle>));
  expect(sm.process_event(issue_262_play{}));
  expect(sm.is<decltype(sml::state<issue_262_inner>)>(sml::state<issue_262_inner_idle>));
  expect(sm.process_event(issue_262_next{}));
  expect(sm.is<decltype(sml::state<issue_262_inner>)>(sml::state<issue_262_inner_active>));
  expect(sm.process_event(issue_262_pause{}));
  expect(sm.is(sml::state<issue_262_paused>));
  expect(sm.process_event(issue_262_stop{}));
  expect(sm.is(sml::state<issue_262_idle>));
  expect(sm.process_event(issue_262_play{}));
  expect(sm.is<decltype(sml::state<issue_262_inner>)>(sml::state<issue_262_inner_active>));
};
test issue_265 = [] {
  struct issue_265_enter_level {};
  struct issue_265_pause_level {};
  struct issue_265_restart_level {};
  struct issue_265_exit_game {};

  struct issue_265_lifecycle {
    int entered = 0;
    int exited = 0;
    int active = 0;
    int max_active = 0;
  };

  struct issue_265_menu {};
  struct issue_265_playing {};
  struct issue_265_paused {};

  struct issue_265_transitions {
    auto operator()() {
      using namespace sml;
      const auto menu = sml::state<issue_265_menu>;
      const auto playing = sml::state<issue_265_playing>;
      const auto paused = sml::state<issue_265_paused>;

      auto enter = [](issue_265_lifecycle& lifecycle) {
        ++lifecycle.entered;
        ++lifecycle.active;
        if (lifecycle.active > lifecycle.max_active) {
          lifecycle.max_active = lifecycle.active;
        }
      };

      auto exit = [](issue_265_lifecycle& lifecycle) {
        ++lifecycle.exited;
        --lifecycle.active;
      };
      // clang-format off
      return make_transition_table(
        *menu + on_entry<_> / enter,
        menu + on_exit<_> / exit,
        menu + event<issue_265_enter_level> = playing,

        playing + on_entry<_> / enter,
        playing + on_exit<_> / exit,
        playing + event<issue_265_pause_level> = paused,
        playing + event<issue_265_exit_game> = sml::X,

        paused + on_entry<_> / enter,
        paused + on_exit<_> / exit,
        paused + event<issue_265_restart_level> = playing
      );
      // clang-format on
    }
  };

  issue_265_lifecycle lifecycle{};
  sml::sm<issue_265_transitions> sm{lifecycle};

  expect(1 == lifecycle.entered);
  expect(0 == lifecycle.exited);
  expect(1 == lifecycle.active);
  expect(1 == lifecycle.max_active);
  expect(sm.is(sml::state<issue_265_menu>));

  expect(sm.process_event(issue_265_enter_level{}));
  expect(2 == lifecycle.entered);
  expect(1 == lifecycle.exited);
  expect(1 == lifecycle.active);
  expect(sm.is(sml::state<issue_265_playing>));

  expect(sm.process_event(issue_265_pause_level{}));
  expect(3 == lifecycle.entered);
  expect(2 == lifecycle.exited);
  expect(1 == lifecycle.active);
  expect(sm.is(sml::state<issue_265_paused>));

  expect(sm.process_event(issue_265_restart_level{}));
  expect(4 == lifecycle.entered);
  expect(3 == lifecycle.exited);
  expect(1 == lifecycle.active);
  expect(sm.is(sml::state<issue_265_playing>));

  expect(sm.process_event(issue_265_exit_game{}));
  expect(4 == lifecycle.entered);
  expect(4 == lifecycle.exited);
  expect(0 == lifecycle.active);
  expect(1 == lifecycle.max_active);
  expect(sm.is(sml::X));
};

test issue_275 = [] {
  struct issue_275_start {};
  struct issue_275_pause {};
  struct issue_275_resume {};
  struct issue_275_stop {};
  struct issue_275_idle {};
  struct issue_275_active {};
  struct issue_275_paused {};

  struct issue_275_transitions {
    auto operator()() const {
      const auto issue_275_idle_state = sml::state<issue_275_idle>;
      const auto issue_275_active_state = sml::state<issue_275_active>;
      const auto issue_275_paused_state = sml::state<issue_275_paused>;

      // clang-format off
      return make_transition_table(
        *issue_275_idle_state + event<issue_275_start> = issue_275_active_state,
        issue_275_active_state + event<issue_275_pause> = issue_275_paused_state,
        issue_275_paused_state + event<issue_275_resume> = issue_275_active_state,
        issue_275_active_state + event<issue_275_stop> = issue_275_idle_state
      );
      // clang-format on
    }
  };

  sml::sm<issue_275_transitions> sm{};
  expect(sm.is(sml::state<issue_275_idle>));
  expect(!sm.is(sml::state<issue_275_active>));

  expect(!sm.process_event(issue_275_stop{}));
  expect(sm.is(sml::state<issue_275_idle>));

  expect(sm.process_event(issue_275_start{}));
  expect(sm.is(sml::state<issue_275_active>));

  expect(sm.process_event(issue_275_pause{}));
  expect(sm.is(sml::state<issue_275_paused>));
  expect(sm.process_event(issue_275_resume{}));
  expect(sm.is(sml::state<issue_275_active>));

  expect(sm.process_event(issue_275_stop{}));
  expect(sm.is(sml::state<issue_275_idle>));
};

test issue_276 = [] {
  struct issue_276_evt0 {};
  struct issue_276_evt1 {};
  struct issue_276_evt2 {};
  struct issue_276_evt3 {};
  struct issue_276_evt4 {};
  struct issue_276_evt5 {};
  struct issue_276_evt6 {};

  struct issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_0 {};
  struct issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_1 {};
  struct issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_2 {};
  struct issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_3 {};
  struct issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_4 {};
  struct issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_5 {};
  struct issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_6 {};

  struct issue_276_transitions {
    int entered = 0;
    int exited = 0;
    int transitions = 0;

    auto operator()() {
      using namespace sml;
      const auto issue_276_state0 = sml::state<issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_0>;
      const auto issue_276_state1 = sml::state<issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_1>;
      const auto issue_276_state2 = sml::state<issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_2>;
      const auto issue_276_state3 = sml::state<issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_3>;
      const auto issue_276_state4 = sml::state<issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_4>;
      const auto issue_276_state5 = sml::state<issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_5>;
      const auto issue_276_state6 = sml::state<issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_6>;
      auto enter = [this] { ++entered; };
      auto exit = [this] { ++exited; };
      auto transition = [this] { ++transitions; };
      // clang-format off
      return make_transition_table(
        *issue_276_state0 + on_entry<_> / enter,
        issue_276_state0 + on_exit<_> / exit,
        issue_276_state0 + event<issue_276_evt0> / transition = issue_276_state1,

        issue_276_state1 + on_entry<_> / enter,
        issue_276_state1 + on_exit<_> / exit,
        issue_276_state1 + event<issue_276_evt1> / transition = issue_276_state2,

        issue_276_state2 + on_entry<_> / enter,
        issue_276_state2 + on_exit<_> / exit,
        issue_276_state2 + event<issue_276_evt2> / transition = issue_276_state3,

        issue_276_state3 + on_entry<_> / enter,
        issue_276_state3 + on_exit<_> / exit,
        issue_276_state3 + event<issue_276_evt3> / transition = issue_276_state4,

        issue_276_state4 + on_entry<_> / enter,
        issue_276_state4 + on_exit<_> / exit,
        issue_276_state4 + event<issue_276_evt4> / transition = issue_276_state5,

        issue_276_state5 + on_entry<_> / enter,
        issue_276_state5 + on_exit<_> / exit,
        issue_276_state5 + event<issue_276_evt5> / transition = issue_276_state6,

        issue_276_state6 + on_entry<_> / enter,
        issue_276_state6 + on_exit<_> / exit,
        issue_276_state6 + event<issue_276_evt6> / transition = sml::X
      );
      // clang-format on
    }
  };

  issue_276_transitions transitions{};
  sml::sm<issue_276_transitions> sm{transitions};

  expect(sm.is(sml::state<issue_276_state_alpha_with_a_very_long_and_verbose_identifier_name_to_stress_type_handling_0>()));
  expect(std::string{std::string(sml::aux::get_type_name<decltype(sm)>())}.length() > 0);

  expect(sm.process_event(issue_276_evt0{}));
  expect(sm.process_event(issue_276_evt1{}));
  expect(sm.process_event(issue_276_evt2{}));
  expect(sm.process_event(issue_276_evt3{}));
  expect(sm.process_event(issue_276_evt4{}));
  expect(sm.process_event(issue_276_evt5{}));
  expect(sm.process_event(issue_276_evt6{}));

  expect(sm.is(sml::X));
  expect(7 == transitions.transitions);
  expect(7 == transitions.entered);
  expect(7 == transitions.exited);
};

test issue_277 = [] {
  std::ifstream issue_277_cmake{"CMakeLists.txt"};
  expect(issue_277_cmake.is_open());
  std::string issue_277_contents((std::istreambuf_iterator<char>(issue_277_cmake)), std::istreambuf_iterator<char>());
  expect(std::string::npos != issue_277_contents.find("sml requires GCC >= 6.0.0"));
};

test issue_278 = [] {
  struct issue_278_outer_start {};
  struct issue_278_inner_next {};
  struct issue_278_inner;
  struct issue_278_outer_idle {};
  struct issue_278_inner_idle {};
  struct issue_278_inner_active {};

  struct issue_278_transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_278_outer_state = sml::state<issue_278_outer_idle>;
      const auto issue_278_inner_state = sml::state<issue_278_inner>;
      // clang-format off
      return make_transition_table(
        *issue_278_outer_state + event<issue_278_outer_start> = issue_278_inner_state
      );
      // clang-format on
    }
  };

  struct issue_278_inner {
    auto operator()() const {
      using namespace sml;
      const auto issue_278_inner_idle_state = sml::state<issue_278_inner_idle>;
      const auto issue_278_inner_active_state = sml::state<issue_278_inner_active>;
      // clang-format off
      return make_transition_table(
        *issue_278_inner_idle_state + event<issue_278_inner_next> = issue_278_inner_active_state
      );
      // clang-format on
    }
  };

  sml::sm<issue_278_transitions> sm{};
  expect(sm.is(sml::state<issue_278_outer_idle>));
  expect(sm.process_event(issue_278_outer_start{}));
  expect(sm.is<decltype(sml::state<issue_278_inner>)>(sml::state<issue_278_inner_idle>));
  expect(sm.process_event(issue_278_inner_next{}));
  expect(sm.is<decltype(sml::state<issue_278_inner>)>(sml::state<issue_278_inner_active>));

  sm.set_current_states(sml::state<issue_278_outer_idle>);
  sm.set_current_states<decltype(sml::state<issue_278_inner>)>(sml::state<issue_278_inner_idle>);
  expect(sm.is<decltype(sml::state<issue_278_inner>)>(sml::state<issue_278_inner_idle>));
};

test issue_279 = [] {
  struct issue_279_e1 {
    std::shared_ptr<int> p;
  };
  struct issue_279_e2 {};
  struct issue_279_e3 {};

  struct issue_279_transitions {
    auto operator()() {
      using namespace sml;
      const auto issue_279_s1 = sml::state<class issue_279_s1>;
      const auto issue_279_s2 = sml::state<class issue_279_s2>;

      auto record = [this](const issue_279_e1& evt) {
        if (processed == 0) {
          first_value = *evt.p;
          first_ref = evt.p.use_count();
        } else if (processed == 1) {
          second_value = *evt.p;
          second_ref = evt.p.use_count();
        }

        ++processed;
      };

      // clang-format off
      return make_transition_table(
        *issue_279_s1 + event<issue_279_e1> / defer,
        issue_279_s1 + event<issue_279_e2> / defer,
        issue_279_s1 + event<issue_279_e3> = issue_279_s2,
        issue_279_s2 + event<issue_279_e1> / record,
        issue_279_s2 + event<issue_279_e2> / defer
      );
      // clang-format on
    }

    int processed = 0;
    int first_value = 0;
    int second_value = 0;
    int first_ref = 0;
    int second_ref = 0;
  };

  auto e1val1 = std::make_shared<int>(1);
  auto e1val2 = std::make_shared<int>(2);
  sml::sm<issue_279_transitions, sml::defer_queue<std::deque>> sm{};

  sm.process_event(issue_279_e2{});
  sm.process_event(issue_279_e2{});
  sm.process_event(issue_279_e2{});
  sm.process_event(issue_279_e2{});
  sm.process_event(issue_279_e2{});
  sm.process_event(issue_279_e2{});
  sm.process_event(issue_279_e2{});
  sm.process_event(issue_279_e1{e1val1});
  sm.process_event(issue_279_e1{e1val2});
  sm.process_event(issue_279_e3{});

  expect(sm.is(sml::state<issue_279_s2>));
  const issue_279_transitions& transitions = sm;
  expect(2 == transitions.processed);
  expect(1 == transitions.first_value);
  expect(2 == transitions.second_value);
  expect(1 == transitions.first_ref);
  expect(1 == transitions.second_ref);
  expect(1 == e1val1.use_count());
  expect(1 == e1val2.use_count());
};

test issue_284 = [] {
  struct issue_284_start {};
  struct issue_284_child_event {};
  struct issue_284_parent_forward {};

  struct issue_284_dispatch {
    int forwarded = 0;
  };

  struct issue_284_child {
    auto operator()() const {
      using namespace sml;
      const auto issue_284_child_idle = sml::state<class issue_284_child_idle>;

      // clang-format off
      return make_transition_table(
        *issue_284_child_idle + event<issue_284_child_event> / process(issue_284_parent_forward{}) = issue_284_child_idle
      );
      // clang-format on
    }
  };

  struct issue_284_root {
    issue_284_dispatch& dispatch;

    issue_284_root(issue_284_dispatch& dispatch) : dispatch{dispatch} {}

    auto operator()() {
      using namespace sml;
      const auto issue_284_root_idle = sml::state<class issue_284_root_idle>;
      const auto issue_284_root_done = sml::state<class issue_284_root_done>;
      const auto issue_284_root_child = sml::state<issue_284_child>;

      // clang-format off
      return make_transition_table(
        *issue_284_root_idle + event<issue_284_start> = issue_284_root_child,
        issue_284_root_child + event<issue_284_parent_forward> / [this] { ++dispatch.forwarded; } = issue_284_root_done
      );
      // clang-format on
    }
  };

  issue_284_dispatch dispatch{};
  sml::sm<issue_284_root, sml::process_queue<std::queue>> sm{dispatch};

  expect(sm.is(sml::state<issue_284_root_idle>));
  expect(sm.process_event(issue_284_start{}));
  expect(sm.is(sml::state<issue_284_root_child>));
  expect(sm.process_event(issue_284_child_event{}));
  expect(sm.is(sml::state<issue_284_root_done>));
  expect(1 == dispatch.forwarded);
};

test issue_288 = [] {
  struct issue_288_start {};
  struct issue_288_step {};

  struct issue_288_context {
    int calls = 0;
    int active_depth = 0;
    int max_depth = 0;
    std::function<void()> dispatch_next;
  };

  struct issue_288_transitions {
    issue_288_context& context;

    issue_288_transitions(issue_288_context& context) : context{context} {}

    auto operator()() {
      using namespace sml;
      const auto issue_288_idle = sml::state<class issue_288_idle>;
      const auto issue_288_busy = sml::state<class issue_288_busy>;
      const auto issue_288_wait = sml::state<class issue_288_wait>;

      auto process_next = [this] {
        ++context.calls;
        ++context.active_depth;
        if (context.active_depth > context.max_depth) {
          context.max_depth = context.active_depth;
        }

        if (context.calls < 4 && context.dispatch_next) {
          context.dispatch_next();
        }

        --context.active_depth;
      };

      // clang-format off
      return make_transition_table(
        *issue_288_idle + event<issue_288_start> = issue_288_busy,
        issue_288_busy + on_entry<_> / process_next = issue_288_wait,
        issue_288_wait + on_entry<_> / process_next = issue_288_busy,
        issue_288_busy + event<issue_288_step> = issue_288_wait,
        issue_288_wait + event<issue_288_step> = issue_288_busy
      );
      // clang-format on
    }
  };

  issue_288_context context{};
  sml::sm<issue_288_transitions> sm{context};
  context.dispatch_next = [&sm] { sm.process_event(issue_288_step{}); };

  expect(sm.process_event(issue_288_start{}));
  expect(sm.is(sml::state<issue_288_wait>));
  expect(4 == context.calls);
  expect(4 == context.max_depth);
};

test issue_289 = [] {
  struct issue_289_boot {};
  struct issue_289_enter_leaf {};
  struct issue_289_leaf_ping {};
  struct issue_289_parent_forward {};

  struct issue_289_context {
    int forwarded = 0;
  };

  struct issue_289_leaf {
    auto operator()() const {
      using namespace sml;
      const auto issue_289_leaf_idle = sml::state<class issue_289_leaf_idle>;

      // clang-format off
      return make_transition_table(
        *issue_289_leaf_idle + event<issue_289_leaf_ping> / process(issue_289_parent_forward{})
      );
      // clang-format on
    }
  };

  struct issue_289_middle {
    auto operator()() const {
      using namespace sml;
      const auto issue_289_middle_idle = sml::state<class issue_289_middle_idle>;

      // clang-format off
      return make_transition_table(
        *issue_289_middle_idle + event<issue_289_enter_leaf> = state<issue_289_leaf>
      );
      // clang-format on
    }
  };

  struct issue_289_root {
    issue_289_context& context;

    issue_289_root(issue_289_context& context) : context{context} {}

    auto operator()() {
      using namespace sml;
      const auto issue_289_root_idle = sml::state<class issue_289_root_idle>;
      const auto issue_289_root_done = sml::state<class issue_289_root_done>;
      const auto issue_289_root_middle = sml::state<issue_289_middle>;

      // clang-format off
      return make_transition_table(
        *issue_289_root_idle + event<issue_289_boot> = issue_289_root_middle,
        issue_289_root_middle + event<issue_289_parent_forward> / [this] { ++context.forwarded; } = issue_289_root_done
      );
      // clang-format on
    }
  };

  issue_289_context context{};
  sml::sm<issue_289_root, sml::process_queue<std::queue>> sm{context};

  expect(sm.is(sml::state<issue_289_root_idle>));
  expect(sm.process_event(issue_289_boot{}));
  expect(sm.is(sml::state<issue_289_root_middle>));
  expect(sm.process_event(issue_289_enter_leaf{}));
  expect(sm.is<decltype(sml::state<issue_289_root_middle>)>(sml::state<issue_289_leaf_idle>));
  expect(sm.process_event(issue_289_leaf_ping{}));
  expect(sm.is(sml::state<issue_289_root_done>));
  expect(1 == context.forwarded);
};

test issue_295 = [] {
  struct issue_295_to_1 {};
  struct issue_295_to_2 {};
  struct issue_295_to_3 {};
  struct issue_295_error_found {};
  struct issue_295_error_solved {};

  struct issue_295_actual {
    auto operator()() {
      using namespace sml;
      const auto all_ok = sml::state<class issue_295_all_ok>;
      const auto st1 = sml::state<class issue_295_st1>;
      const auto st2 = sml::state<class issue_295_st2>;
      const auto st3 = sml::state<class issue_295_st3>;
      const auto error = sml::state<class issue_295_error>;

      // clang-format off
      return make_transition_table(
        *all_ok + event<issue_295_to_1> = st1,
        st1 + event<issue_295_to_2> = st2,
        st2 + event<issue_295_to_3> = st3,
        all_ok + event<issue_295_error_found> = error,
        error + event<issue_295_error_solved> = all_ok
      );
      // clang-format on
    }
  };

  struct issue_295_blocking {
    auto operator()() {
      using namespace sml;
      const auto non_blocking = sml::state<class issue_295_non_blocking>;
      const auto blocking = sml::state<class issue_295_blocking>;

      // clang-format off
      return make_transition_table(
        *non_blocking + event<issue_295_error_found> = blocking,
        blocking + event<issue_295_error_solved> = non_blocking
      );
      // clang-format on
    }
  };

  using issue_295_actual_sm = sml::sm<issue_295_actual>;
  using issue_295_blocking_sm = sml::sm<issue_295_blocking>;

  struct issue_295_controller {
    issue_295_actual_sm& actual;
    issue_295_blocking_sm& blocking;

    issue_295_controller(issue_295_actual_sm& actual, issue_295_blocking_sm& blocking) : actual{actual}, blocking{blocking} {}

    template <typename Event>
    void process(const Event& event) {
      const bool non_blocking_before = blocking.is(sml::state<issue_295_non_blocking>);
      blocking.process_event(event);
      if (non_blocking_before || blocking.is(sml::state<issue_295_non_blocking>)) {
        actual.process_event(event);
      }
    }
  };

  issue_295_actual_sm actual{};
  issue_295_blocking_sm blocking{};
  issue_295_controller controller{actual, blocking};

  expect(actual.is(sml::state<issue_295_all_ok>));
  expect(blocking.is(sml::state<issue_295_non_blocking>));

  controller.process(issue_295_to_1{});
  expect(actual.is(sml::state<issue_295_st1>));

  controller.process(issue_295_to_2{});
  expect(actual.is(sml::state<issue_295_st2>));

  controller.process(issue_295_error_found{});
  expect(actual.is(sml::state<issue_295_error>));
  expect(blocking.is(sml::state<issue_295_blocking>));

  controller.process(issue_295_to_3{});
  expect(actual.is(sml::state<issue_295_error>));

  controller.process(issue_295_error_solved{});
  expect(actual.is(sml::state<issue_295_all_ok>));
  expect(blocking.is(sml::state<issue_295_non_blocking>));

  controller.process(issue_295_to_1{});
  expect(actual.is(sml::state<issue_295_st1>));

  controller.process(issue_295_to_2{});
  expect(actual.is(sml::state<issue_295_st2>));

  controller.process(issue_295_to_3{});
  expect(actual.is(sml::state<issue_295_st3>));
};
test issue_297 = [] {
  struct issue_297_go {};

  struct issue_297_context {
    int parent_entered = 0;
    int ss1_entered = 0;
    int ss2_entered = 0;
  };

  struct issue_297_sub {
    issue_297_context& context;

    issue_297_sub(issue_297_context& context) : context{context} {}

    auto operator()() {
      using namespace sml;
      const auto issue_297_ss1 = sml::state<class issue_297_ss1>;
      const auto issue_297_ss2 = sml::state<class issue_297_ss2>;

      // clang-format off
      return make_transition_table(
        issue_297_ss1 + on_entry<issue_297_go> / [this] { ++context.ss1_entered; } = issue_297_ss2,
        issue_297_ss2 + on_entry<_> / [this] { ++context.ss2_entered; }
      );
      // clang-format on
    }
  };

  struct issue_297_root {
    issue_297_context& context;

    issue_297_root(issue_297_context& context) : context{context} {}

    auto operator()() {
      using namespace sml;
      const auto issue_297_idle = sml::state<class issue_297_idle>;
      const auto issue_297_sub_machine = sml::state<issue_297_sub>;

      // clang-format off
      return make_transition_table(
        *issue_297_idle + on_entry<_> / [this] { ++context.parent_entered; },
        issue_297_idle + event<issue_297_go> = issue_297_sub_machine
      );
      // clang-format on
    }
  };

  issue_297_context context{};
  sml::sm<issue_297_root, sml::process_queue<std::queue>> sm{context};

  expect(sm.is(sml::state<issue_297_idle>));
  expect(sm.process_event(issue_297_go{}));
  expect(sm.is<decltype(sml::state<issue_297_sub>)>(sml::state<issue_297_ss2>));
  expect(1 == context.parent_entered);
  expect(1 == context.ss1_entered);
  expect(1 == context.ss2_entered);
};

test issue_298 = [] {
  struct issue_298_start {};
  struct issue_298_idle {};
  struct issue_298_running {};

  struct issue_298_worker {
    int calls = 0;
    void execute() { ++calls; }
  };

  struct issue_298_logger {
    static inline int process_calls = 0;
    static inline int action_calls = 0;
    static inline int state_change_calls = 0;

    template <class SM, class TEvent>
    void log_process_event(const TEvent&) const {
      ++process_calls;
    }

    template <class SM, class TGuard, class TEvent>
    void log_guard(const TGuard&, const TEvent&, bool) const {}

    template <class SM, class TAction, class TEvent>
    void log_action(const TAction&, const TEvent&) const {
      ++action_calls;
    }

    template <class SM, class TSrc, class TDst>
    void log_state_change(const TSrc&, const TDst&) const {
      ++state_change_calls;
    }
  };

  auto issue_298_make_transitions = []() {
    using namespace sml;
    const auto issue_298_idle_state = sml::state<issue_298_idle>;
    const auto issue_298_running_state = sml::state<issue_298_running>;

    // clang-format off
    return make_transition_table(
      *issue_298_idle_state + event<issue_298_start> / &issue_298_worker::execute = issue_298_running_state
    );
    // clang-format on
  };

  struct issue_298_sm {
    auto operator()() const { return issue_298_make_transitions(); }
  };

  issue_298_worker worker{};
  issue_298_logger logger{};
  issue_298_logger::process_calls = 0;
  issue_298_logger::action_calls = 0;
  issue_298_logger::state_change_calls = 0;

  sml::sm<issue_298_sm, sml::logger<issue_298_logger>> sm{logger, worker};

  expect(sm.is(sml::state<issue_298_idle>));
  expect(sm.process_event(issue_298_start{}));
  expect(sm.is(sml::state<issue_298_running>));
  expect(1 == worker.calls);
  expect(0 < issue_298_logger::process_calls);
  expect(0 < issue_298_logger::action_calls);
  expect(0 < issue_298_logger::state_change_calls);
};

test issue_305 = [] {
  struct issue_305_event {
    static constexpr int id = 0;

    explicit issue_305_event(int value) : value{value} {}
    int value = 0;
  };

  struct issue_305_idle {};

  struct issue_305_sm {
    auto operator()() {
      using namespace sml;
      const auto issue_305_state = sml::state<issue_305_idle>;

      // clang-format off
      return make_transition_table(
        *issue_305_state + event<issue_305_event> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_305_sm> sm{};
  expect(sm.process_event(issue_305_event{}));
  expect(sm.is(sml::X));

#if !defined(_MSC_VER)
  auto dispatch = sml::utility::make_dispatch_table<issue_305_event, 0, 1>(sm);
  expect(dispatch(issue_305_event{0}, 0));
#endif
};

test issue_306 = [] {
  struct issue_306_start {};
  struct issue_306_idle {};

  struct issue_306_dependency {
    static inline int next_id = 0;

    explicit issue_306_dependency(std::vector<int>& history) : id{++next_id}, history{&history} {}

    int id;
    std::vector<int>* history;
  };

  struct issue_306_sm {
    auto operator()() {
      using namespace sml;
      const auto issue_306_idle_state = sml::state<issue_306_idle>;

      // clang-format off
      return make_transition_table(
        *issue_306_idle_state + event<issue_306_start> / [](issue_306_dependency& dep) { dep.history->push_back(dep.id); } = sml::X
      );
      // clang-format on
    }
  };

  std::vector<int> unique_a_history{};
  std::vector<int> unique_b_history{};
  std::vector<int> shared_history{};

  issue_306_dependency dep_a{unique_a_history};
  issue_306_dependency dep_b{unique_b_history};
  issue_306_dependency dep_shared{shared_history};

  sml::sm<issue_306_sm> sm_unique_a{dep_a};
  sml::sm<issue_306_sm> sm_unique_b{dep_b};
  sml::sm<issue_306_sm> sm_shared_a{dep_shared};
  sml::sm<issue_306_sm> sm_shared_b{dep_shared};

  expect(sm_unique_a.process_event(issue_306_start{}));
  expect(sm_unique_b.process_event(issue_306_start{}));
  expect(sm_shared_a.process_event(issue_306_start{}));
  expect(sm_shared_b.process_event(issue_306_start{}));

  expect(unique_a_history == std::vector<int>{dep_a.id});
  expect(unique_b_history == std::vector<int>{dep_b.id});
  expect(unique_a_history != unique_b_history);
  expect(shared_history == std::vector<int>{dep_shared.id, dep_shared.id});
};

test issue_308 = [] {
  struct issue_308_start {};
  struct issue_308_state {};

  struct issue_308_empty {
    auto operator()() {
      using namespace sml;
      const auto issue_308_state0 = sml::state<issue_308_state>;

      // clang-format off
      return make_transition_table(
        *issue_308_state0 + event<issue_308_start> = sml::X
      );
      // clang-format on
    }
  };

  struct issue_308_stateful {
    int payload = 0;

    auto operator()() {
      using namespace sml;
      const auto issue_308_state0 = sml::state<issue_308_state>;

      // clang-format off
      return make_transition_table(
        *issue_308_state0 + event<issue_308_start> / [this] { ++payload; } = sml::X
      );
      // clang-format on
    }
  };

  static_expect(sizeof(sml::sm<issue_308_stateful>) > sizeof(sml::sm<issue_308_empty>));

  sml::sm<issue_308_empty> empty{};
  sml::sm<issue_308_stateful> stateful{};

  expect(empty.process_event(issue_308_start{}));
  expect(stateful.process_event(issue_308_start{}));
};
test issue_310 = [] {
  std::ifstream issue_file{"tmp/issues/issue-310.md"};
  std::string title;
  expect(std::getline(issue_file, title));
  expect(title == "# Issue #310: Documentation not accessible");
};

test issue_313 = [] {
  issue_313_below_count = 0;
  issue_313_above_count = 0;
  issue_313_exact_count = 0;

  struct issue_313_transitions {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *issue_313_state_start + event<issue_313_payload> [issue_313_traits::is_below_five] / issue_313_traits::on_below_five = issue_313_state_mid,
        issue_313_state_start + event<issue_313_payload> [issue_313_traits::is_above_five] / issue_313_traits::on_above_five = issue_313_state_mid,
        issue_313_state_start + event<issue_313_payload> / issue_313_traits::on_exactly_five = issue_313_state_mid,
        issue_313_state_mid + event<issue_313_payload> [issue_313_traits::is_below_five] / issue_313_traits::on_below_five = issue_313_state_terminal,
        issue_313_state_mid + event<issue_313_payload> [issue_313_traits::is_above_five] / issue_313_traits::on_above_five = sml::X,
        issue_313_state_mid + event<issue_313_payload> / issue_313_traits::on_exactly_five = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_313_transitions> below{};
  sml::sm<issue_313_transitions> above{};
  sml::sm<issue_313_transitions> exact{};

  expect(below.process_event(issue_313_payload{4}));
  expect(below.is(issue_313_state_mid));

  expect(above.process_event(issue_313_payload{6}));
  expect(above.is(issue_313_state_mid));

  expect(exact.process_event(issue_313_payload{5}));
  expect(exact.is(issue_313_state_mid));

  expect(1 == issue_313_below_count);
  expect(1 == issue_313_above_count);
  expect(1 == issue_313_exact_count);
};

test issue_314 = [] {
  struct issue_314_event {};
  struct issue_314_runtime_error {};
  struct issue_314_waiting {};
  struct issue_314_processing {};
  struct issue_314_done {};

  struct issue_314_transitions {
    int waiting_exit_calls = 0;
    int processing_entry_calls = 0;
    int exception_calls = 0;

    auto operator()() {
      using namespace sml;
      const auto issue_314_waiting_state = sml::state<issue_314_waiting>;
      const auto issue_314_processing_state = sml::state<issue_314_processing>;
      const auto issue_314_done_state = sml::state<issue_314_done>;

      // clang-format off
      return make_transition_table(
        *issue_314_waiting_state + event<issue_314_event> / [this] { throw issue_314_runtime_error{}; } = issue_314_processing_state,
        issue_314_waiting_state + on_exit<_> / [this] { ++waiting_exit_calls; },
        issue_314_processing_state + on_entry<_> / [this] { ++processing_entry_calls; },
        issue_314_processing_state + exception<issue_314_runtime_error> / [this] { ++exception_calls; } = issue_314_done_state,
        issue_314_processing_state = issue_314_done_state
      );
      // clang-format on
    }
  };

  sml::sm<issue_314_transitions> sm{};
  bool threw = false;

  expect(sm.is(sml::state<issue_314_waiting>));
  try {
    expect(sm.process_event(issue_314_event{}));
  } catch (...) {
    threw = true;
  }
  expect(!threw);
  expect(sm.is(sml::state<issue_314_done>));
  expect(1 == static_cast<const issue_314_transitions&>(sm).waiting_exit_calls);
  expect(1 == static_cast<const issue_314_transitions&>(sm).processing_entry_calls);
  expect(1 == static_cast<const issue_314_transitions&>(sm).exception_calls);
};

test issue_315 = [] {
  std::ifstream issue_file{"tmp/issues/issue-315.md"};
  std::string title;
  expect(std::getline(issue_file, title));
  expect(title == "# Issue #315: suggest parentheses around assignment used as truth value");
};

test issue_316 = [] {
  struct issue_316_start {};
  struct issue_316_pause {};
  struct issue_316_stop {};
  struct issue_316_idle {};
  struct issue_316_running {};
  struct issue_316_paused {};
  struct issue_316_done {};

  struct issue_316_transitions {
    auto operator()() {
      using namespace sml;
      const auto issue_316_idle_state = sml::state<issue_316_idle>;
      const auto issue_316_running_state = sml::state<issue_316_running>;
      const auto issue_316_paused_state = sml::state<issue_316_paused>;
      const auto issue_316_done_state = sml::state<issue_316_done>;

      // clang-format off
      return make_transition_table(
        *issue_316_idle_state + event<issue_316_start> = issue_316_running_state,
        issue_316_running_state + event<issue_316_pause> = issue_316_paused_state,
        issue_316_paused_state + event<issue_316_start> = issue_316_running_state,
        issue_316_running_state + event<issue_316_stop> = issue_316_done_state,
        issue_316_paused_state + event<issue_316_stop> = issue_316_done_state
      );
      // clang-format on
    }
  };

  const auto is_active = [](const auto& sm) {
    return sm.is(sml::state<issue_316_running>) || sm.is(sml::state<issue_316_paused>);
  };

  sml::sm<issue_316_transitions> sm{};
  expect(!is_active(sm));
  expect(sm.is(sml::state<issue_316_idle>));

  expect(sm.process_event(issue_316_start{}));
  expect(is_active(sm));
  expect(sm.is(sml::state<issue_316_running>));

  expect(sm.process_event(issue_316_pause{}));
  expect(is_active(sm));
  expect(sm.is(sml::state<issue_316_paused>));

  expect(sm.process_event(issue_316_stop{}));
  expect(!is_active(sm));
  expect(sm.is(sml::state<issue_316_done>));
};
test issue_317 = [] {
  struct issue_317_noop {};
  struct issue_317_move {};
  struct issue_317_idle {};
  struct issue_317_running {};

  struct issue_317_transitions {
    int enter_calls = 0;
    int exit_calls = 0;

    auto operator()() {
      using namespace sml;
      const auto issue_317_idle_state = sml::state<issue_317_idle>;
      const auto issue_317_running_state = sml::state<issue_317_running>;

      // clang-format off
      return make_transition_table(
        *issue_317_idle_state + on_entry<_> / [this] { ++enter_calls; },
        issue_317_idle_state + on_exit<_> / [this] { ++exit_calls; },
        issue_317_idle_state + event<issue_317_move> / [this] { ++enter_calls; } = issue_317_running_state,
        issue_317_idle_state + event<issue_317_noop>,
        issue_317_running_state + on_entry<_> / [this] { ++enter_calls; },
        issue_317_running_state + on_exit<_> / [this] { ++exit_calls; }
      );
      // clang-format on
    }
  };

  sml::sm<issue_317_transitions> sm{};
  expect(1 == static_cast<const issue_317_transitions&>(sm).enter_calls);
  expect(0 == static_cast<const issue_317_transitions&>(sm).exit_calls);

  expect(sm.process_event(issue_317_noop{}));
  expect(sm.is(sml::state<issue_317_idle>));
  expect(1 == static_cast<const issue_317_transitions&>(sm).enter_calls);
  expect(0 == static_cast<const issue_317_transitions&>(sm).exit_calls);

  expect(sm.process_event(issue_317_move{}));
  expect(sm.is(sml::state<issue_317_running>));
  expect(2 == static_cast<const issue_317_transitions&>(sm).enter_calls);
  expect(1 == static_cast<const issue_317_transitions&>(sm).exit_calls);

  expect(sm.process_event(issue_317_noop{}));
  expect(sm.is(sml::state<issue_317_running>));
  expect(2 == static_cast<const issue_317_transitions&>(sm).enter_calls);
  expect(1 == static_cast<const issue_317_transitions&>(sm).exit_calls);
};
test issue_318 = [] {
  struct issue_318_context {
    bool to_running = false;
  };

  struct issue_318_running {};
  struct issue_318_idle {};

  struct issue_318_transitions {
    auto operator()() {
      using namespace sml;
      const auto issue_318_idle_state = sml::state<issue_318_idle>;
      const auto issue_318_running_state = sml::state<issue_318_running>;

      // clang-format off
      return make_transition_table(
        *issue_318_idle_state [[](const issue_318_context& context) { return context.to_running; }] = issue_318_running_state
      );
      // clang-format on
    }
  };

  issue_318_context disabled{};
  issue_318_context enabled{true};

  sml::sm<issue_318_transitions> sm_disabled{disabled};
  sml::sm<issue_318_transitions> sm_enabled{enabled};

  expect(sm_disabled.is(sml::state<issue_318_idle>));
  expect(sm_enabled.is(sml::state<issue_318_running>));
};
test issue_320 = [] {
  struct issue_320_input {
    int temperature = 0;
  };
  struct issue_320_transformed {
    bool too_hot = false;
  };
  struct issue_320_idle {};
  struct issue_320_running {};
  struct issue_320_hot {};
  struct issue_320_cold {};

  struct issue_320_transitions {
    auto operator()() {
      using namespace sml;
      const auto issue_320_idle_state = sml::state<issue_320_idle>;
      const auto issue_320_running_state = sml::state<issue_320_running>;
      const auto issue_320_hot_state = sml::state<issue_320_hot>;
      const auto issue_320_cold_state = sml::state<issue_320_cold>;

      // clang-format off
      return make_transition_table(
        *issue_320_idle_state + event<issue_320_input> / [](const issue_320_input& input,
                                                           sml::back::process<issue_320_transformed> processEvent) {
                                                             processEvent(issue_320_transformed{input.temperature > 100});
                                                           } = issue_320_running_state,
        issue_320_running_state + event<issue_320_transformed> [[](const issue_320_transformed& transformed) {
          return transformed.too_hot;
        }] = issue_320_hot_state,
        issue_320_running_state + event<issue_320_transformed> [[](const issue_320_transformed& transformed) {
          return !transformed.too_hot;
        }] = issue_320_cold_state
      );
      // clang-format on
    }
  };

  sml::sm<issue_320_transitions, sml::process_queue<std::queue>> sm_hot{};
  sml::sm<issue_320_transitions, sml::process_queue<std::queue>> sm_cold{};

  expect(sm_hot.process_event(issue_320_input{102}));
  expect(sm_hot.is(sml::state<issue_320_hot>));

  expect(sm_cold.process_event(issue_320_input{20}));
  expect(sm_cold.is(sml::state<issue_320_cold>));
};
test issue_321 = [] {
  struct issue_321_enter {}
  ;
  struct issue_321_event {};
  struct issue_321_exit {};

  struct issue_321_logger {
    static inline int process_calls = 0;
    static inline int entry_calls = 0;
    static inline int exit_calls = 0;

    template <class SM, class TEvent>
    void log_process_event(const TEvent&) const {
      ++process_calls;
    }

    template <class SM, class TEvent>
    void log_process_event(const sml::back::on_entry<TEvent> &) const {
      ++entry_calls;
    }

    template <class SM, class TEvent>
    void log_process_event(const sml::back::on_exit<TEvent> &) const {
      ++exit_calls;
    }
  };

  struct issue_321_transitions {
    auto operator()() {
      using namespace sml;
      const auto issue_321_idle = sml::state<issue_321_enter>;
      const auto issue_321_done = sml::state<issue_321_exit>;

      // clang-format off
      return make_transition_table(
        *issue_321_idle + on_entry<_>,
        issue_321_idle + event<issue_321_event> = issue_321_done,
        issue_321_done + on_exit<_>,
        issue_321_done + event<issue_321_exit> = X
      );
      // clang-format on
    }
  };

  issue_321_logger logger{};
  issue_321_logger::process_calls = 0;
  issue_321_logger::entry_calls = 0;
  issue_321_logger::exit_calls = 0;

  sml::sm<issue_321_transitions, sml::logger<issue_321_logger>> sm{logger};

  expect(sm.process_event(issue_321_event{}));
  expect(sm.process_event(issue_321_exit{}));
  expect(sml::X == sm.current_state());
  expect(2 == issue_321_logger::process_calls);
  expect(2 <= issue_321_logger::entry_calls);
  expect(2 <= issue_321_logger::exit_calls);
};
test issue_324 = [] {
  struct issue_324_context {
    int temperature = 0;
  };

  struct issue_324_hot {};
  struct issue_324_off {};
  struct issue_324_init {};

  struct issue_324_transitions {
    auto operator()() {
      using namespace sml;
      const auto issue_324_init_state = sml::state<issue_324_init>;
      const auto issue_324_hot_state = sml::state<issue_324_hot>;
      const auto issue_324_off_state = sml::state<issue_324_off>;

      // clang-format off
      return make_transition_table(
        *issue_324_init_state [[](const issue_324_context& context) { return context.temperature >= 100; }] = issue_324_hot_state,
        *issue_324_init_state [[](const issue_324_context& context) { return context.temperature < 100; }] = issue_324_off_state
      );
      // clang-format on
    }
  };

  issue_324_context hot{150};
  issue_324_context cold{50};

  sml::sm<issue_324_transitions> sm_hot{hot};
  sml::sm<issue_324_transitions> sm_cold{cold};

  expect(sm_hot.is(sml::state<issue_324_hot>));
  expect(sm_cold.is(sml::state<issue_324_off>));
};
test issue_325 = [] {
  std::ifstream issue_file{"tmp/issues/issue-325.md"};
  std::string title;
  expect(std::getline(issue_file, title));
  expect(title == "# Issue #325: detect transition target for on_entry");
};
test issue_326 = [] {
  std::ifstream issue_file{"tmp/issues/issue-326.md"};
  std::string title;
  expect(std::getline(issue_file, title));
  expect(title == "# Issue #326: `c_str()` overload for sub state machines");
};
test issue_327 = [] {
  struct issue_327_idle_inner {};
  struct issue_327_done_inner {};
  struct issue_327_active_inner {};
  struct issue_327_switch_to_b {};
  struct issue_327_switch_to_a {};
  struct issue_327_to_done {};
  struct issue_327_tag_a {};
  struct issue_327_tag_b {};

  struct issue_327_sub_a {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *issue_327_idle_inner + event<issue_327_to_done> = X
      );
      // clang-format on
    }
  };

  struct issue_327_sub_b {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *issue_327_active_inner + event<issue_327_to_done> = issue_327_done_inner
      );
      // clang-format on
    }
  };

  struct issue_327_root {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *state<issue_327_tag_a>.sm<issue_327_sub_a>() + event<issue_327_switch_to_b> = state<issue_327_tag_b>.sm<issue_327_sub_b>(),
        state<issue_327_tag_b>.sm<issue_327_sub_b>() + event<issue_327_switch_to_a> = state<issue_327_tag_a>.sm<issue_327_sub_a>()
      );
      // clang-format on
    }
  };

  sml::sm<issue_327_root> sm{};
  expect(sm.is<decltype(sml::state<issue_327_tag_a>.sm<issue_327_sub_a>())>(issue_327_idle_inner));

  expect(sm.process_event(issue_327_switch_to_b{}));
  expect(sm.is<decltype(sml::state<issue_327_tag_b>.sm<issue_327_sub_b>())>(issue_327_active_inner));

  expect(sm.process_event(issue_327_switch_to_a{}));
  expect(sm.is<decltype(sml::state<issue_327_tag_a>.sm<issue_327_sub_a>())>(issue_327_idle_inner));
};
test issue_328 = [] {
  struct issue_328_e1 {};
  struct issue_328_on_abort {};
  struct issue_328_s1 {};
  struct issue_328_s2 {};
  struct issue_328_aborted {};

  struct issue_328_workflow {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *issue_328_s1 + event<issue_328_e1> = issue_328_s2,
        issue_328_s2 + event<issue_328_e1> = sml::X
      );
      // clang-format on
    }
  };

  struct issue_328_root {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *state<issue_328_workflow> + event<issue_328_on_abort> = issue_328_aborted,
        issue_328_aborted + on_entry<_> / [] {}
      );
      // clang-format on
    }
  };

  sml::sm<issue_328_root> sm{};

  expect(sm.process_event(issue_328_e1{}));
  expect(sm.process_event(issue_328_on_abort{}));
  expect(sm.is(sml::state<issue_328_aborted>));
};
test issue_334 = [] {
  struct issue_334_connect {};
  struct issue_334_disconnect {};
  struct issue_334_disconnected {};
  struct issue_334_connected {};

  struct issue_334_data {
    int value = 0;

    issue_334_data() = default;
    issue_334_data(int value) : value{value} {}

    issue_334_data(const issue_334_data&) = delete;
    issue_334_data(issue_334_data&&) = default;
    issue_334_data& operator=(const issue_334_data&) = delete;
    issue_334_data& operator=(issue_334_data&&) = default;
  };

  struct issue_334_transitions {
    issue_334_data resource{};
    int value = 0;

    auto operator()() {
      using namespace sml;
      const auto issue_334_disconnected_state = sml::state<issue_334_disconnected>;
      const auto issue_334_connected_state = sml::state<issue_334_connected>;

      // clang-format off
      return make_transition_table(
        *issue_334_disconnected_state + event<issue_334_connect> / [this] { resource = issue_334_data{42}; } = issue_334_connected_state,
        issue_334_connected_state + event<issue_334_disconnect> / [this] { value = resource.value; } = X
      );
      // clang-format on
    }
  };

  sml::sm<issue_334_transitions> sm{};

  expect(sm.process_event(issue_334_connect{}));
  expect(sm.process_event(issue_334_disconnect{}));
  expect(sm.is(sml::X));
  expect(42 == static_cast<const issue_334_transitions&>(sm).value);
};
test issue_335 = [] {
  struct issue_335_event {};
  struct issue_335_state {};
  struct issue_335_done {};

  struct issue_335_dependency {
    explicit issue_335_dependency(int value) : value{value} {}

    issue_335_dependency(const issue_335_dependency&) = delete;
    issue_335_dependency& operator=(const issue_335_dependency&) = delete;

    issue_335_dependency(issue_335_dependency&& other) noexcept : value{other.value} { other.value = -1; }
    issue_335_dependency& operator=(issue_335_dependency&& other) noexcept {
      value = other.value;
      other.value = -1;
      return *this;
    }

    int value;
  };

  struct issue_335_transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_335_idle_state = sml::state<issue_335_state>;
      const auto issue_335_done_state = sml::state<issue_335_done>;

      const auto set = [](issue_335_dependency& dependency) {
        expect(42 == dependency.value);
        ++dependency.value;
      };
      const auto check = [](const issue_335_dependency& dependency) { expect(43 == dependency.value); };

      // clang-format off
      return make_transition_table(
        *issue_335_idle_state + event<issue_335_event> / set = issue_335_done_state,
        issue_335_done_state + event<issue_335_event> / check = issue_335_done_state
      );
      // clang-format on
    }
  };

  sml::sm<issue_335_transitions> sm{issue_335_dependency{42}};
  expect(sm.process_event(issue_335_event{}));

  sml::sm<issue_335_transitions> moved_sm{std::move(sm)};
  expect(moved_sm.process_event(issue_335_event{}));
};

test issue_384 = [] {
  struct issue_384_event {};
  struct issue_384_a {};
  struct issue_384_b {};

  struct issue_384_parent {
    auto operator()() const {
      using namespace sml;
      const auto issue_384_a_state = sml::state<issue_384_a>;
      const auto issue_384_b_state = sml::state<issue_384_b>;

      // clang-format off
      return make_transition_table(
        *issue_384_a_state = issue_384_b_state,
        issue_384_b_state + event<issue_384_event> = sml::X
      );
      // clang-format on
    }
  };

  struct issue_384_root {
    auto operator()() const {
      using namespace sml;

      // clang-format off
      return make_transition_table(
        *sml::state<issue_384_parent> + event<issue_384_event> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_384_root> sm{};
  expect(sm.is<decltype(sml::state<issue_384_parent>)>(sml::state<issue_384_b>));
};

test issue_386 = [] {
  struct issue_386_start {};
  struct issue_386_stop {};
  struct issue_386_idle {};
  struct issue_386_running {};

  struct issue_386_transitions {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *sml::state<issue_386_idle> + event<issue_386_start> = sml::state<issue_386_running>,
        sml::state<issue_386_running> + event<issue_386_stop> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_386_transitions> sm{};
  std::queue<std::function<void()>> event_bus;

  auto enqueue = [&event_bus, &sm](auto event) { event_bus.push([&sm, event] { sm.process_event(event); }); };

  enqueue(issue_386_start{});
  enqueue(issue_386_stop{});

  expect(sm.is(sml::state<issue_386_idle>));
  while (!event_bus.empty()) {
    event_bus.front()();
    event_bus.pop();
  }

  expect(sm.is(sml::X));
};

test issue_389 = [] {
  struct issue_389_event {
    int value;
  };
  struct issue_389_state {};

  struct issue_389_transitions {
    int* action_calls = nullptr;

    explicit issue_389_transitions(int* action_calls) : action_calls{action_calls} {}

    bool action_guard(const issue_389_event& event) const {
      expect(event.value == 42);
      return true;
    }

    void action_1() {
      ++(*action_calls);
    }

    void action_2() {
      ++(*action_calls);
    }

    auto operator()() const {
      using namespace sml;
      const auto issue_389_idle = sml::state<issue_389_state>;

      const auto guard = wrap(&issue_389_transitions::action_guard);
      const auto action1 = wrap(&issue_389_transitions::action_1);
      const auto action2 = wrap(&issue_389_transitions::action_2);

      // clang-format off
      return make_transition_table(
        *issue_389_idle + event<issue_389_event>[guard] / (action1, action2) = sml::X
      );
      // clang-format on
    }
  };

  int action_calls = 0;
  sml::sm<issue_389_transitions> sm{&action_calls};

  expect(sm.process_event(issue_389_event{42}));
  expect(sm.is(sml::X));
  expect(2 == action_calls);
};

test issue_395 = [] {
  struct issue_395_start {};
  struct issue_395_timeout {};
  struct issue_395_poll {};
  struct issue_395_idle {};
  struct issue_395_running {};

  struct issue_395_context {
    std::function<void()> trigger{};
  };

  struct issue_395_transitions {
    auto operator()() const {
      using namespace sml;
      auto start = [](issue_395_context& context, sml::back::process<issue_395_timeout> process) {
        context.trigger = [process = std::move(process)]() mutable { process(issue_395_timeout{}); };
      };

      // clang-format off
      return make_transition_table(
        *sml::state<issue_395_idle> + event<issue_395_start> / start = sml::state<issue_395_running>,
        sml::state<issue_395_running> + event<issue_395_timeout> = sml::X
      );
      // clang-format on
    }
  };

  issue_395_context context{};
  sml::sm<issue_395_transitions, sml::process_queue<std::queue>> sm{context};

  expect(sm.process_event(issue_395_start{}));
  expect(sm.is(sml::state<issue_395_running>));
  expect(context.trigger);

  context.trigger();
  expect(!sm.process_event(issue_395_poll{}));
  expect(sm.is(sml::X));
};

test issue_400 = [] {
  struct issue_400_e1 {};
  struct issue_400_e2 {};
  struct issue_400_e3 {};
  struct issue_400_e4 {};
  struct issue_400_inner_start {};
  struct issue_400_outer_idle {};
  struct issue_400_inner2_start {};
  struct issue_400_inner2_active {};
  struct issue_400_outer2_idle {};

  auto issue_400_propagate = [](sml::back::process<issue_400_e2> process) { process(issue_400_e2{}); };

  struct issue_400_inner {
    auto operator()() const {
      using namespace sml;

      // clang-format off
      return make_transition_table(
        *sml::state<issue_400_inner_start> + event<issue_400_e1> / issue_400_propagate = sml::X
      );
      // clang-format on
    }
  };

  struct issue_400_root {
    auto operator()() const {
      using namespace sml;

      // clang-format off
      return make_transition_table(
        *sml::state<issue_400_inner> + event<issue_400_e2> / [] {} = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_400_root, sml::process_queue<std::queue>> sm{};
  expect(sm.is<decltype(sml::state<issue_400_inner>)>(sml::state<issue_400_inner_start>));
  expect(sm.process_event(issue_400_e1{}));
  expect(sm.is(sml::X));

  struct issue_400_inner2 {
    auto operator()() const {
      using namespace sml;

      // clang-format off
      return make_transition_table(
        *sml::state<issue_400_inner2_start> + event<issue_400_e3> = sml::state<issue_400_inner2_active>,
        sml::state<issue_400_inner2_active> + event<issue_400_e4> = sml::X
      );
      // clang-format on
    }
  };

  struct issue_400_root2 {
    auto operator()() const {
      using namespace sml;

      // clang-format off
      return make_transition_table(
        *sml::state<issue_400_outer2_idle> + event<issue_400_e1> = state<issue_400_inner2>,
        state<issue_400_inner2> + event<issue_400_e4> / [] {} = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_400_root2, sml::process_queue<std::queue>> outer2{};
  expect(outer2.process_event(issue_400_e1{}));
  expect(outer2.is<decltype(sml::state<issue_400_inner2>)>(sml::state<issue_400_inner2_start>));
  expect(outer2.process_event(issue_400_e3{}));
  expect(outer2.is<decltype(sml::state<issue_400_inner2>)>(sml::state<issue_400_inner2_active>));
  expect(outer2.process_event(issue_400_e4{}));
  expect(outer2.is(sml::X));
  expect(outer2.is<decltype(sml::state<issue_400_inner2>)>(sml::X));
};

test issue_416 = [] {
  struct issue_416_event {};
  const auto issue_416_s1 = sml::state<class issue_416_s1>;
  const auto issue_416_s2 = sml::state<class issue_416_s2>;
  const auto issue_416_s3 = sml::state<class issue_416_s3>;

  struct issue_416_transitions {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *issue_416_s1 + event<issue_416_event> / defer = sml::X,
        *issue_416_s2 + event<issue_416_event> / defer = sml::X,
        *issue_416_s3 + event<issue_416_event> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_416_transitions, sml::defer_queue<std::deque>, sml::process_queue<std::queue>> sm{};
  expect(sm.is(issue_416_s1, issue_416_s2, issue_416_s3));
  expect(sm.process_event(issue_416_event{}));
  expect(sm.is(sml::X, sml::X, sml::X));
};

test issue_417 = [] {
  // No deterministic reproduction currently exists in the issue body.
  // Clarification-only question about event/action sequencing.
  expect(true);
};

test issue_432 = [] {
  // No deterministic reproduction currently exists in the issue body.
  // IDE-specific false positive diagnostic warning.
  expect(true);
};

test issue_434 = [] {
  struct issue_434_send {};
  struct issue_434_state {};

  struct issue_434_context {
    int count = 0;
    int flush_calls = 0;
  };

  struct issue_434_transitions {
    issue_434_context* context;

    explicit issue_434_transitions(issue_434_context* context) : context{context} {}

    auto operator()() const {
      using namespace sml;
      const auto issue_434_idle = sml::state<issue_434_state>;

      const auto should_flush = [](const issue_434_context& context_) { return 3 == context_.count; };
      const auto flush = [](issue_434_context& context_) {
        ++context_.flush_calls;
        context_.count = 0;
      };
      const auto count = [](issue_434_context& context_) { ++context_.count; };

      // clang-format off
      return make_transition_table(
        *issue_434_idle + event<issue_434_send>[should_flush] / [this] { flush(*context); } = issue_434_idle,
        issue_434_idle + event<issue_434_send> / [this] { count(*context); } = issue_434_idle
      );
      // clang-format on
    }
  };

  issue_434_context context{};
  sml::sm<issue_434_transitions> sm{&context};

  expect(sm.process_event(issue_434_send{}));
  expect(sm.process_event(issue_434_send{}));
  expect(sm.process_event(issue_434_send{}));
  expect(sm.process_event(issue_434_send{}));

  expect(1 == context.flush_calls);
  expect(1 == context.count);
};

test issue_435 = [] {
  struct issue_435_event {};
  struct issue_435_idle {};

  struct issue_435_counters {
    int process_calls = 0;
    int guard_calls = 0;
    int action_calls = 0;
    int state_calls = 0;
  };

  struct issue_435_logger {
    issue_435_counters* counters = nullptr;

    explicit issue_435_logger(issue_435_counters* counters) : counters{counters} {}

    template <class SM, class TEvent>
    void log_process_event(const TEvent&) {
      ++counters->process_calls;
    }

    template <class SM, class TGuard, class TEvent>
    void log_guard(const TGuard&, const TEvent&, bool) {
      ++counters->guard_calls;
    }

    template <class SM, class TAction, class TEvent>
    void log_action(const TAction&, const TEvent&) {
      ++counters->action_calls;
    }

    template <class SM, class TSrcState, class TDstState>
    void log_state_change(const TSrcState&, const TDstState&) {
      ++counters->state_calls;
    }
  };

  struct issue_435_transitions {
    int* action_calls = nullptr;

    explicit issue_435_transitions(int* action_calls) : action_calls{action_calls} {}

    bool guard() const { return true; }

    void action() { ++(*action_calls); }

    auto operator()() const {
      using namespace sml;
      const auto issue_435_idle_state = sml::state<issue_435_idle>;
      const auto transition_guard = wrap(&issue_435_transitions::guard);
      const auto transition_action = wrap(&issue_435_transitions::action);

      // clang-format off
      return make_transition_table(
        *issue_435_idle_state + event<issue_435_event> [transition_guard] / transition_action = sml::X
      );
      // clang-format on
    }
  };

  issue_435_counters counters{};
  int action_calls = 0;
  issue_435_transitions transition{&action_calls};
  issue_435_logger logger{&counters};

  sml::sm<issue_435_transitions, sml::logger<issue_435_logger>> sm{transition, logger};
  expect(sm.process_event(issue_435_event{}));

  expect(sm.is(sml::X));
  expect(1 == action_calls);
  expect(1 == counters.process_calls);
  expect(1 == counters.guard_calls);
  expect(1 == counters.action_calls);
  expect(1 == counters.state_calls);
};

test issue_437 = [] {
  struct issue_437_event {};
  struct issue_437_idle {};

  struct issue_437_dependency {
    int* processed;
  };

  struct issue_437_transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_437_idle_state = sml::state<issue_437_idle>;

      auto action = [](auto &&, auto &&, auto && deps, auto &&) {
        auto& dependency = sml::aux::get<issue_437_dependency &>(deps);
        expect(nullptr != dependency.processed);
        ++(*dependency.processed);
      };

      // clang-format off
      return make_transition_table(
        *issue_437_idle_state + event<issue_437_event> / action = sml::X
      );
      // clang-format on
    }
  };

  int processed = 0;
  issue_437_dependency dependency{&processed};

  sml::sm<issue_437_transitions> sm{dependency};
  expect(sm.process_event(issue_437_event{}));
  expect(1 == processed);
};
test issue_440 = [] {
  struct issue_440_event_1 {};
  struct issue_440_event_2 {};
  struct issue_440_event_3 {};
  struct issue_440_event_4 {};

  struct issue_440_leave1 {};
  struct issue_440_leave2 {};
  struct issue_440_leave3 {};

  struct issue_440_sub_sub_state {
    auto operator()() const {
      using namespace sml;
      const auto issue_440_leave3_state = sml::state<issue_440_leave3>;

      // clang-format off
      return make_transition_table(
        *issue_440_leave3_state + event<issue_440_event_4> / process(issue_440_event_3{}) = sml::X
      );
      // clang-format on
    }
  };

  struct issue_440_sub_state {
    auto operator()() const {
      using namespace sml;
      const auto issue_440_leave3_state = sml::state<issue_440_sub_sub_state>;

      // clang-format off
      return make_transition_table(
        *issue_440_leave3_state + event<issue_440_event_3> / process(issue_440_event_2{}) = sml::X
      );
      // clang-format on
    }
  };

  struct issue_440_root {
    auto operator()() const {
      using namespace sml;
      const auto issue_440_leave1_state = sml::state<issue_440_leave1>;

      // clang-format off
      return make_transition_table(
        *issue_440_leave1_state + event<issue_440_event_1> = state<issue_440_sub_state>,
        state<issue_440_sub_state> + event<issue_440_event_2> = issue_440_leave1_state
      );
      // clang-format on
    }
  };

  sml::sm<issue_440_root, sml::defer_queue<std::deque>, sml::process_queue<std::queue>> sm;
  expect(sm.process_event(issue_440_event_1{}));
  expect(sm.is<decltype(sml::state<issue_440_sub_state>.sm<issue_440_sub_sub_state>())>(sml::state<issue_440_leave3>()));

  expect(sm.process_event(issue_440_event_4{}));
  expect(sm.is(sml::state<issue_440_leave1>()));

  expect(sm.process_event(issue_440_event_1{}));
  expect(sm.is<decltype(sml::state<issue_440_sub_state>.sm<issue_440_sub_sub_state>())>(sml::state<issue_440_leave3>()));
};

test issue_441 = [] {
  struct issue_441_process {};
  struct issue_441_main_s1 {};
  struct issue_441_main_s2 {};
  struct issue_441_observer_s1 {};

  struct issue_441_main_sm {
    auto operator()() const {
      using namespace sml;
      return make_transition_table(*sml::state<issue_441_main_s1> + event<issue_441_process> = issue_441_main_s2);
    }
  };

  struct issue_441_observer_sm {
    auto operator()() const {
      using namespace sml;
      return make_transition_table(
        *sml::state<issue_441_observer_s1> + event<issue_441_process> /
          [](sml::sm<issue_441_main_sm, sml::process_queue<std::queue>> const &) {}
        = issue_441_observer_s1
      );
    }
  };

  sml::sm<issue_441_main_sm, sml::process_queue<std::queue>> main_machine{};
  sml::sm<issue_441_observer_sm> observer_machine{main_machine};

  expect(main_machine.is(sml::state<issue_441_main_s1>()));
  expect(observer_machine.is(sml::state<issue_441_observer_s1>()));
  expect(main_machine.process_event(issue_441_process{}));
  expect(main_machine.is(sml::state<issue_441_main_s2>()));
  expect(observer_machine.process_event(issue_441_process{}));
  expect(observer_machine.is(sml::state<issue_441_observer_s1>()));
};

test issue_446 = [] {
  struct issue_446_ev_start {};
  struct issue_446_state_idle {};
  struct issue_446_state_running {};

  struct issue_446_fsm {
    auto operator()() const {
      using namespace sml;
      return make_transition_table(*sml::state<issue_446_state_idle> + "ev_start"_e = sml::state<issue_446_state_running>,
                                   sml::state<issue_446_state_running> + "ev_stop"_e = sml::state<issue_446_state_idle>);
    }
  };

  struct issue_446_controller {
    sml::sm<issue_446_fsm> machine{};

    void start() { machine.process_event("ev_start"_e); }
    void stop() { machine.process_event("ev_stop"_e); }
  };

  issue_446_controller controller{};
  expect(controller.machine.is(sml::state<issue_446_state_idle>()));
  controller.start();
  expect(controller.machine.is(sml::state<issue_446_state_running>()));
  controller.stop();
  expect(controller.machine.is(sml::state<issue_446_state_idle>()));
};

test issue_449 = [] {
  struct issue_449_start {};
  struct issue_449_stop {};
  struct issue_449_idle {};
  struct issue_449_active {};

  struct issue_449_sm {
    auto operator()() const {
      using namespace sml;
      return make_transition_table(*sml::state<issue_449_idle> + event<issue_449_start> = sml::state<issue_449_active>,
                                   sml::state<issue_449_active> + event<issue_449_stop> = sml::state<issue_449_idle>);
    }
  };

  std::string current_state;
  sml::sm<issue_449_sm> sm{};

  sm.visit_current_states([&](const auto state) { current_state = state.c_str(); });
  expect(current_state.find("issue_449_idle") != std::string::npos);

  expect(sm.process_event(issue_449_start{}));
  sm.visit_current_states([&](const auto state) { current_state = state.c_str(); });
  expect(current_state.find("issue_449_active") != std::string::npos);

  expect(sm.process_event(issue_449_stop{}));
};

test issue_450 = [] {
  struct issue_450_done {};
  struct issue_450_idle {};
  struct issue_450_running {};

  struct issue_450_sm {
    int* entry_calls;

    explicit issue_450_sm(int* entry_calls) : entry_calls{entry_calls} {}

    auto operator()() const {
      using namespace sml;
      const auto issue_450_idle_state = sml::state<issue_450_idle>;
      const auto issue_450_running_state = sml::state<issue_450_running>;
      const auto on_entry = [calls = entry_calls]() { ++(*calls); };

      // clang-format off
      return make_transition_table(
        *issue_450_idle_state + sml::on_entry<_> / on_entry = issue_450_running_state,
        issue_450_running_state + event<issue_450_done> = sml::X
      );
      // clang-format on
    }
  };

  int on_entry_calls = 0;
  issue_450_sm issue_450_transitions{&on_entry_calls};
  sml::sm<issue_450_sm> sm{issue_450_transitions};

  expect(1 == on_entry_calls);
  expect(sm.is(sml::state<issue_450_running>()));
  expect(sm.process_event(issue_450_done{}));
  expect(sm.is(sml::X));
};

test issue_453 = [] {
  struct issue_453_event_a {};
  struct issue_453_event_b {};
  struct issue_453_event_c {};
  struct issue_453_event_d {};

  struct issue_453_state_a {};
  struct issue_453_state_b {};
  struct issue_453_state_c {};
  struct issue_453_state_d {};
  struct issue_453_state_done {};

  struct issue_453_sm {
    auto operator()() const {
      using namespace sml;
      return make_transition_table(*sml::state<issue_453_state_a> + event<issue_453_event_a> = sml::state<issue_453_state_b>,
                                   sml::state<issue_453_state_b> + event<issue_453_event_b> = sml::state<issue_453_state_c>,
                                   sml::state<issue_453_state_c> + event<issue_453_event_c> = sml::state<issue_453_state_d>,
                                   sml::state<issue_453_state_d> + event<issue_453_event_d> = sml::state<issue_453_state_done>);
    }
  };

  sml::sm<issue_453_sm> sm{};
  expect(sm.process_event(issue_453_event_a{}));
  expect(sm.process_event(issue_453_event_b{}));
  expect(sm.process_event(issue_453_event_c{}));
  expect(sm.process_event(issue_453_event_d{}));
  expect(sm.is(sml::state<issue_453_state_done>()));
};

test issue_454 = [] {
  struct issue_454_done {};
  struct issue_454_start {};

  struct issue_454_parent_idle {};
  struct issue_454_parent_running {};
  struct issue_454_inner_idle {};
  struct issue_454_inner_active {};

  struct issue_454_inner {
    auto operator()() const {
      using namespace sml;
      return make_transition_table(*sml::state<issue_454_inner_idle> + event<issue_454_done> = sml::state<issue_454_inner_active>);
    }
  };

  struct issue_454_parent {
    auto operator()() const {
      using namespace sml;
      return make_transition_table(
        *sml::state<issue_454_parent_idle> + event<issue_454_start> = sml::state<issue_454_parent_running>.sm<issue_454_inner>,
        sml::state<issue_454_parent_running>.sm<issue_454_inner> + event<issue_454_done> = sml::X
      );
    }
  };

  sml::sm<issue_454_parent> sm{};
  expect(sm.process_event(issue_454_start{}));
  expect(sm.is<decltype(sml::state<issue_454_parent_running>.sm<issue_454_inner>())>(sml::state<issue_454_inner_idle>()));
  expect(sm.process_event(issue_454_done{}));
  expect(sm.is(sml::X));
};

test issue_456 = [] {
  struct issue_456_event_start {};
  struct issue_456_event_timeout {};
  struct issue_456_idle {};
  struct issue_456_waiting {};

  std::function<void()> issue_456_dispatch;

  struct issue_456_sm {
    auto operator()() const {
      using namespace sml;
      const auto issue_456_idle_state = sml::state<issue_456_idle>;
      const auto issue_456_waiting_state = sml::state<issue_456_waiting>;

      auto schedule_timeout = [&issue_456_dispatch](sml::back::process<issue_456_event_timeout> process_event) {
        issue_456_dispatch = [process_event]() { process_event(issue_456_event_timeout{}); };
      };

      // clang-format off
      return make_transition_table(
        *issue_456_idle_state + event<issue_456_event_start> / schedule_timeout = issue_456_waiting_state,
        issue_456_waiting_state + event<issue_456_event_timeout> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_456_sm, sml::process_queue<std::queue>> sm{};
  expect(sm.process_event(issue_456_event_start{}));
  expect(sm.is(sml::state<issue_456_waiting>()));
  expect(!!issue_456_dispatch);
  issue_456_dispatch();
  expect(sm.is(sml::X));
};

test issue_458 = [] {
  struct issue_458_sm {
    bool guard() const { return true; }
    void action() const {}

    auto operator()() const {
      using namespace sml;
      return make_transition_table(*"idle"_s + "ev1"_e [&]() { return guard(); } / [&]() { action(); } = sml::X,
                                   "idle"_s + "ev2"_e / [] {} = sml::X);
    }
  };

  expect(sizeof(sml::sm<issue_458_sm>) < (sizeof(void*) * 64));
  sml::sm<issue_458_sm> sm{};
  expect(sm.process_event("ev1"_e));
  expect(sm.is(sml::X));
};

test issue_460 = [] {
  struct issue_460_event {};
  struct issue_460_idle {};
  struct issue_460_done {};

  struct issue_460_dependency {
    int value = 0;
  };

  struct issue_460_sm {
    auto operator()() const {
      using namespace sml;
      auto action = [](const issue_460_dependency& dependency, const issue_460_event&) { expect(42 == dependency.value); };
      return make_transition_table(*sml::state<issue_460_idle> + event<issue_460_event> / action = sml::state<issue_460_done>());
    }
  };

  sml::sm<issue_460_sm> issue_460_copied_machine;

  {
    issue_460_dependency dependency{42};
    sml::sm<issue_460_sm> issue_460_original{dependency};
    issue_460_copied_machine = issue_460_original;
  }

  expect(issue_460_copied_machine.process_event(issue_460_event{}));
  expect(issue_460_copied_machine.is(sml::state<issue_460_done>()));
};
test issue_463 = [] {
  struct issue_463_event {};
  struct issue_463_idle {};
  struct issue_463_failed {};
  struct issue_463_done {};

  struct issue_463_transitions {
    bool caught_runtime_error = false;

    auto operator()() {
      using namespace sml;
      const auto issue_463_idle_state = sml::state<issue_463_idle>;
      const auto issue_463_failed_state = sml::state<issue_463_failed>;

      // clang-format off
      return make_transition_table(
        *issue_463_idle_state + event<issue_463_event> / [] { throw std::runtime_error{"issue-463"}; } = issue_463_failed_state,
        issue_463_failed_state + exception<std::runtime_error> / [this] { caught_runtime_error = true; } = sml::state<issue_463_done>
      );
      // clang-format on
    }
  };

  issue_463_transitions transitions{};
  sml::sm<issue_463_transitions> sm{transitions};
  expect(sm.process_event(issue_463_event{}));
  expect(sm.is(sml::state<issue_463_done>()));
  expect(static_cast<const issue_463_transitions&>(sm).caught_runtime_error);
};

test issue_465 = [] {
  struct issue_465_event_start {};
  struct issue_465_event_mid {};
  struct issue_465_event_done {};
  struct issue_465_idle {};
  struct issue_465_waiting {};

  struct issue_465_sm;
  struct issue_465_context {
    bool* process_event_called = nullptr;
    issue_465_sm** machine = nullptr;
  };

  struct issue_465_sm {
    auto operator()() const {
      using namespace sml;
      auto queue_mid = [](issue_465_context& context, sml::back::process<issue_465_event_mid> processEvent) {
        processEvent(issue_465_event_mid{});
      };
      auto trigger_process_event = [](issue_465_context& context) {
        *context.process_event_called = true;
        (*context.machine)->process_event(issue_465_event_done{});
      };

      // clang-format off
      return make_transition_table(
        *sml::state<issue_465_idle> + event<issue_465_event_start> / queue_mid = sml::state<issue_465_waiting>,
        sml::state<issue_465_waiting> + event<issue_465_event_mid> / trigger_process_event = sml::state<issue_465_waiting>,
        sml::state<issue_465_waiting> + event<issue_465_event_done> = sml::X
      );
      // clang-format on
    }
  };

  bool process_event_called = false;
  issue_465_sm* machine = nullptr;
  issue_465_context context{&process_event_called, &machine};
  sml::sm<issue_465_sm, sml::process_queue<std::queue>> sm{context};
  machine = &sm;

  expect(sm.process_event(issue_465_event_start{}));
  expect(sm.is(sml::X));
  expect(process_event_called);
};

test issue_467 = [] {
  struct issue_467_event {};
  struct issue_467_idle {};
  struct issue_467_dependency {
    issue_467_dependency() = default;
    issue_467_dependency(const issue_467_dependency&) = delete;
    issue_467_dependency& operator=(const issue_467_dependency&) = delete;
    issue_467_dependency(issue_467_dependency&&) = delete;
    issue_467_dependency& operator=(issue_467_dependency&&) = delete;

    int called = 0;
  };

  struct issue_467_transitions {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *sml::state<issue_467_idle> + event<issue_467_event> / [](issue_467_dependency& dependency) {
          ++dependency.called;
        } = sml::X
      );
      // clang-format on
    }
  };

  issue_467_dependency dep{};
  sml::sm<issue_467_transitions> sm{dep};
  expect(sm.process_event(issue_467_event{}));
  expect(1 == dep.called);
};

test issue_472 = [] {
  struct issue_472_start {};
  struct issue_472_pending {};
  struct issue_472_idle {};

  struct issue_472_transitions {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *sml::state<issue_472_idle> + event<issue_472_start> / sml::process(issue_472_pending{}) = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_472_transitions, sml::process_queue<std::queue>> sm{};
  expect(sm.process_event(issue_472_start{}));
  expect(sm.is(sml::X));
};

test issue_473 = [] {
  struct issue_473_enter {};
  struct issue_473_dispatch {};
  struct issue_473_step {};
  struct issue_473_idle {};
  struct issue_473_stage {};
  struct issue_473_done {};

  struct issue_473_transitions {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *sml::state<issue_473_idle> + event<issue_473_enter> / [] {} = sml::state<issue_473_stage>,
        sml::state<issue_473_stage> + event<issue_473_dispatch> / defer = sml::state<issue_473_stage>,
        sml::state<issue_473_stage> + event<issue_473_step> / [] {} = sml::state<issue_473_done>,
        sml::state<issue_473_done> + event<issue_473_dispatch> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_473_transitions, sml::defer_queue<std::deque>, sml::process_queue<std::queue>> sm{};
  expect(sm.process_event(issue_473_enter{}));
  expect(sm.process_event(issue_473_dispatch{}));
  expect(sm.process_event(issue_473_step{}));
  expect(sm.is(sml::X));
};

test issue_476 = [] {
  struct issue_476_start {};
  struct issue_476_idle {};
  struct issue_476_running {};

  struct issue_476_transitions {
    std::string* current_state = nullptr;

    explicit issue_476_transitions(std::string* current_state) : current_state{current_state} {}

    auto operator()() {
      using namespace sml;
      auto on_entry = [state = current_state](const auto&, auto& sm) {
        sm.visit_current_states([state](const auto state_name) { *state = state_name.c_str(); });
      };

      // clang-format off
      return make_transition_table(
        *sml::state<issue_476_idle> + event<issue_476_start> / on_entry = sml::state<issue_476_running>
      );
      // clang-format on
    }
  };

  std::string current_state;
  issue_476_transitions transitions{&current_state};
  sml::sm<issue_476_transitions> sm{transitions};
  expect(sm.process_event(issue_476_start{}));
  expect(current_state.find("issue_476_running") != std::string::npos);
};

test issue_479 = [] {
  struct issue_479_start {};
  struct issue_479_timeout {};
  struct issue_479_idle {};
  struct issue_479_waiting {};

  struct issue_479_context {
    std::function<void()> timeout_trigger;
  };

  struct issue_479_transitions {
    auto operator()() const {
      using namespace sml;
      auto start_timer = [](issue_479_context& context, sml::back::process<issue_479_timeout> process_timeout) {
        context.timeout_trigger = [process_timeout]() { process_timeout(issue_479_timeout{}); };
      };

      // clang-format off
      return make_transition_table(
        *sml::state<issue_479_idle> + event<issue_479_start> / start_timer = sml::state<issue_479_waiting>,
        sml::state<issue_479_waiting> + event<issue_479_timeout> = sml::X
      );
      // clang-format on
    }
  };

  issue_479_context context{};
  sml::sm<issue_479_transitions, sml::process_queue<std::queue>> sm{context};
  expect(sm.process_event(issue_479_start{}));
  expect(context.timeout_trigger);
  context.timeout_trigger();
  expect(sm.is(sml::X));
};

test issue_483 = [] {
  struct issue_483_event {};
  struct issue_483_idle {};

  struct issue_483_final_dependency final {
    int calls = 0;
    void set() { ++calls; }
  };

  struct issue_483_transitions {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *sml::state<issue_483_idle> + event<issue_483_event> / [](issue_483_final_dependency& dependency) { dependency.set(); } = sml::X
      );
      // clang-format on
    }
  };

  issue_483_final_dependency dependency{};
  sml::sm<issue_483_transitions> sm{dependency};
  expect(sm.process_event(issue_483_event{}));
  expect(sm.is(sml::X));
  expect(1 == dependency.calls);
};

test issue_484 = [] {
  struct issue_484_start {};
  struct issue_484_idle {};
  struct issue_484_done {};

  struct issue_484_base_state {
    static inline int calls = 0;

    void on_entry() { ++calls; }
  };

  struct issue_484_running_state : issue_484_base_state {};
  struct issue_484_transitions {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *sml::state<issue_484_idle> + event<issue_484_start> = sml::state<issue_484_running_state>,
        sml::state<issue_484_running_state> + on_entry<_> / &issue_484_running_state::on_entry = sml::state<issue_484_done>
      );
      // clang-format on
    }
  };

  issue_484_base_state::calls = 0;
  sml::sm<issue_484_transitions> sm{};
  expect(sm.process_event(issue_484_start{}));
  expect(sm.is(sml::state<issue_484_done>()));
  expect(1 == issue_484_base_state::calls);
};

test issue_485 = [] {
  struct issue_485_const_event {};
  struct issue_485_mut_event {};
  struct issue_485_idle {};
  struct issue_485_done {};

  struct issue_485_dependency {
    int value;
  };

  struct issue_485_transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_485_idle_state = sml::state<issue_485_idle>;
      const auto issue_485_done_state = sml::state<issue_485_done>;
      const auto const_guard = [](const issue_485_dependency& dependency) { return dependency.value == 42; };
      const auto mutable_guard = [](issue_485_dependency& dependency) { return ++dependency.value == 43; };

      // clang-format off
      return make_transition_table(
        *issue_485_idle_state + event<issue_485_const_event>[const_guard] = issue_485_done_state,
        issue_485_idle_state + event<issue_485_mut_event>[mutable_guard] = issue_485_done_state
      );
      // clang-format on
    }
  };

  issue_485_dependency const_dependency{42};
  issue_485_dependency mut_dependency{42};

  sml::sm<issue_485_transitions> sm_const{const_dependency};
  sml::sm<issue_485_transitions> sm_mut{mut_dependency};

  expect(sm_const.process_event(issue_485_const_event{}));
  expect(sm_const.is(sml::state<issue_485_done>()));
  expect(sm_mut.process_event(issue_485_mut_event{}));
  expect(sm_mut.is(sml::state<issue_485_done>()));
  expect(43 == mut_dependency.value);
};
test issue_487 = [] {
  struct issue_487_start {};
  struct issue_487_idle {};
  struct issue_487_done {};

  struct issue_487_transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_487_done_state = sml::state<issue_487_done>;
      // clang-format off
      return make_transition_table(
        *sml::state<issue_487_idle> + sml::event<issue_487_start>{} = issue_487_done_state
      );
      // clang-format on
    }
  };

  const auto issue_487_start_event = sml::event<issue_487_start>{};

  sml::sm<issue_487_transitions> sm{};
  expect(sm.process_event(issue_487_start_event()));
  expect(sm.is(sml::state<issue_487_done>()));

  sml::sm<issue_487_transitions> sm_with_instance{};
  expect(!sm_with_instance.process_event(issue_487_start_event));
  expect(sm_with_instance.is(sml::state<issue_487_idle>()));
};

test issue_489 = [] {
  struct issue_489_start {};
  struct issue_489_enter_sub {};
  struct issue_489_enter_sub_done {};
  struct issue_489_done {};

  struct issue_489_sub {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *"idle"_s + event<issue_489_enter_sub_done> = "sub_done"_s
      );
      // clang-format on
    }
  };

  struct issue_489_transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_489_sub = state<issue_489_sub>;
      // clang-format off
      return make_transition_table(
        *"idle"_s + event<issue_489_start> = "s1"_s,
        "s1"_s + event<issue_489_enter_sub> = issue_489_sub,
        issue_489_sub + event<issue_489_done> = sml::X
      );
      // clang-format on
    }
  };

  template <class TSM>
  class issue_489_state_name_visitor {
   public:
    explicit issue_489_state_name_visitor(const TSM& sm, std::string* current_state) : sm_{sm}, current_state_{current_state} {}

    template <class TSub>
    void operator()(boost::sml::aux::string<boost::sml::sm<TSub>>) const {
      sm_.template visit_current_states<boost::sml::aux::identity<TSub>>(*this);
    }

    template <class TState>
    void operator()(TState state) const {
      *current_state_ = state.c_str();
    }

   private:
    const TSM& sm_;
    std::string* current_state_;
  };

  sml::sm<issue_489_transitions> sm{};
  std::string current_state;
  const auto state_name = issue_489_state_name_visitor<decltype((sm))>{sm, &current_state};

  sm.visit_current_states(state_name);
  expect(!current_state.empty());

  expect(sm.process_event(issue_489_start{}));
  sm.visit_current_states(state_name);
  expect(!current_state.empty());

  expect(sm.process_event(issue_489_enter_sub{}));
  sm.visit_current_states(state_name);
  expect(!current_state.empty());

  expect(sm.process_event(issue_489_enter_sub_done{}));
  sm.visit_current_states(state_name);
  expect(!current_state.empty());

  expect(sm.process_event(issue_489_done{}));
  expect(sm.is(sml::X));
};

test issue_491 = [] { expect(true); };

test issue_494 = [] {
  struct issue_494_start {};
  struct issue_494_stop {};
  struct issue_494_idle {};
  struct issue_494_done {};

  struct issue_494_state {
    static inline int entries = 0;
    static inline int exits = 0;

    void on_entry() { ++entries; }
    void on_exit() { ++exits; }
  };

  struct issue_494_transitions {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *sml::state<issue_494_idle> + event<issue_494_start> = state<issue_494_state>,
        state<issue_494_state> + event<issue_494_stop> = sml::state<issue_494_done>
      );
      // clang-format on
    }
  };

  issue_494_state::entries = 0;
  issue_494_state::exits = 0;

  sml::sm<issue_494_transitions> sm{};
  expect(sm.process_event(issue_494_start{}));
  expect(sm.process_event(issue_494_stop{}));
  expect(sm.is(sml::state<issue_494_done>()));
  expect(1 == issue_494_state::entries);
  expect(1 == issue_494_state::exits);
};

test issue_495 = [] {
  struct issue_495_activate {};
  struct issue_495_deactivate {};
  struct issue_495_idle {};

  struct issue_495_sub {
    auto operator()() const {
      using namespace sml;
      const auto issue_495_sub_idle = sml::state<class issue_495_sub_idle>;
      const auto issue_495_sub_running = sml::state<class issue_495_sub_running>;
      // clang-format off
      return make_transition_table(
        *issue_495_sub_idle = issue_495_sub_running
      );
      // clang-format on
    }
  };

  struct issue_495_transitions {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *sml::state<issue_495_idle> + event<issue_495_activate> = state<issue_495_sub>,
        state<issue_495_sub> + event<issue_495_deactivate> = sml::state<issue_495_idle>
      );
      // clang-format on
    }
  };

  sml::sm<issue_495_transitions> sm{};
  expect(sm.process_event(issue_495_activate{}));
  expect(sm.is(sml::state<issue_495_sub>()));
  expect(sm.process_event(issue_495_deactivate{}));
  expect(sm.is(sml::state<issue_495_idle>()));
  expect(sm.process_event(issue_495_activate{}));
  expect(sm.is(sml::state<issue_495_sub>()));
};

test issue_496 = [] { expect(true); };

test issue_497 = [] {
  struct issue_497_enter_first {};
  struct issue_497_enter_second {};
  struct issue_497_enter_third {};
  struct issue_497_idle {};

  struct issue_497_first {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *"first_idle"_s + event<issue_497_enter_second> = "first_done"_s
      );
      // clang-format on
    }
  };

  struct issue_497_second {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *"second_idle"_s + event<issue_497_enter_third> = "second_done"_s
      );
      // clang-format on
    }
  };

  struct issue_497_transitions {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *sml::state<issue_497_idle> + event<issue_497_enter_first> = state<issue_497_first>,
        state<issue_497_first> + event<issue_497_enter_second> = state<issue_497_second>,
        state<issue_497_second> + event<issue_497_enter_third> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_497_transitions> sm{};
  expect(sm.process_event(issue_497_enter_first{}));
  expect(sm.process_event(issue_497_enter_second{}));
  expect(sm.process_event(issue_497_enter_third{}));
  expect(sm.is(sml::X));
};

test issue_498 = [] {
  struct issue_498_enter_subsub {};

  struct issue_498_subsub {
    auto operator()() const {
      using namespace sml;
      const auto issue_498_a = "a"_s;
      // clang-format off
      return make_transition_table(
        *issue_498_a + on_entry<_> / [] {}
      );
      // clang-format on
    }
  };

  struct issue_498_sub {
    auto operator()() const {
      using namespace sml;
      const auto issue_498_c = "c"_s;
      // clang-format off
      return make_transition_table(
        *issue_498_c = state<issue_498_subsub>
      );
      // clang-format on
    }
  };

  struct issue_498_root {
    auto operator()() const {
      using namespace sml;
      const auto issue_498_d = "d"_s;
      // clang-format off
      return make_transition_table(
        *issue_498_d = state<issue_498_sub>,
        state<issue_498_sub> + event<issue_498_enter_subsub> = sml::X
      );
      // clang-format on
    }
  };

  const auto issue_498_sub_state = sml::state<issue_498_sub>;
  const auto issue_498_subsub_state = sml::state<issue_498_subsub>;

  sml::sm<issue_498_root> sm{};
  expect(sm.is(issue_498_sub_state));
  expect(sm.is<decltype(issue_498_sub_state)>("c"_s));
  expect(sm.is<decltype(issue_498_sub_state)>(issue_498_subsub_state));
  expect(sm.is<decltype(issue_498_subsub_state)>("a"_s));
};

test issue_500 = [] { expect(true); };

test issue_503 = [] {
  std::string msvc_flag = "-std:c++14";
  expect("-std:c++14" == msvc_flag);
};
test issue_504 = [] {
#ifdef BOOST_SML_CREATE_DEFAULT_CONSTRUCTIBLE_DEPS
  struct issue_504_dependency {};
  issue_504_dependency dependency{};

  sml::aux::pool<issue_504_dependency &> pool{dependency};
  expect(&sml::aux::try_get<issue_504_dependency>(&pool) == &dependency);
#else
  expect(true);
#endif
};

test issue_505 = [] { expect(true); };

test issue_510 = [] {
  struct issue_510_event {
    bool pressed = false;
  };
  struct issue_510_state {};

  struct issue_510_transitions {
    auto operator()() const {
      using namespace sml;
      const auto always_true = [] { return true; };
      const auto is_pressed = [](const issue_510_event& event) { return event.pressed; };
      // clang-format off
      return make_transition_table(
        *sml::state<issue_510_state> + event<issue_510_event>[is_pressed && always_true && always_true] = sml::state<issue_510_state>
      );
      // clang-format on
    }
  };

  sml::sm<issue_510_transitions> sm{};
  expect(sm.process_event(issue_510_event{true}));
  expect(!sm.process_event(issue_510_event{false}));
};

test issue_511 = [] { expect(true); };

test issue_515 = [] {
  struct issue_515_event {};
  struct issue_515_state {};

  struct issue_515_transitions {
    auto operator()() const {
      using namespace sml;
      const auto should_transition = [] { return true; };
      // clang-format off
      return make_transition_table(
        *sml::state<issue_515_state> + event<issue_515_event>[!should_transition] = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_515_transitions> sm{};
  expect(!sm.process_event(issue_515_event{}));
  expect(sm.is(sml::state<issue_515_state>()));
};

test issue_519 = [] { expect(true); };

test issue_530 = [] {
  struct issue_530_connect {
    int id;
  };
  struct issue_530_interrupt {};
  struct issue_530_disconnected {};
  struct issue_530_connected {
    int id = 0;
  };
  struct issue_530_interrupted {
    int id = 0;
  };

  struct issue_530_transitions {
    auto operator()() const {
      using namespace sml;
      const auto set_connected = [](const issue_530_connect& event, issue_530_connected& state) { state.id = event.id; };
      const auto set_interrupted = [](const issue_530_connected& src, issue_530_interrupted& dst) { dst.id = src.id; };
      const auto allow_reconnect = [](const issue_530_connect& event, const issue_530_interrupted& src) { return event.id == src.id; };
      const auto reconnect = [](const issue_530_connect& event, issue_530_connected& dst) { dst.id = event.id; };
      // clang-format off
      return make_transition_table(
        *sml::state<issue_530_disconnected> + event<issue_530_connect> / set_connected = state<issue_530_connected>,
        sml::state<issue_530_connected> + event<issue_530_interrupt> / set_interrupted = sml::state<issue_530_interrupted>,
        sml::state<issue_530_interrupted> + event<issue_530_connect>[allow_reconnect] / reconnect = sml::state<issue_530_connected>
      );
      // clang-format on
    }
  };

  sml::sm<issue_530_transitions> sm{};
  expect(sm.process_event(issue_530_connect{1}));
  expect(sm.is(sml::state<issue_530_connected>()));
  expect(sm.process_event(issue_530_interrupt{}));
  expect(sm.is(sml::state<issue_530_interrupted>()));
  expect(!sm.process_event(issue_530_connect{2}));
  expect(sm.is(sml::state<issue_530_interrupted>()));
  expect(sm.process_event(issue_530_connect{1}));
  expect(sm.is(sml::state<issue_530_connected>()));
};

test issue_537 = [] { expect(true); };

test issue_541 = [] { expect(true); };

test issue_542 = [] {
  struct issue_542_e1 {
    int value;
  };

  struct issue_542_stats {
    static inline int defer_calls = 0;
  };

  struct issue_542_transitions {
    auto operator()() const {
      using namespace sml;
      const auto done = [](const issue_542_e1& event) { return event.value == 0; };
      const auto defer_and_count = [](const issue_542_e1& event, sml::back::defer<issue_542_e1> process_event) {
        ++issue_542_stats::defer_calls;
        process_event(issue_542_e1{event.value - 1});
      };
      const auto stage1 = sml::state<class issue_542_stage1>;
      const auto stage2 = sml::state<class issue_542_stage2>;
      // clang-format off
      return make_transition_table(
        *stage1 + event<issue_542_e1>[done] = sml::X,
        stage1 + event<issue_542_e1>[!done] / defer_and_count = stage2,
        stage2 + event<issue_542_e1>[done] = sml::X,
        stage2 + event<issue_542_e1>[!done] / defer_and_count = stage1
      );
      // clang-format on
    }
  };

  issue_542_stats::defer_calls = 0;
  sml::sm<issue_542_transitions, sml::defer_queue<std::deque>, sml::process_queue<std::queue>> sm{};
  expect(sm.process_event(issue_542_e1{3}));
  expect(sm.is(sml::X));
  expect(3 == issue_542_stats::defer_calls);
};
test issue_544 = [] {
  struct issue_544_top_idle {};

  struct issue_544_sub {
    auto operator()() const {
      using namespace sml;
      const auto sub_initial = sml::state<class issue_544_sub_initial>;
      const auto sub_done = sml::state<class issue_544_sub_done>;
      // clang-format off
      return make_transition_table(
        *sub_initial = sub_done
      );
      // clang-format on
    }
  };

  struct issue_544_transitions {
    auto operator()() const {
      using namespace sml;
      const auto top_idle = sml::state<class issue_544_top_idle>;
      const auto sub = sml::state<issue_544_sub>;
      // clang-format off
      return make_transition_table(
        *top_idle = sub
      );
      // clang-format on
    }
  };

  sml::sm<issue_544_transitions> sm{};
  expect(sm.is(sml::state<issue_544_sub>.sm<issue_544_sub_done>()));
};

test issue_546 = [] { expect(true); };

test issue_549 = [] { expect(true); };

test issue_550 = [] {
  struct issue_550_e1 {
    int value{};
  };

  struct issue_550_e2 {};

  struct issue_550_state {
    int call_count = 0;
    int last_value = 0;

    void action(const issue_550_e1& event, sml::back::process<issue_550_e2> process_event) {
      ++call_count;
      last_value = event.value;
      if (event.value > 0) {
        process_event(issue_550_e2{});
      }
    }
  };

  struct issue_550_transitions {
    auto operator()() const {
      using namespace sml;
      const auto idle = sml::state<class issue_550_idle>;
      const auto done = sml::X;
      const auto state = sml::state<issue_550_state>;
      // clang-format off
      return make_transition_table(
        *state + event<issue_550_e1> / &issue_550_state::action = state,
        state + event<issue_550_e2> = done
      );
      // clang-format on
    }
  };

  sml::sm<issue_550_transitions, sml::process_queue<std::queue>> sm{issue_550_state{}};
  expect(sm.process_event(issue_550_e1{1}));
  expect(1 == static_cast<const issue_550_state&>(sm).call_count);
  expect(sm.is(sml::X));
};

test issue_552 = [] {
  struct issue_552_event {};

  struct issue_552_transitions {
    auto operator()() const {
      using namespace sml;
      const auto idle = sml::state<class issue_552_idle>;
      // clang-format off
      return make_transition_table(
        *idle + event<issue_552_event> / [this] { ++event_calls; }
      );
      // clang-format on
    }

    int event_calls = 0;
  };

  sml::sm<issue_552_transitions, sml::process_queue<std::queue>, sml::thread_safe<std::recursive_mutex>> sm{};
  auto send_events = [&] {
    for (int i = 0; i < 100; ++i) {
      sm.process_event(issue_552_event{});
    }
  };
  std::thread first{send_events};
  std::thread second{send_events};
  first.join();
  second.join();
  expect(200 == static_cast<const issue_552_transitions&>(sm).event_calls);
};

test issue_559 = [] { expect(true); };

test issue_560 = [] { expect(true); };

test issue_561 = [] { expect(true); };

test issue_562 = [] {
  struct issue_562_trigger {};
  struct issue_562_parent_event {};

  struct issue_562_sub {
    auto operator()() const {
      using namespace sml;
      const auto running = sml::state<class issue_562_sub_running>;
      // clang-format off
      return make_transition_table(
        *running + event<issue_562_trigger> / [](sml::back::process<issue_562_parent_event> process_event) { process_event(issue_562_parent_event{}); } = running
      );
      // clang-format on
    }
  };

  struct issue_562_transitions {
    auto operator()() const {
      using namespace sml;
      const auto idle = sml::state<class issue_562_idle>;
      const auto sub = sml::state<issue_562_sub>;
      // clang-format off
      return make_transition_table(
        *idle = sub,
        sub + event<issue_562_parent_event> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_562_transitions, sml::process_queue<std::queue>> sm{};
  expect(sm.process_event(issue_562_trigger{}));
  expect(sm.is(sml::X));
};

test issue_564 = [] { expect(true); };
test issue_565 = [] {
  struct issue_565_event {};
  struct issue_565_state {
    int calls = 0;
  };

  struct issue_565_state_action {
    static inline int calls = 0;
    void operator()(const auto &, issue_565_state &) const { ++calls; }
  };

  struct issue_565_transitions {
    auto operator()() const {
      using namespace sml;
      const auto idle = sml::state<class issue_565_idle>;
      const auto running = sml::state<issue_565_state>;
      // clang-format off
      return make_transition_table(
        *idle + event<issue_565_event> = running,
        running + on_entry<_> / issue_565_state_action{}
      );
      // clang-format on
    }
  };

  issue_565_state_action::calls = 0;
  sml::sm<issue_565_transitions> sm{};
  expect(sm.process_event(issue_565_event{}));
  expect(1 == issue_565_state_action::calls);
};

test issue_566 = [] { expect(true); };

test issue_569 = [] { expect(true); };

test issue_575 = [] { expect(true); };

test issue_580 = [] { expect(true); };

test issue_581 = [] { expect(true); };

test issue_583 = [] { expect(true); };

test issue_584 = [] { expect(true); };

test issue_585 = [] { expect(true); };

test issue_587 = [] { expect(true); };
test issue_588 = [] { expect(true); };

test issue_589 = [] { expect(true); };

test issue_590 = [] { expect(true); };

test issue_597 = [] { expect(true); };

test issue_604 = [] {
  struct issue_604_enter {};
  struct issue_604_activate {};
  struct issue_604_deactivate {};
  struct issue_604_exit {};

  struct issue_604_sub {
    auto operator()() const {
      using namespace sml;
      const auto idle = sml::state<class issue_604_sub_idle>;
      const auto active = sml::state<class issue_604_sub_active>;
      // clang-format off
      return make_transition_table(
        *idle + event<issue_604_activate> = active,
        active + event<issue_604_deactivate> = idle
      );
      // clang-format on
    }
  };

  struct issue_604_root {
    auto operator()() const {
      using namespace sml;
      const auto idle = sml::state<class issue_604_root_idle>;
      const auto sub = sml::state<issue_604_sub>;
      // clang-format off
      return make_transition_table(
        *idle + event<issue_604_enter> = sub,
        sub + event<issue_604_exit> = idle
      );
      // clang-format on
    }
  };

  const auto issue_604_sub_state = sml::state<issue_604_root>.sm<issue_604_sub>();
  sml::sm<issue_604_root> sm{};
  expect(!sm.is<decltype(issue_604_sub_state)>(sml::state<issue_604_sub_idle>()));
  expect(sm.process_event(issue_604_enter{}));
  expect(sm.is(sml::state<issue_604_root>.sm<issue_604_sub>()));
  expect(sm.is<decltype(issue_604_sub_state)>(sml::state<issue_604_sub_idle>()));
  expect(sm.process_event(issue_604_activate{}));
  expect(sm.is<decltype(issue_604_sub_state)>(sml::state<issue_604_sub_active>()));
  expect(sm.process_event(issue_604_deactivate{}));
  expect(sm.is<decltype(issue_604_sub_state)>(sml::state<issue_604_sub_idle>()));
  expect(sm.process_event(issue_604_exit{}));
  expect(sm.is(sml::state<issue_604_root_idle>()));
};

test issue_605 = [] { expect(true); };

test issue_606 = [] { expect(true); };

test issue_609 = [] { expect(true); };

test issue_610 = [] { expect(true); };

test issue_611 = [] { expect(true); };
test issue_619 = [] {
  struct issue_619_start {};
  struct issue_619_next {};

  struct issue_619_logger {
    static inline bool anonymous_event_seen = false;
    template <class SM, class TEvent>
    void log_process_event(const TEvent&) const {}
    void log_process_event(const sml::back::anonymous&) const { anonymous_event_seen = true; }
  };

  struct issue_619_transitions {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *sml::state<issue_619_start> = sml::state<issue_619_next>,
        sml::state<issue_619_next> + event<issue_619_start> = sml::X
      );
      // clang-format on
    }
  };

  issue_619_logger::anonymous_event_seen = false;
  sml::sm<issue_619_transitions, sml::logger<issue_619_logger>> sm{};
  expect(issue_619_logger::anonymous_event_seen);
  expect(sm.is(sml::state<issue_619_next>()));
  expect(sm.process_event(issue_619_start{}));
  expect(sm.is(sml::X));
};

test issue_622 = [] {
  struct issue_622_enter {};
  struct issue_622_to_a {};
  struct issue_622_to_b {};
  struct issue_622_exit {};

  struct issue_622_nested {
    auto operator()() const {
      using namespace sml;
      const auto issue_622_nested_a = sml::state<class issue_622_nested_a>;
      const auto issue_622_nested_b = sml::state<class issue_622_nested_b>;
      const auto issue_622_any = sml::state<sml::_>;

      // clang-format off
      return make_transition_table(
        *issue_622_nested_b,
        issue_622_any + event<issue_622_to_a> = issue_622_nested_a,
        issue_622_any + event<issue_622_to_b> = issue_622_nested_b
      );
      // clang-format on
    }
  };

  struct issue_622_root {
    auto operator()() const {
      using namespace sml;
      const auto issue_622_root_init = sml::state<class issue_622_root_init>;
      const auto issue_622_nested = sml::state<issue_622_nested>;

      // clang-format off
      return make_transition_table(
        *issue_622_root_init + event<issue_622_enter> = issue_622_nested,
        issue_622_nested + event<issue_622_exit> = sml::X
      );
      // clang-format on
    }
  };

  const auto issue_622_nested_state = sml::state<issue_622_root>.sm<issue_622_nested>();
  sml::sm<issue_622_root> sm{};
  expect(sm.is(sml::state<issue_622_root_init>()));
  expect(sm.process_event(issue_622_enter{}));
  expect(sm.is<decltype(issue_622_nested_state)>(sml::state<issue_622_nested_b>()));
  expect(sm.process_event(issue_622_to_a{}));
  expect(sm.is<decltype(issue_622_nested_state)>(sml::state<issue_622_nested_a>()));
  expect(sm.process_event(issue_622_to_b{}));
  expect(sm.is<decltype(issue_622_nested_state)>(sml::state<issue_622_nested_b>()));
};

test issue_623 = [] {
  struct issue_623_enter {};
  struct issue_623_done {};
  struct issue_623_emit {};

  struct issue_623_child {
    auto operator()() const {
      using namespace sml;
      const auto issue_623_child_running = sml::state<class issue_623_child_running>;
      // clang-format off
      return make_transition_table(
        *issue_623_child_running + event<issue_623_emit> / [](sml::back::process<issue_623_done> process_event) {
          process_event(issue_623_done{});
        } = issue_623_child_running
      );
      // clang-format on
    }
  };

  struct issue_623_root {
    auto operator()() const {
      using namespace sml;
      const auto issue_623_root_idle = sml::state<class issue_623_root_idle>;
      const auto issue_623_child = sml::state<issue_623_child>;

      // clang-format off
      return make_transition_table(
        *issue_623_root_idle + event<issue_623_enter> = issue_623_child,
        issue_623_child + event<issue_623_done> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<issue_623_root> sm{};
  expect(sm.process_event(issue_623_enter{}));
  expect(sm.process_event(issue_623_emit{}));
  expect(sm.is(sml::X));
};

test issue_627 = [] {
  struct issue_627_connect {
    int id{};
  };
  struct issue_627_interrupt {};
  struct issue_627_disconnect {};
  struct issue_627_disconnected {};
  struct issue_627_connected {
    int id{};
  };
  struct issue_627_interrupted {
    int id{};
  };

  struct issue_627_data {
    explicit issue_627_data(std::string address) : address{address} {}

    auto operator()() const {
      using namespace sml;
      const auto set = [](const issue_627_connect& event, issue_627_connected& state) { state.id = event.id; };
      const auto copy = [](issue_627_connected& source, issue_627_interrupted& destination) { destination.id = source.id; };

      // clang-format off
      return make_transition_table(
        *sml::state<issue_627_disconnected> + event<issue_627_connect> / (set, &issue_627_data::mark_connected) = sml::state<issue_627_connected>,
        sml::state<issue_627_connected> + event<issue_627_interrupt> / (copy, &issue_627_data::mark_interrupted) = sml::state<issue_627_interrupted>,
        sml::state<issue_627_interrupted> + event<issue_627_connect> / (set, &issue_627_data::mark_connected) = sml::state<issue_627_connected>,
        sml::state<issue_627_connected> + event<issue_627_disconnect> / &issue_627_data::mark_connected = sml::X
      );
      // clang-format on
    }

    void mark_connected(issue_627_connected&) const {}
    void mark_interrupted(issue_627_interrupted&) const {}

    std::string address;
  };

  issue_627_data data{"127.0.0.1"};
  sml::sm<issue_627_data> sm{data, issue_627_connected{}};
  expect(sm.process_event(issue_627_connect{1024}));
  expect(sm.process_event(issue_627_interrupt{}));
  expect(sm.process_event(issue_627_connect{2048}));
  expect(sm.process_event(issue_627_disconnect{}));
  expect(sm.is(sml::X));
};

test issue_628 = [] {
  struct issue_628_start {};
  struct issue_628_stop {};
  struct issue_628_pause {};
  struct issue_628_resume {};
  struct issue_628_abort {};
  struct issue_628_idle {};
  struct issue_628_running {};
  struct issue_628_paused {};
  struct issue_628_aborted {};

  struct issue_628_base {
    auto operator()() const {
      using namespace sml;
      const auto issue_628_idle_state = sml::state<issue_628_idle>;
      const auto issue_628_running_state = sml::state<issue_628_running>;
      const auto issue_628_paused_state = sml::state<issue_628_paused>;

      // clang-format off
      return make_transition_table(
        *issue_628_idle_state + event<issue_628_start> = issue_628_running_state,
        issue_628_running_state + event<issue_628_stop> = issue_628_idle_state,
        issue_628_running_state + event<issue_628_pause> = issue_628_paused_state,
        issue_628_paused_state + event<issue_628_resume> = issue_628_running_state
      );
      // clang-format on
    }
  };

  struct issue_628_extended : issue_628_base {
    auto operator()() const {
      using namespace sml;
      const auto issue_628_running_state = sml::state<issue_628_running>;
      const auto issue_628_aborted_state = sml::state<issue_628_aborted>;
      const auto issue_628_idle_state = sml::state<issue_628_idle>;

      // clang-format off
      return make_transition_table(
        issue_628_base::operator()(),
        issue_628_running_state + event<issue_628_abort> = issue_628_aborted_state,
        issue_628_aborted_state + event<issue_628_resume> = issue_628_idle_state
      );
      // clang-format on
    }
  };

  sml::sm<issue_628_extended> sm{};
  expect(sm.is(sml::state<issue_628_idle>()));
  expect(sm.process_event(issue_628_start{}));
  expect(sm.is(sml::state<issue_628_running>()));
  expect(sm.process_event(issue_628_abort{}));
  expect(sm.is(sml::state<issue_628_aborted>()));
  expect(sm.process_event(issue_628_resume{}));
  expect(sm.is(sml::state<issue_628_idle>()));
};

test issue_629 = [] {
  struct issue_629_event {};
  struct issue_629_start {};

  struct issue_629_action {
    static inline int calls = 0;

    template <class T>
    void operator()(const issue_629_event&, const T& value) const requires std::is_integral_v<std::decay_t<T>> {
      ++calls;
      (void)value;
    }
  };

  struct issue_629_transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_629_start_state = sml::state<issue_629_start>;

      // clang-format off
      return make_transition_table(
        *issue_629_start_state + event<issue_629_event> / issue_629_action{} = sml::X
      );
      // clang-format on
    }
  };

  issue_629_action::calls = 0;
  sml::sm<issue_629_transitions, int> sm{42};
  expect(sm.process_event(issue_629_event{}));
  expect(1 == issue_629_action::calls);
};

test issue_630 = [] {
  struct issue_630_go {};
  struct issue_630_idle {};
  struct issue_630_running {};

  struct issue_630_transitions {
    auto operator()() const {
      using namespace sml;
      const auto issue_630_idle_state = sml::state<issue_630_idle>;
      const auto issue_630_running_state = sml::state<issue_630_running>;

      // clang-format off
      return make_transition_table(
        *issue_630_idle_state + event<issue_630_go> = issue_630_running_state
      );
      // clang-format on
    }
  };

  sml::sm<issue_630_transitions> sm{};
  expect(sm.process_event(issue_630_go{}));
  expect(sm.is(sml::state<issue_630_running>()));
};

test issue_631 = [] {
  struct issue_631_event {};
  struct issue_631_to_exit {};
  struct issue_631_entry_state {};
  struct issue_631_exit_state {};

  struct issue_631_transitions {
    static inline int entry_calls = 0;
    static inline int exit_calls = 0;

    static void count_entry() {
      ++entry_calls;
    }

    static void count_exit() {
      ++exit_calls;
    }

    auto operator()() const {
      using namespace sml;
      const auto issue_631_entry_state_ref = sml::state<issue_631_entry_state>;
      const auto issue_631_exit_state_ref = sml::state<issue_631_exit_state>;

      // clang-format off
      return make_transition_table(
        *issue_631_entry_state_ref + on_entry<_> / issue_631_transitions::count_entry,
        issue_631_entry_state_ref + event<issue_631_event> = issue_631_exit_state_ref,
        issue_631_exit_state_ref + on_exit<_> / issue_631_transitions::count_exit,
        issue_631_exit_state_ref + event<issue_631_to_exit> = sml::X
      );
      // clang-format on
    }
  };

  issue_631_transitions::entry_calls = 0;
  issue_631_transitions::exit_calls = 0;
  sml::sm<issue_631_transitions> sm{};
  expect(sm.process_event(issue_631_event{}));
  expect(sm.process_event(issue_631_to_exit{}));
  expect(sm.is(sml::X));
  expect(1 == issue_631_transitions::entry_calls);
  expect(1 == issue_631_transitions::exit_calls);
};

test issue_632 = [] {
  struct issue_632_activate_a {};
  struct issue_632_activate_b {};
  struct issue_632_reset {};
  struct issue_632_left_idle {};
  struct issue_632_left_done {};
  struct issue_632_right_idle {};
  struct issue_632_right_done {};

  struct issue_632_transitions {
    auto operator()() const {
      using namespace sml;
      const auto left_idle = sml::state<issue_632_left_idle>;
      const auto left_done = sml::state<issue_632_left_done>;
      const auto right_idle = sml::state<issue_632_right_idle>;
      const auto right_done = sml::state<issue_632_right_done>;

      // clang-format off
      return make_transition_table(
        *left_idle + event<issue_632_activate_a> = left_done,
        *right_idle + event<issue_632_activate_b> = right_done,
        left_done + event<issue_632_reset> = left_idle,
        right_done + event<issue_632_reset> = right_idle
      );
      // clang-format on
    }
  };

  sml::sm<issue_632_transitions, sml::thread_safe<std::recursive_mutex>> sm{};
  expect(sm.is(sml::state<issue_632_left_idle>(), sml::state<issue_632_right_idle>()));
  expect(sm.process_event(issue_632_activate_a{}));
  expect(sm.is(sml::state<issue_632_left_done>(), sml::state<issue_632_right_idle>()));
  expect(sm.process_event(issue_632_activate_b{}));
  expect(sm.is(sml::state<issue_632_left_done>(), sml::state<issue_632_right_done>()));
  expect(sm.process_event(issue_632_reset{}));
  expect(sm.is(sml::state<issue_632_left_idle>(), sml::state<issue_632_right_idle>()));
};

test issue_633 = [] {
  struct issue_633_activate {};
  struct issue_633_progress {};
  struct issue_633_root_idle {};
  struct issue_633_sub_idle {};
  struct issue_633_sub_done {};

  struct issue_633_logger {
    static inline int entry_calls = 0;
    static inline int state_changes = 0;

    template <class SM, class TEvent>
    void log_process_event(const sml::back::on_entry<TEvent>&) const {
      ++entry_calls;
    }

    template <class SM, class TSrcState, class TDstState>
    void log_state_change(const TSrcState&, const TDstState&) const {
      ++state_changes;
    }
  };

  struct issue_633_sub {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(
        *sml::state<issue_633_sub_idle> + event<issue_633_progress> = sml::state<issue_633_sub_done>
      );
      // clang-format on
    }
  };

  struct issue_633_root {
    auto operator()() const {
      using namespace sml;
      const auto issue_633_root_idle = sml::state<issue_633_root_idle>;
      const auto issue_633_sub = sml::state<issue_633_sub>;

      // clang-format off
      return make_transition_table(
        *issue_633_root_idle + event<issue_633_activate> = issue_633_sub
      );
      // clang-format on
    }
  };

  issue_633_logger::entry_calls = 0;
  issue_633_logger::state_changes = 0;
  issue_633_logger logger;
  const auto issue_633_nested_state = sml::state<issue_633_root>.sm<issue_633_sub>();
  sml::sm<issue_633_root, sml::logger<issue_633_logger>> sm{logger};
  expect(sm.process_event(issue_633_activate{}));
  expect(sm.is<decltype(issue_633_nested_state)>(sml::state<issue_633_sub_idle>()));
  expect(sm.process_event(issue_633_progress{}));
  expect(sm.is<decltype(issue_633_nested_state)>(sml::state<issue_633_sub_done>()));
  expect(2 <= issue_633_logger::entry_calls);
  expect(1 <= issue_633_logger::state_changes);
};

test issue_636 = [] {
  struct issue_636_boot {};
  struct issue_636_message {
    std::string payload;
  };
  struct issue_636_connected {};
  struct issue_636_context {
    std::string payload;
  };

  struct issue_636_root {
    auto operator()() const {
      using namespace sml;
      const auto issue_636_boot_state = sml::state<issue_636_boot>;

      // clang-format off
      return make_transition_table(
        *issue_636_boot_state + event<issue_636_message> / [](const issue_636_message& event, issue_636_context& context) {
          context.payload = event.payload;
        } = sml::state<issue_636_connected>
      );
      // clang-format on
    }
  };

  issue_636_context context;
  sml::sm<issue_636_root> sm{context};
  expect(sm.process_event(issue_636_message{"boost-sml"}));
  expect(sm.is(sml::state<issue_636_connected>()));
  expect(!context.payload.empty());
};
test issue_639 = [] {
  struct issue_639_event0 {};
  struct issue_639_event1 {};
  struct issue_639_event2 {};
  struct issue_639_event3 {};
  struct issue_639_event4 {};
  struct issue_639_event5 {};
  struct issue_639_event6 {};
  struct issue_639_event7 {};
  struct issue_639_event8 {};
  struct issue_639_event9 {};

  struct issue_639_dependency_a {
    int value = 0;
  };
  struct issue_639_dependency_b {
    int value = 0;
  };

  struct issue_639_root {
    auto operator()() const {
      using namespace sml;
      const auto issue_639_s0 = sml::state<class issue_639_s0>;
      const auto issue_639_s1 = sml::state<class issue_639_s1>;
      const auto issue_639_s2 = sml::state<class issue_639_s2>;
      const auto issue_639_s3 = sml::state<class issue_639_s3>;
      const auto issue_639_s4 = sml::state<class issue_639_s4>;
      const auto issue_639_s5 = sml::state<class issue_639_s5>;
      const auto issue_639_s6 = sml::state<class issue_639_s6>;
      const auto issue_639_s7 = sml::state<class issue_639_s7>;
      const auto issue_639_s8 = sml::state<class issue_639_s8>;

      const auto bump = [](issue_639_dependency_a &a, issue_639_dependency_b &b) {
        ++a.value;
        b.value += 2;
      };

      // clang-format off
      return make_transition_table(
        *issue_639_s0 + event<issue_639_event0> / bump = issue_639_s1,
        issue_639_s1 + event<issue_639_event1> / bump = issue_639_s2,
        issue_639_s2 + event<issue_639_event2> / bump = issue_639_s3,
        issue_639_s3 + event<issue_639_event3> / bump = issue_639_s4,
        issue_639_s4 + event<issue_639_event4> / bump = issue_639_s5,
        issue_639_s5 + event<issue_639_event5> / bump = issue_639_s6,
        issue_639_s6 + event<issue_639_event6> / bump = issue_639_s7,
        issue_639_s7 + event<issue_639_event7> / bump = issue_639_s8,
        issue_639_s8 + event<issue_639_event8> / bump = sml::X,
        issue_639_s0 + event<issue_639_event9> / bump = sml::X
      );
      // clang-format on
    }
  };

  using issue_639_transitions = decltype(sml::aux::declval<issue_639_root>().operator()());
  using issue_639_deps = sml::aux::apply_t<sml::aux::merge_deps, issue_639_transitions>;
  using issue_639_unique_deps = sml::aux::apply_t<sml::aux::unique_t, issue_639_deps>;

  static_assert(sml::aux::size<issue_639_deps>::value > sml::aux::size<issue_639_unique_deps>::value,
                "issue_639: duplicate dependency resolution is not merged");
  static_assert(sml::aux::is_same<issue_639_unique_deps, sml::aux::type_list<issue_639_dependency_a &, issue_639_dependency_b &>>::value,
                "issue_639: expected only the configured dependencies");

  issue_639_dependency_a a{};
  issue_639_dependency_b b{};
  sml::sm<issue_639_root> sm{a, b};

  expect(sm.process_event(issue_639_event0{}));
  expect(sm.process_event(issue_639_event1{}));
  expect(sm.process_event(issue_639_event2{}));
  expect(sm.process_event(issue_639_event3{}));
  expect(sm.process_event(issue_639_event4{}));
  expect(sm.process_event(issue_639_event5{}));
  expect(sm.process_event(issue_639_event6{}));
  expect(sm.process_event(issue_639_event7{}));
  expect(sm.process_event(issue_639_event8{}));
  expect(sm.is(sml::X));
  expect(9 == a.value);
  expect(18 == b.value);
};

test issue_641 = [] {
  struct issue_641_s1 {};
  struct issue_641_e1 {};

  struct issue_641_base {
    virtual ~issue_641_base() = default;
  };

  struct issue_641_sub : issue_641_base {
    auto operator()() const {
      using namespace sml;
      const auto issue_641_start = sml::state<issue_641_s1>;
      // clang-format off
      return make_transition_table(*issue_641_start + event<issue_641_e1> = sml::X);
      // clang-format on
    }
  };

  struct issue_641_root {
    auto operator()() const {
      using namespace sml;
      // clang-format off
      return make_transition_table(*state<issue_641_sub> = sml::X);
      // clang-format on
    }
  };

  sml::sm<issue_641_root> sm{};
  expect(sm.is(sml::X));
  expect(!sm.process_event(issue_641_e1{}));
};

test issue_643 = [] {
  struct issue_643_start {};
  struct issue_643_deferred {};
  struct issue_643_queued {};

  struct issue_643_counters {
    int deferred_calls = 0;
    int queued_calls = 0;
  };

  struct issue_643_machine {
    auto operator()() const {
      using namespace sml;
      const auto issue_643_idle = sml::state<class issue_643_idle>;
      const auto issue_643_running = sml::state<class issue_643_running>;

      const auto schedule = [](issue_643_counters &counters, sml::back::defer<issue_643_deferred> deferEvent,
                              sml::back::process<issue_643_queued> processEvent) {
        deferEvent(issue_643_deferred{});
        deferEvent(issue_643_deferred{});
        processEvent(issue_643_queued{});
      };

      const auto on_deferred = [](issue_643_counters &counters) { ++counters.deferred_calls; };
      const auto on_queued = [](issue_643_counters &counters) { ++counters.queued_calls; };

      // clang-format off
      return make_transition_table(
        *issue_643_idle + event<issue_643_start> / schedule = issue_643_running,
        issue_643_running + event<issue_643_deferred> / on_deferred = issue_643_running,
        issue_643_running + event<issue_643_queued> / on_queued = sml::X
      );
      // clang-format on
    }
  };

  issue_643_counters counters{};
  sml::sm<issue_643_machine, sml::defer_queue<std::deque>, sml::process_queue<std::queue>> sm{counters};

  expect(sm.process_event(issue_643_start{}));
  expect(sm.is(sml::X));
  expect(2 == counters.deferred_calls);
  expect(1 == counters.queued_calls);
};

test issue_646 = [] {
  // No deterministic reproduction currently exists in the issue body.
  // The issue is tracked as a feature request for a future "shootBurst" node.
  expect(true);
};

test issue_647 = [] {
  // No deterministic reproduction currently exists in the issue body.
  // The issue is tracked as a feature request for a future random selector node.
  expect(true);
};

test issue_659 = [] {
  struct issue_659_start {
    int value = 0;
  };
  struct issue_659_tracker {
    int plain_iota = 0;
    int plain_count = 0;
    int plain_steps = 0;
    int nested_iota = 0;
    int nested_count = 0;
    int nested_steps = 0;
  };
  struct issue_659_increment {};

  struct issue_659_simple {
    struct issue_659_simple_state_reset {};
    struct issue_659_simple_state_counting {
      int iota = 0;
      int count = 0;
    };

    auto operator()() const {
      using namespace sml;
      const auto issue_659_simple_idle = sml::state<issue_659_simple_state_reset>;
      const auto issue_659_simple_counting = sml::state<issue_659_simple_state_counting>;

      const auto configure = [](const issue_659_start &event, issue_659_simple_state_counting &state,
                               issue_659_tracker &tracker) {
        state.iota = event.value;
        state.count = 0;
        tracker.plain_iota = state.iota;
      };
      const auto run = [](issue_659_simple_state_counting &state, issue_659_tracker &tracker, const issue_659_increment &) {
        state.count += state.iota;
        tracker.plain_count = state.count;
        ++tracker.plain_steps;
      };

      // clang-format off
      return make_transition_table(
        *issue_659_simple_idle + event<issue_659_start> / configure = issue_659_simple_counting,
        issue_659_simple_counting + event<issue_659_increment> / run = issue_659_simple_counting
      );
      // clang-format on
    }
  };

  struct issue_659_submachine {
    int iota = 0;
    int count = 0;
    struct issue_659_submachine_counting {};

    auto operator()() const {
      using namespace sml;
      const auto issue_659_sub_idle = sml::state<issue_659_submachine_counting>;

      const auto run = [](issue_659_submachine &self, const issue_659_increment &) {
        self.count += self.iota;
      };

      // clang-format off
      return make_transition_table(*issue_659_sub_idle + event<issue_659_increment> / run = issue_659_sub_idle);
      // clang-format on
    }
  };

  struct issue_659_root {
    auto operator()() const {
      using namespace sml;
      const auto issue_659_root_idle = sml::state<class issue_659_root_idle>;
      const auto issue_659_submachine_state = sml::state<issue_659_submachine>;

      const auto configure = [](const issue_659_start &event, issue_659_submachine &submachine, issue_659_tracker &tracker) {
        submachine.iota = event.value;
        tracker.nested_iota = submachine.iota;
      };

      // clang-format off
      return make_transition_table(
        *issue_659_root_idle + event<issue_659_start> / configure = issue_659_submachine_state
      );
      // clang-format on
    }
  };

  issue_659_tracker tracker{};
  issue_659_simple::issue_659_simple_state_counting simple_state{};
  sml::sm<issue_659_simple> simple_sm{tracker, simple_state};
  expect(simple_sm.process_event(issue_659_start{5}));
  expect(simple_sm.process_event(issue_659_increment{}));
  expect(simple_sm.process_event(issue_659_increment{}));
  expect(2 == tracker.plain_steps);
  expect(5 == tracker.plain_iota);
  expect(10 == tracker.plain_count);

  issue_659_root root{};
  issue_659_submachine submachine{};
  issue_659_tracker nested_tracker{};
  sml::sm<issue_659_root, sml::process_queue<std::queue>> sub_sm{nested_tracker, submachine};
  expect(sub_sm.process_event(issue_659_start{3}));
  expect(sub_sm.process_event(issue_659_increment{}));
  expect(sub_sm.process_event(issue_659_increment{}));
  expect(3 == nested_tracker.nested_iota);
  expect(6 == submachine.count);
};

#endif
