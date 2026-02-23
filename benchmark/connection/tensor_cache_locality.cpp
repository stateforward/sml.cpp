// Simulates the "10k tensor actors" cache-locality question:
// - SML path: one SML machine per tensor, dispatch fill/unlink events.
// - Flat path: one tightly packed refs/live array with direct function calls.

#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include <boost/sml.hpp>
#include <boost/sml/utility/sm_pool.hpp>

namespace sml = boost::sml;

namespace {

constexpr std::size_t kTensorCount = 10'000;
constexpr std::size_t kNodeCount = 10'000;
constexpr std::size_t kFanIn = 4;
constexpr std::uint16_t kInitialRefs = 4;

struct ev_fill {};
struct ev_unlink {};
struct ev_fill_idx {
  std::uint16_t id{};
};
struct ev_unlink_idx {
  std::uint16_t id{};
};
struct ev_tick {};
using pooled_fill = sml::utility::indexed_event<ev_fill>;
using pooled_unlink = sml::utility::indexed_event<ev_unlink>;
using pooled_tick = sml::utility::indexed_event<ev_tick>;

struct tensor_slot {
  std::uint16_t refs{};
  std::uint16_t live{};
};

struct node_op {
  std::uint16_t dst{};
  std::array<std::uint16_t, kFanIn> src{};
};

enum class access_pattern : std::uint8_t { local, random };

std::vector<node_op> make_ops(const access_pattern pattern) {
  std::vector<node_op> ops;
  ops.reserve(kNodeCount);

  if (pattern == access_pattern::local) {
    for (std::size_t i = 0; i < kNodeCount; ++i) {
      node_op op{};
      op.dst = static_cast<std::uint16_t>(i % kTensorCount);
      for (std::size_t j = 0; j < kFanIn; ++j) {
        op.src[j] = static_cast<std::uint16_t>((i + j + 1) % kTensorCount);
      }
      ops.push_back(op);
    }
    return ops;
  }

  std::minstd_rand rng{1337};
  std::uniform_int_distribution<unsigned> dist{0u, static_cast<unsigned>(kTensorCount - 1)};
  for (std::size_t i = 0; i < kNodeCount; ++i) {
    node_op op{};
    op.dst = static_cast<std::uint16_t>(dist(rng));
    for (auto& src : op.src) {
      src = static_cast<std::uint16_t>(dist(rng));
    }
    ops.push_back(op);
  }
  return ops;
}

const std::vector<node_op>& local_ops() {
  static const auto ops = make_ops(access_pattern::local);
  return ops;
}

const std::vector<node_op>& random_ops() {
  static const auto ops = make_ops(access_pattern::random);
  return ops;
}

const std::vector<std::uint16_t>& random_tensor_ids() {
  static const auto ids = [] {
    std::vector<std::uint16_t> out;
    out.reserve(kNodeCount * (1 + kFanIn));
    std::minstd_rand rng{424242};
    std::uniform_int_distribution<unsigned> dist{0u, static_cast<unsigned>(kTensorCount - 1)};
    for (std::size_t i = 0; i < kNodeCount * (1 + kFanIn); ++i) {
      out.push_back(static_cast<std::uint16_t>(dist(rng)));
    }
    return out;
  }();
  return ids;
}

struct flat_tensor_pool {
  explicit flat_tensor_pool() : slots(kTensorCount) {}

  void reset() {
    for (auto& slot : slots) {
      slot.refs = kInitialRefs;
      slot.live = 0;
    }
  }

  std::uint16_t fill(const std::uint16_t id) {
    auto& slot = slots[id];
    ++slot.live;
    return slot.live;
  }

  std::uint16_t fill_ev(const ev_fill_idx& ev) { return fill(ev.id); }

  std::uint16_t unlink(const std::uint16_t id) {
    auto& slot = slots[id];
    if (slot.refs > 0) {
      --slot.refs;
    }
    return slot.refs;
  }

  std::uint16_t unlink_ev(const ev_unlink_idx& ev) { return unlink(ev.id); }

  std::vector<tensor_slot> slots;
};

struct tensor_actor {
  auto operator()() const {
    using namespace sml;
    const auto hot = "hot"_s;
    const auto can_unlink = [](const tensor_slot& slot) { return slot.refs > 0; };
    const auto do_fill = [](tensor_slot& slot) { ++slot.live; };
    const auto do_unlink = [](tensor_slot& slot) { --slot.refs; };
    // clang-format off
    return make_transition_table(
      *hot + event<ev_fill> / do_fill,
      hot + event<ev_unlink> [can_unlink] / do_unlink
    );
    // clang-format on
  }
};

struct nodata_actor {
  auto operator()() const {
    using namespace sml;
    const auto a = "a"_s;
    const auto b = "b"_s;
    // clang-format off
    return make_transition_table(
      *a + event<ev_tick> = b,
      b + event<ev_tick> = a
    );
    // clang-format on
  }
};

struct nodata_direct {
  bool state{};
  bool process_event(const ev_tick&) noexcept {
    state = !state;
    return state;
  }
};

struct nodata_pool_storage {
  explicit nodata_pool_storage(const std::size_t count) : flags(count) {}

  void reset() {
    for (auto& flag : flags) {
      flag = 0;
    }
  }

  std::vector<std::uint8_t> flags;
};

struct nodata_router_actor {
  auto operator()() const {
    using namespace sml;
    const auto hot = "hot"_s;
    const auto toggle = [](nodata_pool_storage& storage, const pooled_tick& ev) {
      storage.flags[ev.id] ^= static_cast<std::uint8_t>(1);
    };
    // clang-format off
    return make_transition_table(
      *hot + event<pooled_tick> / toggle
    );
    // clang-format on
  }
};

struct nodata_sm_pool {
  nodata_sm_pool() : pool(kTensorCount) {}

  bool process_event(const std::uint16_t id) {
    pool.template process_indexed<ev_tick>(id);
    return pool.storage().flags[id] != 0;
  }

  template <class TRange>
  std::size_t process_event_batch(const TRange& ids) {
    return pool.template process_indexed_batch<ev_tick>(ids);
  }

  std::uint8_t sample(const std::uint16_t id) const { return pool.storage().flags[id]; }

  sml::utility::sm_pool<nodata_pool_storage, nodata_router_actor> pool;
};

struct sml_tensor_pool {
  sml_tensor_pool() : slots(kTensorCount) {
    actors.reserve(kTensorCount);
    for (auto& slot : slots) {
      actors.emplace_back(slot);
    }
  }

  void reset() {
    for (auto& slot : slots) {
      slot.refs = kInitialRefs;
      slot.live = 0;
    }
  }

  std::uint16_t fill(const std::uint16_t id) {
    actors[id].process_event(ev_fill{});
    return slots[id].live;
  }

  std::uint16_t unlink(const std::uint16_t id) {
    actors[id].process_event(ev_unlink{});
    return slots[id].refs;
  }

  std::vector<tensor_slot> slots;
  std::vector<sml::sm<tensor_actor>> actors;
};

struct sml_tensor_pool_fused {
  struct entry {
    entry() : sm(slot) {}
    tensor_slot slot{};
    sml::sm<tensor_actor> sm;
  };

  sml_tensor_pool_fused() {
    entries.reserve(kTensorCount);
    for (std::size_t i = 0; i < kTensorCount; ++i) {
      entries.emplace_back();
    }
  }

  void reset() {
    for (auto& e : entries) {
      e.slot.refs = kInitialRefs;
      e.slot.live = 0;
    }
  }

  std::uint16_t fill(const std::uint16_t id) {
    auto& e = entries[id];
    e.sm.process_event(ev_fill{});
    return e.slot.live;
  }

  std::uint16_t unlink(const std::uint16_t id) {
    auto& e = entries[id];
    e.sm.process_event(ev_unlink{});
    return e.slot.refs;
  }

  std::vector<entry> entries;
};

struct tensor_router_actor {
  auto operator()() const {
    using namespace sml;
    const auto hot = "hot"_s;
    const auto do_fill = [](flat_tensor_pool& pool, const pooled_fill& ev) { ++pool.slots[ev.id].live; };
    const auto can_unlink = [](const flat_tensor_pool& pool, const pooled_unlink& ev) { return pool.slots[ev.id].refs > 0; };
    const auto do_unlink = [](flat_tensor_pool& pool, const pooled_unlink& ev) { --pool.slots[ev.id].refs; };
    // clang-format off
    return make_transition_table(
      *hot + event<pooled_fill> / do_fill,
      hot + event<pooled_unlink> [can_unlink] / do_unlink
    );
    // clang-format on
  }
};

struct sml_router_pool {
  sml_router_pool() : pool(kTensorCount) {}

  void reset() { pool.reset(); }

  std::uint16_t fill(const std::uint16_t id) {
    pool.template process_indexed<ev_fill>(id);
    return pool.storage().slots[id].live;
  }

  std::uint16_t unlink(const std::uint16_t id) {
    pool.template process_indexed<ev_unlink>(id);
    return pool.storage().slots[id].refs;
  }

  sml::utility::sm_pool<flat_tensor_pool, tensor_router_actor> pool;
};

struct sml_router_pool_fold {
  sml_router_pool_fold() : pool(kTensorCount) {}

  void reset() { pool.reset(); }

  std::uint16_t fill(const std::uint16_t id) {
    pool.template process_indexed<ev_fill>(id);
    return pool.storage().slots[id].live;
  }

  std::uint16_t unlink(const std::uint16_t id) {
    pool.template process_indexed<ev_unlink>(id);
    return pool.storage().slots[id].refs;
  }

  sml::utility::sm_pool<flat_tensor_pool, tensor_router_actor, sml::dispatch<sml::back::policies::fold_expr>> pool;
};

template <class TPool>
std::uint64_t run_once(TPool& pool, const std::vector<node_op>& ops) {
  std::uint64_t sink = 0;
  for (const auto& op : ops) {
    sink += pool.fill(op.dst);
    for (const auto src : op.src) {
      sink += pool.unlink(src);
    }
  }
  return sink;
}

template <class TPool>
std::uint64_t run_once_event_api(TPool& pool, const std::vector<node_op>& ops) {
  std::uint64_t sink = 0;
  for (const auto& op : ops) {
    const ev_fill_idx fill_event{op.dst};
    sink += pool.fill_ev(fill_event);
    for (const auto src : op.src) {
      const ev_unlink_idx unlink_event{src};
      sink += pool.unlink_ev(unlink_event);
    }
  }
  return sink;
}

}  // namespace

#if defined(TEST_ASM)
int main() {
  flat_tensor_pool flat{};
  sml_tensor_pool actors{};

  flat.reset();
  actors.reset();

  volatile std::uint64_t sink = 0;
  sink += run_once(flat, local_ops());
  sink += run_once(actors, local_ops());
  return sink == 0;
}
#elif defined(TEST_PERF)
int main() {
  flat_tensor_pool flat{};
  sml_tensor_pool actors{};

  volatile std::uint64_t sink = 0;
  for (auto i = 0; i < 2'000; ++i) {
    flat.reset();
    sink += run_once(flat, random_ops());
  }
  for (auto i = 0; i < 2'000; ++i) {
    actors.reset();
    sink += run_once(actors, random_ops());
  }
  return sink == 0;
}
#elif defined(TEST_GBENCH)
#include <benchmark/benchmark.h>

template <class TPool>
static void run_bench(benchmark::State& state, TPool& pool, const std::vector<node_op>& ops) {
  for (auto _ : state) {
    state.PauseTiming();
    pool.reset();
    state.ResumeTiming();

    auto sink = run_once(pool, ops);
    benchmark::DoNotOptimize(sink);
    benchmark::ClobberMemory();
  }
}

static void BM_tensor_flat_local(benchmark::State& state) {
  flat_tensor_pool pool{};
  run_bench(state, pool, local_ops());
}

static void BM_tensor_sml_local(benchmark::State& state) {
  sml_tensor_pool pool{};
  run_bench(state, pool, local_ops());
}

static void BM_tensor_flat_random(benchmark::State& state) {
  flat_tensor_pool pool{};
  run_bench(state, pool, random_ops());
}

static void BM_tensor_flat_event_local(benchmark::State& state) {
  flat_tensor_pool pool{};
  for (auto _ : state) {
    state.PauseTiming();
    pool.reset();
    state.ResumeTiming();

    auto sink = run_once_event_api(pool, local_ops());
    benchmark::DoNotOptimize(sink);
    benchmark::ClobberMemory();
  }
}

static void BM_tensor_flat_event_random(benchmark::State& state) {
  flat_tensor_pool pool{};
  for (auto _ : state) {
    state.PauseTiming();
    pool.reset();
    state.ResumeTiming();

    auto sink = run_once_event_api(pool, random_ops());
    benchmark::DoNotOptimize(sink);
    benchmark::ClobberMemory();
  }
}

static void BM_tensor_sml_random(benchmark::State& state) {
  sml_tensor_pool pool{};
  run_bench(state, pool, random_ops());
}

static void BM_tensor_sml_fused_local(benchmark::State& state) {
  sml_tensor_pool_fused pool{};
  run_bench(state, pool, local_ops());
}

static void BM_tensor_sml_fused_random(benchmark::State& state) {
  sml_tensor_pool_fused pool{};
  run_bench(state, pool, random_ops());
}

static void BM_tensor_sml_router_local(benchmark::State& state) {
  sml_router_pool pool{};
  run_bench(state, pool, local_ops());
}

static void BM_tensor_sml_router_random(benchmark::State& state) {
  sml_router_pool pool{};
  run_bench(state, pool, random_ops());
}

static void BM_tensor_sml_router_fold_local(benchmark::State& state) {
  sml_router_pool_fold pool{};
  run_bench(state, pool, local_ops());
}

static void BM_tensor_sml_router_fold_random(benchmark::State& state) {
  sml_router_pool_fold pool{};
  run_bench(state, pool, random_ops());
}

static void BM_dispatch_direct_single(benchmark::State& state) {
  nodata_direct actor{};
  std::uint64_t sink = 0;
  for (auto _ : state) {
    sink += static_cast<std::uint64_t>(actor.process_event(ev_tick{}));
    benchmark::DoNotOptimize(sink);
  }
}

static void BM_dispatch_sml_single(benchmark::State& state) {
  sml::sm<nodata_actor> actor{};
  std::uint64_t sink = 0;
  for (auto _ : state) {
    sink += static_cast<std::uint64_t>(actor.process_event(ev_tick{}));
    benchmark::DoNotOptimize(sink);
  }
}

static void BM_dispatch_direct_actor_array_random(benchmark::State& state) {
  std::vector<nodata_direct> actors(kTensorCount);
  const auto& ids = random_tensor_ids();
  std::uint64_t sink = 0;
  for (auto _ : state) {
    for (const auto id : ids) {
      sink += static_cast<std::uint64_t>(actors[id].process_event(ev_tick{}));
    }
    benchmark::DoNotOptimize(sink);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * ids.size()));
}

static void BM_dispatch_sml_actor_array_random(benchmark::State& state) {
  std::vector<sml::sm<nodata_actor>> actors;
  actors.reserve(kTensorCount);
  for (std::size_t i = 0; i < kTensorCount; ++i) {
    actors.emplace_back();
  }

  const auto& ids = random_tensor_ids();
  std::uint64_t sink = 0;
  for (auto _ : state) {
    for (const auto id : ids) {
      sink += static_cast<std::uint64_t>(actors[id].process_event(ev_tick{}));
    }
    benchmark::DoNotOptimize(sink);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * ids.size()));
}

static void BM_dispatch_sml_actor_array_random_fold(benchmark::State& state) {
  std::vector<sml::sm<nodata_actor, sml::dispatch<sml::back::policies::fold_expr>>> actors;
  actors.reserve(kTensorCount);
  for (std::size_t i = 0; i < kTensorCount; ++i) {
    actors.emplace_back();
  }

  const auto& ids = random_tensor_ids();
  std::uint64_t sink = 0;
  for (auto _ : state) {
    for (const auto id : ids) {
      sink += static_cast<std::uint64_t>(actors[id].process_event(ev_tick{}));
    }
    benchmark::DoNotOptimize(sink);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * ids.size()));
}

static void BM_dispatch_sml_pool_random(benchmark::State& state) {
  nodata_sm_pool actors{};
  const auto& ids = random_tensor_ids();
  std::uint64_t sink = 0;
  for (auto _ : state) {
    for (const auto id : ids) {
      sink += static_cast<std::uint64_t>(actors.process_event(id));
    }
    benchmark::DoNotOptimize(sink);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * ids.size()));
}

static void BM_dispatch_sml_pool_batch_random(benchmark::State& state) {
  nodata_sm_pool actors{};
  const auto& ids = random_tensor_ids();
  std::uint64_t sink = 0;
  for (auto _ : state) {
    sink += actors.process_event_batch(ids);
    sink += actors.sample(ids[0]);
    sink += actors.sample(ids[1]);
    benchmark::DoNotOptimize(sink);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * ids.size()));
}

BENCHMARK(BM_tensor_flat_local);
BENCHMARK(BM_tensor_sml_local);
BENCHMARK(BM_tensor_flat_random);
BENCHMARK(BM_tensor_sml_random);
BENCHMARK(BM_tensor_flat_event_local);
BENCHMARK(BM_tensor_flat_event_random);
BENCHMARK(BM_tensor_sml_fused_local);
BENCHMARK(BM_tensor_sml_fused_random);
BENCHMARK(BM_tensor_sml_router_local);
BENCHMARK(BM_tensor_sml_router_random);
BENCHMARK(BM_tensor_sml_router_fold_local);
BENCHMARK(BM_tensor_sml_router_fold_random);
BENCHMARK(BM_dispatch_direct_single);
BENCHMARK(BM_dispatch_sml_single);
BENCHMARK(BM_dispatch_direct_actor_array_random);
BENCHMARK(BM_dispatch_sml_actor_array_random);
BENCHMARK(BM_dispatch_sml_actor_array_random_fold);
BENCHMARK(BM_dispatch_sml_pool_random);
BENCHMARK(BM_dispatch_sml_pool_batch_random);

BENCHMARK_MAIN();
#endif
