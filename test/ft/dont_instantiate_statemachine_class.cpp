#include <boost/sml.hpp>

namespace sml = boost::sml;

[[maybe_unused]] const auto idle = sml::state<class idle>;
[[maybe_unused]] const auto idle2 = sml::state<class idle2>;
[[maybe_unused]] const auto s1 = sml::state<class s1>;
[[maybe_unused]] const auto s2 = sml::state<class s2>;

test non_empty_statemachine_class_with_deleted_copy_constructor = []() {
  struct non_empty_statemachine_class {
    non_empty_statemachine_class() = default;
    non_empty_statemachine_class(const non_empty_statemachine_class&) = delete;

    auto operator()() {
      using namespace sml;
      return make_transition_table(*"start"_s + on_entry<_> / []() {});
    }

    int some_variable_to_make_class_not_empty = 0;
  };

  non_empty_statemachine_class instance;

  boost::sml::sm<non_empty_statemachine_class, sml::dont_instantiate_statemachine_class>{instance};
};

test non_empty_statemachine_class_with_sub_statemachine = []() {
  struct sub {
    sub() = default;
    sub(const sub&) = delete;

    auto operator()() noexcept {
      using namespace sml;

      // clang-format off
      return make_transition_table(
         *"idle"_s + on_entry<_> / [] { }
      );
      // clang-format on
    }

    int a_in_sub = 0;
  };

  struct StateMachine {
    StateMachine() = default;
    StateMachine(const StateMachine&) = delete;

    auto operator()() {
      using namespace sml;
      return make_transition_table(*"start"_s = state<sub>);
    }

    int privateMemberVariable = 0;
  };

  StateMachine stateMachineInstance;
  sub subInstance;

  sml::sm<StateMachine, sml::dont_instantiate_statemachine_class> sm{stateMachineInstance, subInstance};
};
