#include "engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
constexpr size_t kBatchSize = 256;
constexpr size_t kDefaultEvents = 1'000'000;
constexpr size_t kMaxEvents = 1 << 20;
constexpr size_t kRuns = 7;

struct RunResult {
    double seconds;
    uint64_t fills;
    std::vector<double> batch_microseconds;
};

RunResult run_once(size_t event_count) {
    lob::MatchingEngine engine(1, 1000);
    std::vector<double> batch_times;
    batch_times.reserve((event_count + kBatchSize - 1) / kBatchSize);
    uint64_t fill_count = 0;
    const auto run_start = Clock::now();

    for (size_t first = 0; first < event_count; first += kBatchSize) {
        const size_t last = std::min(first + kBatchSize, event_count);
        const auto batch_start = Clock::now();
        for (size_t i = first; i < last; ++i) {
            const lob::Side side = (i % 2 == 0) ? lob::Side::Buy : lob::Side::Sell;
            if (!engine.submit({static_cast<uint64_t>(i + 1), side, 100, 1})) {
                std::cerr << "event queue unexpectedly full\n";
                std::exit(EXIT_FAILURE);
            }
        }
        const auto fills = engine.process_events();
        fill_count += fills.size();
        const auto batch_end = Clock::now();
        batch_times.push_back(
            std::chrono::duration<double, std::micro>(batch_end - batch_start).count());
    }

    const auto run_end = Clock::now();
    if (engine.total_bid_volume() != 0 || engine.total_ask_volume() != 0 ||
        engine.live_order_count() != 0) {
        std::cerr << "workload left unexpected resting quantity\n";
        std::exit(EXIT_FAILURE);
    }
    return {
        std::chrono::duration<double>(run_end - run_start).count(),
        fill_count,
        std::move(batch_times),
    };
}

double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(
        p * static_cast<double>(values.size() - 1));
    return values[index];
}

} // namespace

int main(int argc, char **argv) {
    size_t event_count = kDefaultEvents;
    if (argc > 1) {
        char *end = nullptr;
        const unsigned long long parsed = std::strtoull(argv[1], &end, 10);
        if (end == argv[1] || *end != '\0' || parsed < 2 ||
            parsed > kMaxEvents || parsed % 2 != 0) {
            std::cerr << "usage: benchmark_lob [even event count, 2.."
                      << kMaxEvents << "]\n";
            return EXIT_FAILURE;
        }
        event_count = static_cast<size_t>(parsed);
    }

    // Warm up allocator, branch paths, and instruction cache; exclude this run.
    (void)run_once(std::min<size_t>(event_count, 100'000));

    std::vector<double> run_seconds;
    std::vector<double> batch_samples;
    uint64_t expected_fills = 0;
    for (size_t run = 0; run < kRuns; ++run) {
        RunResult result = run_once(event_count);
        if (run == 0) {
            expected_fills = result.fills;
        } else if (result.fills != expected_fills) {
            std::cerr << "fill count changed between runs\n";
            return EXIT_FAILURE;
        }
        run_seconds.push_back(result.seconds);
        batch_samples.insert(
            batch_samples.end(),
            result.batch_microseconds.begin(),
            result.batch_microseconds.end());
    }

    const double median_seconds = percentile(run_seconds, 0.50);
    const double throughput = static_cast<double>(event_count) / median_seconds;
    std::cout << "events_per_run=" << event_count << " runs=" << kRuns
              << " fills_per_run=" << expected_fills << '\n'
              << "median_throughput_events_per_second=" << throughput << '\n'
              << "batch_latency_us_p50=" << percentile(batch_samples, 0.50) << '\n'
              << "batch_latency_us_p95=" << percentile(batch_samples, 0.95) << '\n'
              << "batch_latency_us_p99=" << percentile(batch_samples, 0.99) << '\n'
              << "batch_size_events=" << kBatchSize << '\n'
              << "note=synthetic in-process benchmark; batch latency includes queue submission, matching, and fill-report allocation\n";
    return EXIT_SUCCESS;
}
