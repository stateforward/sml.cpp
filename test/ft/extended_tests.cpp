#include <algorithm>
#include <boost/sml.hpp>
#include <string>
#include <vector>

namespace sml = boost::sml;

inline std::string canonical_state_name(std::string state) {
  if (0 == state.find("class ")) {
    state.erase(0, 6);
  } else if (0 == state.find("struct ")) {
    state.erase(0, 7);
  }
  const auto scope_pos = state.rfind("::");
  if (scope_pos != std::string::npos) {
    state.erase(0, scope_pos + 2);
  }
  return state;
}

template <class TSM>
std::vector<std::string> sorted_current_states(const TSM& sm) {
  std::vector<std::string> states;
  sm.visit_current_states([&](auto state) { states.push_back(canonical_state_name(state.c_str())); });
  std::sort(states.begin(), states.end());
  return states;
}

template <class TSM>
bool all_regions_terminated(const TSM& sm) {
  const auto states = sorted_current_states(sm);
  return std::all_of(states.cbegin(), states.cend(), [](const auto& state) { return state == "terminate"; });
}

struct e_single_start {};
struct e_single_finish {};
struct e_single_idle {};

struct e_ortho_left_start {};
struct e_ortho_left_finish {};
struct e_ortho_right_start {};
struct e_ortho_right_finish {};
struct e_ortho_unused {};
struct e_ortho_right_done {};

const auto qa_idle = sml::state<class qa_idle>;
const auto qa_active = sml::state<class qa_active>;
const auto qb_idle = sml::state<class qb_idle>;
const auto qb_active = sml::state<class qb_active>;

const auto qb_region_left = sml::state<class qb_region_left>;
const auto qb_region_right = sml::state<class qb_region_right>;
const auto qb_region_left_next = sml::state<class qb_region_left_next>;
const auto qb_region_right_next = sml::state<class qb_region_right_next>;
const auto qb_region_right_done = sml::state<class qb_region_right_done>;

struct e_return_left {};
struct e_return_right {};
struct e_return_unused {};

test is_single_region_termination_and_event_visibility = [] {
  struct machine {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
          *qa_idle + event<e_single_start> = qa_active
        , qa_active + event<e_single_finish> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<machine> sm{};
  expect((std::vector<std::string>{"qa_idle"} == sorted_current_states(sm)));
  expect(sm.process_event(e_single_start{}));
  expect((std::vector<std::string>{"qa_active"} == sorted_current_states(sm)));
  expect(!sm.process_event(e_single_idle{}));
  expect(sm.process_event(e_single_finish{}));
  expect((std::vector<std::string>{"terminate"} == sorted_current_states(sm)));
  expect(sm.is(sml::X));
  expect(!sm.process_event(e_single_idle{}));
};

test orthogonal_regions_partial_termination = [] {
  struct machine {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *qb_region_left  + event<e_ortho_left_start>  = qb_region_left_next
        , qb_region_left_next + event<e_ortho_left_finish> = sml::X
        , *qb_region_right + event<e_ortho_right_start>  = qb_region_right_next
        , qb_region_right_next + event<e_ortho_right_finish> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<machine> sm{};
  expect((std::vector<std::string>{"qb_region_left", "qb_region_right"} == sorted_current_states(sm)));
  expect(!all_regions_terminated(sm));

  expect(sm.process_event(e_ortho_left_start{}));
  expect((std::vector<std::string>{"qb_region_left_next", "qb_region_right"} == sorted_current_states(sm)));
  expect(!all_regions_terminated(sm));

  expect(sm.process_event(e_ortho_left_finish{}));
  expect((std::vector<std::string>{"qb_region_right", "terminate"} == sorted_current_states(sm)));
  expect(!all_regions_terminated(sm));
  expect(!sm.process_event(e_ortho_unused{}));
  expect(!all_regions_terminated(sm));

  expect(sm.process_event(e_ortho_right_start{}));
  expect((std::vector<std::string>{"qb_region_right_next", "terminate"} == sorted_current_states(sm)));
  expect(sm.process_event(e_ortho_right_finish{}));
  expect((std::vector<std::string>{"terminate", "terminate"} == sorted_current_states(sm)));
  expect(all_regions_terminated(sm));
};

test visit_current_states_reports_all_regions = [] {
  struct machine {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *qb_region_left + event<e_ortho_left_start> = qb_region_left_next
        , qb_region_left_next + event<e_ortho_left_finish> = sml::X
        , *qb_region_right + event<e_ortho_right_start> = qb_region_right_next
        , qb_region_right_next + event<e_ortho_right_finish> = sml::X
      );
      // clang-format on
    }
  };

  sml::sm<machine> sm{};

  expect((std::vector<std::string>{"qb_region_left", "qb_region_right"} == sorted_current_states(sm)));
  sm.process_event(e_ortho_left_start{});
  sm.process_event(e_ortho_right_start{});
  expect((std::vector<std::string>{"qb_region_left_next", "qb_region_right_next"} == sorted_current_states(sm)));

  sm.process_event(e_ortho_left_finish{});
  sm.process_event(e_ortho_right_finish{});
  expect((std::vector<std::string>{"terminate", "terminate"} == sorted_current_states(sm)));
};

test process_event_return_value_in_orthogonal_regions = [] {
  struct machine {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
          *qa_idle + event<e_return_left> = qa_active
        , qa_active + event<e_return_left> = qa_active
        , *qb_idle + event<e_return_right> = qb_active
      );
      // clang-format on
    }
  };

  sml::sm<machine> sm{};
  expect(sm.process_event(e_return_left{}));
  expect((std::vector<std::string>{"qa_active", "qb_idle"} == sorted_current_states(sm)));
  expect(!sm.process_event(e_return_unused{}));
  expect((std::vector<std::string>{"qa_active", "qb_idle"} == sorted_current_states(sm)));
  expect(sm.process_event(e_return_right{}));
  expect((std::vector<std::string>{"qa_active", "qb_active"} == sorted_current_states(sm)));
};

test orthogonal_regions_still_process_after_partial_termination = [] {
  struct machine {
    auto operator()() {
      using namespace sml;
      // clang-format off
      return make_transition_table(
         *qb_region_left + event<e_ortho_left_start> = qb_region_left_next
        , qb_region_left_next + event<e_ortho_left_finish> = sml::X
        , *qb_region_right + event<e_ortho_right_start> = qb_region_right_next
        , qb_region_right_next + event<e_ortho_right_done> = qb_region_right_done
      );
      // clang-format on
    }
  };

  sml::sm<machine> sm{};
  expect((std::vector<std::string>{"qb_region_left", "qb_region_right"} == sorted_current_states(sm)));
  expect(sm.process_event(e_ortho_left_start{}));
  expect((std::vector<std::string>{"qb_region_left_next", "qb_region_right"} == sorted_current_states(sm)));
  expect(sm.process_event(e_ortho_left_finish{}));
  expect((std::vector<std::string>{"qb_region_right", "terminate"} == sorted_current_states(sm)));

  expect(sm.process_event(e_ortho_right_start{}));
  expect((std::vector<std::string>{"qb_region_right_next", "terminate"} == sorted_current_states(sm)));
  expect(sm.process_event(e_ortho_right_done{}));
  expect((std::vector<std::string>{"qb_region_right_done", "terminate"} == sorted_current_states(sm)));
  expect(!sm.process_event(e_ortho_unused{}));
};
