// Generic "10k actor" dispatch benchmark focused on SML dispatch overhead.
// This intentionally avoids use-case-specific event semantics (e.g. link/unlink).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <boost/sml.hpp>
#include <boost/sml/utility/sm_pool.hpp>

#if defined(TEST_GBENCH)
#include <benchmark/benchmark.h>
#endif

namespace sml = boost::sml;

namespace {

constexpr std::size_t kActorCount = 10'000;
constexpr std::size_t kDispatchCount = 50'000;

struct ev_pulse {};
using pooled_pulse = sml::utility::indexed_event<ev_pulse>;

enum class access_pattern : std::uint8_t { local, random };

std::vector<std::uint16_t> make_ids(const access_pattern pattern) {
  std::vector<std::uint16_t> ids;
  ids.reserve(kDispatchCount);

  if (pattern == access_pattern::local) {
    for (std::size_t i = 0; i < kDispatchCount; ++i) {
      ids.push_back(static_cast<std::uint16_t>(i % kActorCount));
    }
    return ids;
  }

  std::minstd_rand rng{1337};
  std::uniform_int_distribution<unsigned> dist{0u, static_cast<unsigned>(kActorCount - 1)};
  for (std::size_t i = 0; i < kDispatchCount; ++i) {
    ids.push_back(static_cast<std::uint16_t>(dist(rng)));
  }
  return ids;
}

const std::vector<std::uint16_t>& local_ids() {
  static const auto ids = make_ids(access_pattern::local);
  return ids;
}

const std::vector<std::uint16_t>& random_ids() {
  static const auto ids = make_ids(access_pattern::random);
  return ids;
}

struct direct_pool {
  direct_pool() : flags(kActorCount) {}

  bool process_event(const std::uint16_t id) {
    auto& flag = flags[id];
    flag ^= static_cast<std::uint8_t>(1);
    return flag != 0;
  }

  std::uint8_t sample(const std::uint16_t id) const { return flags[id]; }

  std::vector<std::uint8_t> flags;
};

struct pulse_actor {
  auto operator()() const {
    using namespace sml;
    // clang-format off
    return make_transition_table(
      *"off"_s + event<ev_pulse> = "on"_s,
       "on"_s + event<ev_pulse> = "off"_s
    );
    // clang-format on
  }
};

struct actor_pool {
  actor_pool() {
    actors.reserve(kActorCount);
    for (std::size_t i = 0; i < kActorCount; ++i) {
      actors.emplace_back();
    }
  }

  bool process_event(const std::uint16_t id) { return actors[id].process_event(ev_pulse{}); }

  bool sample(const std::uint16_t id) const {
    using namespace sml;
    return actors[id].is("on"_s);
  }

  std::vector<sml::sm<pulse_actor>> actors;
};

struct pool_storage {
  explicit pool_storage(const std::size_t count) : flags(count) {}

  void reset() { std::fill(flags.begin(), flags.end(), static_cast<std::uint8_t>(0)); }

  std::vector<std::uint8_t> flags;
};

struct router_actor {
  auto operator()() const {
    using namespace sml;
    const auto hot = "hot"_s;
    const auto toggle = [](pool_storage& storage, const pooled_pulse& ev) {
      storage.flags[ev.id] ^= static_cast<std::uint8_t>(1);
    };
    // clang-format off
    return make_transition_table(
      *hot + event<pooled_pulse> / toggle
    );
    // clang-format on
  }
};

struct pooled_dispatch {
  pooled_dispatch() : pool(kActorCount) {}

  bool process_event(const std::uint16_t id) {
    pool.template process_indexed<ev_pulse>(id);
    return pool.storage().flags[id] != 0;
  }

  std::size_t process_batch(const std::vector<std::uint16_t>& ids) {
    return pool.template process_indexed_batch<ev_pulse>(ids);
  }

  std::uint8_t sample(const std::uint16_t id) const { return pool.storage().flags[id]; }

  sml::utility::sm_pool<pool_storage, router_actor> pool;
};

}  // namespace

#if defined(TEST_ASM)
int main() {
  auto ids = random_ids();

  direct_pool direct;
  actor_pool actor;
  pooled_dispatch pooled;

  for (std::size_t i = 0; i < 128; ++i) {
    const auto id = ids[i % ids.size()];
    direct.process_event(id);
    actor.process_event(id);
    pooled.process_event(id);
  }

  return static_cast<int>(direct.sample(0) + actor.sample(0) + pooled.sample(0));
}
#elif defined(TEST_PERF)
int main() {
  const auto& ids = random_ids();
  direct_pool direct;
  actor_pool actor;
  pooled_dispatch pooled;

  std::uint64_t sink = 0;
  for (std::size_t round = 0; round < 1000; ++round) {
    for (const auto id : ids) {
      sink += static_cast<std::uint64_t>(direct.process_event(id));
    }
    for (const auto id : ids) {
      sink += static_cast<std::uint64_t>(actor.process_event(id));
    }
    sink += pooled.process_batch(ids);
  }

  return static_cast<int>(sink & 0xFFu);
}
#elif defined(TEST_GBENCH)
void run_scalar_bench(benchmark::State& state, const std::vector<std::uint16_t>& ids, direct_pool& pool) {
  for (auto _ : state) {
    for (const auto id : ids) {
      benchmark::DoNotOptimize(pool.process_event(id));
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(ids.size()));
  benchmark::DoNotOptimize(pool.sample(ids.front()));
}

void run_scalar_bench(benchmark::State& state, const std::vector<std::uint16_t>& ids, actor_pool& pool) {
  for (auto _ : state) {
    for (const auto id : ids) {
      benchmark::DoNotOptimize(pool.process_event(id));
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(ids.size()));
  benchmark::DoNotOptimize(pool.sample(ids.front()));
}

void run_scalar_bench(benchmark::State& state, const std::vector<std::uint16_t>& ids, pooled_dispatch& pool) {
  for (auto _ : state) {
    for (const auto id : ids) {
      benchmark::DoNotOptimize(pool.process_event(id));
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(ids.size()));
  benchmark::DoNotOptimize(pool.sample(ids.front()));
}

void run_batch_bench(benchmark::State& state, const std::vector<std::uint16_t>& ids, pooled_dispatch& pool) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(pool.process_batch(ids));
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(ids.size()));
  benchmark::DoNotOptimize(pool.sample(ids.front()));
}

static void BM_direct_local(benchmark::State& state) {
  direct_pool pool;
  run_scalar_bench(state, local_ids(), pool);
}

static void BM_direct_random(benchmark::State& state) {
  direct_pool pool;
  run_scalar_bench(state, random_ids(), pool);
}

static void BM_sml_actor_local(benchmark::State& state) {
  actor_pool pool;
  run_scalar_bench(state, local_ids(), pool);
}

static void BM_sml_actor_random(benchmark::State& state) {
  actor_pool pool;
  run_scalar_bench(state, random_ids(), pool);
}

static void BM_sml_pool_local(benchmark::State& state) {
  pooled_dispatch pool;
  run_scalar_bench(state, local_ids(), pool);
}

static void BM_sml_pool_random(benchmark::State& state) {
  pooled_dispatch pool;
  run_scalar_bench(state, random_ids(), pool);
}

static void BM_sml_pool_batch_local(benchmark::State& state) {
  pooled_dispatch pool;
  run_batch_bench(state, local_ids(), pool);
}

static void BM_sml_pool_batch_random(benchmark::State& state) {
  pooled_dispatch pool;
  run_batch_bench(state, random_ids(), pool);
}

BENCHMARK(BM_direct_local);
BENCHMARK(BM_direct_random);
BENCHMARK(BM_sml_actor_local);
BENCHMARK(BM_sml_actor_random);
BENCHMARK(BM_sml_pool_local);
BENCHMARK(BM_sml_pool_random);
BENCHMARK(BM_sml_pool_batch_local);
BENCHMARK(BM_sml_pool_batch_random);
BENCHMARK_MAIN();
#endif
