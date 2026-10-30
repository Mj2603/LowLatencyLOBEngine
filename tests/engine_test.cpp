#include "engine.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    {
        lob::OrderBook book(1, 10000);
        check(book.add_order({10, lob::Side::Sell, 100, 3}), "rest first ask");
        check(book.add_order({11, lob::Side::Sell, 100, 4}), "rest second ask");

        const auto fills = book.match_order({20, lob::Side::Buy, 100, 5});
        check(fills.size() == 2, "partial sweep produces two fills");
        check(fills[0].maker_id == 10 && fills[0].quantity == 3, "FIFO first maker");
        check(fills[1].maker_id == 11 && fills[1].quantity == 2, "FIFO second maker");
        check(book.ask_volume() == 2, "remaining ask volume");
        check(book.bid_volume() == 0, "fully filled taker does not rest");
    }

    {
        lob::OrderBook book(1, 10000);
        check(book.add_order({1, lob::Side::Sell, 101, 3}), "rest higher ask");
        const auto fills = book.match_order({2, lob::Side::Buy, 100, 5});
        check(fills.empty(), "buy limit does not cross higher ask");
        check(book.ask_volume() == 3, "higher ask remains");
        check(book.bid_volume() == 5, "unfilled buy rests at its limit");
        check(book.live_order_count() == 2, "both non-crossing orders remain live");
    }

    {
        lob::OrderBook book(1, 10000);
        check(book.add_order({1, lob::Side::Buy, 99, 10}), "rest bid");
        check(book.cancel_order(1, 3), "partial cancellation");
        check(book.bid_volume() == 7, "partial cancel updates volume");
        const auto fills = book.match_order({2, lob::Side::Sell, 99, 8});
        check(fills.size() == 1 && fills[0].quantity == 7, "sell fills remaining bid");
        check(book.ask_volume() == 1, "unfilled sell quantity rests");
        check(book.cancel_order(2), "full cancellation by zero quantity");
        check(book.ask_volume() == 0, "full cancel clears residual ask");
        check(!book.cancel_order(2), "unknown order cancellation is rejected");
    }

    {
        lob::OrderBook book(1, 10000);
        check(book.add_order({1, lob::Side::Buy, 10000, 1}), "highest configured tick is safe");
        check(!book.add_order({2, lob::Side::Buy, 10001, 1}), "out of range price rejected");
        check(!book.add_order({1, lob::Side::Buy, 5, 1}), "duplicate live order ID rejected");
        check(!book.add_order({3, lob::Side::Buy, 5, 0}), "zero quantity rejected");
    }

    {
        bool threw = false;
        try {
            lob::OrderBook invalid(10, 9);
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        check(threw, "invalid price range throws");
    }

    {
        lob::EventRing queue(3);
        const lob::OrderEvent event{{1, lob::Side::Buy, 100, 1}};
        check(queue.push(event), "ring push one");
        check(queue.push({{2, lob::Side::Sell, 100, 1}}), "ring push two");
        check(queue.push({{3, lob::Side::Buy, 100, 1}}), "ring push three");
        check(queue.push({{4, lob::Side::Sell, 100, 1}}), "rounded ring capacity is four");
        check(!queue.push(event), "full ring rejects event");
        lob::OrderEvent popped{};
        for (uint64_t id = 1; id <= 4; ++id) {
            check(queue.pop(popped) && popped.order.id == id, "ring preserves FIFO");
        }
        check(!queue.pop(popped), "empty ring reports no event");
    }

    {
        bool threw = false;
        try {
            lob::EventRing invalid(1);
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        check(threw, "invalid ring capacity throws");
    }

    {
        lob::MatchingEngine engine(1, 10000);
        check(engine.submit({1, lob::Side::Buy, 100, 2}), "submit maker");
        check(engine.submit({2, lob::Side::Sell, 100, 1}), "submit crossing taker");
        const auto fills = engine.process_events();
        check(fills.size() == 1 && fills[0].maker_id == 1, "engine returns execution report");
        check(engine.total_bid_volume() == 1, "engine preserves maker remainder");
        check(engine.cancel_order(1), "engine exposes cancellation");
        check(engine.live_order_count() == 0, "cancel removes order");
    }

    return EXIT_SUCCESS;
}
