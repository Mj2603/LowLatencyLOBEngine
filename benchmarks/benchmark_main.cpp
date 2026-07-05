#include "engine.hpp"
#include <chrono>
#include <iostream>

int main() {
    lob::MatchingEngine engine;
    const size_t n = 1000000;
    const auto start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < n; ++i) {
        lob::Order order{i + 1, (i % 2 == 0) ? lob::Side::Buy : lob::Side::Sell, 100 + static_cast<uint32_t>(i % 10), 1};
        engine.submit(order);
    }

    engine.process_events();
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::cout << "Processed " << n << " events in " << elapsed << " seconds\n";
    std::cout << "Total bid volume=" << engine.total_bid_volume() << " ask volume=" << engine.total_ask_volume() << "\n";
    return 0;
}
