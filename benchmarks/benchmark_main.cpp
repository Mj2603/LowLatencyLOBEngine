// Synthetic in-process benchmark for the matching engine.
//
// Two workloads:
//   stress  - degenerate alternating buy/sell one-lot orders at a single price
//             (every second order crosses; the book never holds more than one
//             order). Kept for continuity with earlier versions.
//   random  - seeded, deterministic multi-level flow: 60% passive limit adds
//             spread over 50 levels each side of the touch midpoint, 20%
//             marketable orders that can sweep several levels, and 20% cancels
//             of currently resting orders.
//
// For every workload the benchmark performs one warm-up run and then R measured
// runs. Each measured run replays the same pre-generated event stream twice,
// each time on a fresh engine:
//   1. a throughput pass with no per-event timing (wall time for the stream),
//   2. a latency pass with steady_clock::now() around each individual event
//      (submit + process_events for orders, cancel_order for cancels).
// Event generation happens before timing starts and is excluded.

#include "engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

#ifndef LOB_BENCH_CXX_FLAGS
#define LOB_BENCH_CXX_FLAGS "unknown"
#endif
#ifndef LOB_BENCH_BUILD_TYPE
#define LOB_BENCH_BUILD_TYPE "unknown"
#endif

enum class Kind : uint8_t { Order, Cancel };

struct Event {
    Kind kind;
    lob::Order order; // for Cancel only order.id is used
};

struct Workload {
    std::string name;
    uint32_t min_price;
    uint32_t max_price;
    std::vector<Event> events;
};

Workload make_stress(size_t count) {
    Workload w{"stress", 1, 1000, {}};
    w.events.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const lob::Side side = (i % 2 == 0) ? lob::Side::Buy : lob::Side::Sell;
        w.events.push_back({Kind::Order, {static_cast<uint64_t>(i + 1), side, 100, 1}});
    }
    return w;
}

// Deterministic random flow. The stream is generated once, before any timing,
// by driving a scratch engine and tracking its resting orders, so that
//   - passive adds are placed 1..50 ticks behind the current touch midpoint,
//   - marketable orders are priced 0..30 ticks through that midpoint, and
//   - cancels target an order that is actually resting (uniformly chosen).
// The resulting stream is fixed by the seed and replayed identically for every
// run and both passes.
Workload make_random(size_t count, uint64_t seed) {
    constexpr uint32_t kMin = 1;
    constexpr uint32_t kMax = 20000;
    constexpr int64_t kSpreadLevels = 50;
    constexpr int kCancelPct = 20;
    constexpr int kPassivePct = 60; // remaining 20% are marketable
    Workload w{"random", kMin, kMax, {}};
    w.events.reserve(count);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99);
    std::uniform_int_distribution<int64_t> offset(1, kSpreadLevels);
    std::uniform_int_distribution<int64_t> cross(0, 30);
    std::uniform_int_distribution<uint32_t> passive_qty(1, 100);
    std::uniform_int_distribution<uint32_t> taker_qty(50, 400);

    // Generator-side bookkeeping only (not part of any timed path).
    struct Live {
        uint32_t qty;
        size_t index;
        lob::Side side;
        uint32_t price;
    };
    lob::MatchingEngine shadow(kMin, kMax);
    std::vector<uint64_t> live;
    std::unordered_map<uint64_t, Live> where;
    std::map<uint32_t, uint32_t> bid_levels; // price -> resting order count
    std::map<uint32_t, uint32_t> ask_levels;
    auto remove_live = [&](uint64_t id) {
        const auto it = where.find(id);
        auto &levels = it->second.side == lob::Side::Buy ? bid_levels : ask_levels;
        const auto level = levels.find(it->second.price);
        if (--level->second == 0) {
            levels.erase(level);
        }
        const size_t idx = it->second.index;
        live[idx] = live.back();
        where[live[idx]].index = idx;
        live.pop_back();
        where.erase(it);
    };

    int64_t mid = (kMin + kMax) / 2;
    uint64_t next_id = 1;
    for (size_t i = 0; i < count; ++i) {
        if (!bid_levels.empty() && !ask_levels.empty()) {
            mid = (static_cast<int64_t>(bid_levels.rbegin()->first) + ask_levels.begin()->first) / 2;
        }
        mid = std::clamp<int64_t>(mid, kMin + kSpreadLevels, kMax - kSpreadLevels);
        const int roll = pct(rng);
        if (roll < kCancelPct && !live.empty()) {
            std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
            const uint64_t id = live[pick(rng)];
            shadow.cancel_order(id);
            remove_live(id);
            w.events.push_back({Kind::Cancel, {id, lob::Side::Buy, 0, 0}});
            continue;
        }
        const bool buy = (rng() & 1U) != 0;
        lob::Order order{next_id++, buy ? lob::Side::Buy : lob::Side::Sell, 0, 0};
        if (roll < kCancelPct + kPassivePct) {
            const int64_t px = buy ? mid - offset(rng) : mid + offset(rng);
            order.price = static_cast<uint32_t>(px);
            order.quantity = passive_qty(rng);
        } else {
            const int64_t px = buy ? mid + cross(rng) : mid - cross(rng);
            order.price = static_cast<uint32_t>(px);
            order.quantity = taker_qty(rng);
        }
        w.events.push_back({Kind::Order, order});
        shadow.submit(order);
        uint32_t filled = 0;
        for (const lob::Fill &f : shadow.process_events()) {
            filled += f.quantity;
            auto &entry = where.at(f.maker_id);
            entry.qty -= f.quantity;
            if (entry.qty == 0) {
                remove_live(f.maker_id);
            }
        }
        if (filled < order.quantity) {
            where.emplace(order.id, Live{order.quantity - filled, live.size(), order.side, order.price});
            live.push_back(order.id);
            ++(buy ? bid_levels : ask_levels)[order.price];
        }
    }
    return w;
}

struct Counters {
    uint64_t fills{0};
    uint64_t cancels_ok{0};
    uint64_t cancels_rejected{0};
    size_t live_orders_end{0};
};

inline void apply(lob::MatchingEngine &engine, const Event &e, Counters &c) {
    if (e.kind == Kind::Cancel) {
        if (engine.cancel_order(e.order.id)) {
            ++c.cancels_ok;
        } else {
            ++c.cancels_rejected;
        }
        return;
    }
    if (!engine.submit(e.order)) {
        std::cerr << "event queue unexpectedly full\n";
        std::exit(EXIT_FAILURE);
    }
    const auto fills = engine.process_events();
    c.fills += fills.size();
}

double throughput_pass(const Workload &w, Counters &c) {
    lob::MatchingEngine engine(w.min_price, w.max_price);
    const auto start = Clock::now();
    for (const Event &e : w.events) {
        apply(engine, e, c);
    }
    const auto end = Clock::now();
    c.live_orders_end = engine.live_order_count();
    return std::chrono::duration<double>(end - start).count();
}

void latency_pass(const Workload &w, Counters &c, std::vector<double> &out_ns) {
    lob::MatchingEngine engine(w.min_price, w.max_price);
    for (const Event &e : w.events) {
        const auto t0 = Clock::now();
        apply(engine, e, c);
        const auto t1 = Clock::now();
        out_ns.push_back(static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }
    c.live_orders_end = engine.live_order_count();
}

double percentile_sorted(const std::vector<double> &sorted, double p) {
    if (sorted.empty()) {
        return 0.0;
    }
    const size_t index = static_cast<size_t>(p * static_cast<double>(sorted.size() - 1) + 0.5);
    return sorted[std::min(index, sorted.size() - 1)];
}

double clock_overhead_ns() {
    constexpr int kSamples = 1'000'000;
    std::vector<double> v;
    v.reserve(kSamples);
    for (int i = 0; i < kSamples; ++i) {
        const auto t0 = Clock::now();
        const auto t1 = Clock::now();
        v.push_back(static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }
    std::sort(v.begin(), v.end());
    return percentile_sorted(v, 0.5);
}

std::string cpu_model() {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0) {
            const auto colon = line.find(':');
            if (colon != std::string::npos && colon + 1 < line.size()) {
                const auto first = line.find_first_not_of(' ', colon + 1);
                return first == std::string::npos ? "unknown" : line.substr(first);
            }
        }
    }
    return "unknown (no /proc/cpuinfo model name)";
}

std::string compiler() {
#if defined(__clang__)
    return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("gcc ") + __VERSION__;
#elif defined(_MSC_VER)
    return "msvc " + std::to_string(_MSC_VER);
#else
    return "unknown";
#endif
}

void run_workload(const Workload &w, size_t runs) {
    // Warm-up: one full pass of each kind, results discarded.
    {
        Counters c;
        (void)throughput_pass(w, c);
        std::vector<double> scratch;
        scratch.reserve(w.events.size());
        latency_pass(w, c, scratch);
    }

    std::vector<double> run_tput;
    std::vector<double> all_ns;
    all_ns.reserve(w.events.size() * runs);
    Counters reference;
    for (size_t r = 0; r < runs; ++r) {
        Counters tc;
        const double seconds = throughput_pass(w, tc);
        run_tput.push_back(static_cast<double>(w.events.size()) / seconds);
        Counters lc;
        latency_pass(w, lc, all_ns);
        if (tc.fills != lc.fills || tc.cancels_ok != lc.cancels_ok ||
            tc.live_orders_end != lc.live_orders_end ||
            (r > 0 && (tc.fills != reference.fills || tc.cancels_ok != reference.cancels_ok))) {
            std::cerr << "non-deterministic result between passes/runs\n";
            std::exit(EXIT_FAILURE);
        }
        reference = tc;
    }
    if (w.name == "stress" && reference.live_orders_end != 0) {
        std::cerr << "stress workload left unexpected resting orders\n";
        std::exit(EXIT_FAILURE);
    }

    std::sort(run_tput.begin(), run_tput.end());
    std::sort(all_ns.begin(), all_ns.end());
    double sum = 0.0;
    for (double v : all_ns) {
        sum += v;
    }

    std::cout << std::fixed << std::setprecision(0);
    std::cout << "[workload " << w.name << "]\n"
              << "events_per_run=" << w.events.size() << " measured_runs=" << runs
              << " price_range=" << w.min_price << ".." << w.max_price << '\n'
              << "fills_per_run=" << reference.fills
              << " cancels_ok=" << reference.cancels_ok
              << " cancels_rejected=" << reference.cancels_rejected
              << " live_orders_at_end=" << reference.live_orders_end << '\n'
              << "throughput_events_per_sec median=" << percentile_sorted(run_tput, 0.5)
              << " min=" << run_tput.front() << " max=" << run_tput.back() << '\n'
              << "per_event_latency_ns samples=" << all_ns.size()
              << " mean=" << (sum / static_cast<double>(all_ns.size()))
              << " p50=" << percentile_sorted(all_ns, 0.50)
              << " p90=" << percentile_sorted(all_ns, 0.90)
              << " p99=" << percentile_sorted(all_ns, 0.99)
              << " p99.9=" << percentile_sorted(all_ns, 0.999)
              << " max=" << all_ns.back() << '\n';
}

[[noreturn]] void usage() {
    std::cerr << "usage: benchmark_lob [--workload stress|random|all] [--events N] "
                 "[--runs R] [--seed S]\n"
                 "defaults: --workload all --events 1000000 --runs 5 --seed 42\n";
    std::exit(EXIT_FAILURE);
}

uint64_t parse_u64(const char *text) {
    char *end = nullptr;
    const unsigned long long v = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0') {
        usage();
    }
    return v;
}

} // namespace

int main(int argc, char **argv) {
    std::string workload = "all";
    size_t events = 1'000'000;
    size_t runs = 5;
    uint64_t seed = 42;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) {
            usage();
        }
        if (arg == "--workload") {
            workload = argv[++i];
        } else if (arg == "--events") {
            events = static_cast<size_t>(parse_u64(argv[++i]));
        } else if (arg == "--runs") {
            runs = static_cast<size_t>(parse_u64(argv[++i]));
        } else if (arg == "--seed") {
            seed = parse_u64(argv[++i]);
        } else {
            usage();
        }
    }
    if ((workload != "stress" && workload != "random" && workload != "all") ||
        events < 2 || events > 50'000'000 || runs < 1 || runs > 100) {
        usage();
    }

    std::cout << "cpu=" << cpu_model() << '\n'
              << "compiler=" << compiler() << '\n'
              << "build_type=" << LOB_BENCH_BUILD_TYPE << " cxx_flags=" << LOB_BENCH_CXX_FLAGS
              << '\n'
              << "seed=" << seed << '\n'
              << "steady_clock_overhead_ns_p50=" << std::fixed << std::setprecision(0)
              << clock_overhead_ns() << '\n'
              << "note=single-threaded in-process synthetic benchmark; per-event latency "
                 "includes ring submit, matching, fill-vector allocation and one "
                 "steady_clock::now() pair\n";

    if (workload == "stress" || workload == "all") {
        run_workload(make_stress(events), runs);
    }
    if (workload == "random" || workload == "all") {
        run_workload(make_random(events, seed), runs);
    }
    return EXIT_SUCCESS;
}
