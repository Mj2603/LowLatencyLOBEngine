#include "engine.hpp"
#include <iostream>

int main() {
    lob::MatchingEngine engine;
    lob::Order order1{1, lob::Side::Buy, 100, 10};
    lob::Order order2{2, lob::Side::Sell, 100, 5};

    engine.submit(order1);
    engine.submit(order2);
    engine.process_events();

    std::cout << "Bid volume=" << engine.total_bid_volume() << " Ask volume=" << engine.total_ask_volume() << "\n";
    return 0;
}
