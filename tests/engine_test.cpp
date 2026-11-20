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

    // Best bid/ask maintenance.
    {
        lob::OrderBook book(1, 10000);
        check(!book.best_bid() && !book.best_ask(), "empty book has no touch");
        check(book.add_order({1, lob::Side::Buy, 95, 5}), "bid 95");
        check(book.add_order({2, lob::Side::Buy, 97, 5}), "bid 97");
        check(book.add_order({3, lob::Side::Buy, 90, 5}), "bid 90");
        check(book.add_order({4, lob::Side::Sell, 103, 5}), "ask 103");
        check(book.add_order({5, lob::Side::Sell, 101, 5}), "ask 101");
        check(book.add_order({6, lob::Side::Sell, 110, 5}), "ask 110");
        check(book.best_bid() == 97u && book.best_ask() == 101u, "touch after adds");

        check(!book.add_order({7, lob::Side::Buy, 101, 1}), "add_order rejects crossing buy");
        check(!book.add_order({8, lob::Side::Sell, 97, 1}), "add_order rejects crossing sell");
        check(book.add_order({9, lob::Side::Buy, 100, 1}), "add_order accepts bid inside spread");
        check(book.best_bid() == 100u, "inside bid becomes best");
        check(book.cancel_order(9), "cancel inside bid");
        check(book.best_bid() == 97u, "best bid falls back after cancel");

        check(book.cancel_order(5, 2), "partial cancel at best ask");
        check(book.best_ask() == 101u, "partial cancel keeps touch");
        check(book.cancel_order(5), "cancel rest of best ask");
        check(book.best_ask() == 103u, "best ask advances after full cancel");
        check(book.cancel_order(3), "cancel non-best bid");
        check(book.best_bid() == 97u, "non-best cancel leaves touch");

        // Sweep: buy 7 @ 110 takes all of 103 and 2 of 110.
        auto fills = book.match_order({20, lob::Side::Buy, 110, 7});
        check(fills.size() == 2 && fills[0].price == 103 && fills[1].price == 110,
              "sweep fills in price order at maker prices");
        check(book.best_ask() == 110u && book.volume_at(lob::Side::Sell, 110) == 3,
              "partial fill leaves residual at new best ask");
        fills = book.match_order({21, lob::Side::Buy, 120, 10});
        check(fills.size() == 1 && fills[0].quantity == 3, "taker clears last ask level");
        check(!book.best_ask(), "ask side empty after sweep");
        check(book.best_bid() == 120u && book.volume_at(lob::Side::Buy, 120) == 7,
              "taker residual rests and becomes best bid");

        fills = book.match_order({22, lob::Side::Sell, 1, 100});
        check(fills.size() == 3, "sell sweeps every bid level");
        check(fills[0].price == 120 && fills[1].price == 97 && fills[2].price == 95,
              "sell sweeps from highest bid downward");
        check(!book.best_bid() && book.best_ask() == 1u, "residual sell rests at its limit");
        check(book.volume_at(lob::Side::Sell, 1) == 100 - 7 - 5 - 5, "residual quantity");
    }

    // FIFO within a level across partial fills, and cancel in the middle of a queue.
    {
        lob::OrderBook book(1, 1000);
        for (uint64_t id = 1; id <= 5; ++id) {
            check(book.add_order({id, lob::Side::Sell, 500, 10}), "queue ask");
        }
        check(book.cancel_order(3), "cancel middle of queue");
        auto fills = book.match_order({100, lob::Side::Buy, 500, 15});
        check(fills.size() == 2 && fills[0].maker_id == 1 && fills[1].maker_id == 2 &&
                  fills[1].quantity == 5,
              "FIFO across makers with partial fill of second");
        fills = book.match_order({101, lob::Side::Buy, 500, 17});
        check(fills.size() == 3 && fills[0].maker_id == 2 && fills[0].quantity == 5 &&
                  fills[1].maker_id == 4 && fills[2].maker_id == 5 && fills[2].quantity == 2,
              "partially filled maker keeps queue priority; cancelled maker skipped");
        check(book.best_ask() == 500u && book.ask_volume() == 8, "remaining queue");
        check(book.add_order({6, lob::Side::Sell, 500, 1}), "late arrival joins back");
        fills = book.match_order({102, lob::Side::Buy, 500, 9});
        check(fills.size() == 2 && fills[0].maker_id == 5 && fills[1].maker_id == 6,
              "late arrival fills after earlier maker");
        check(!book.best_ask() && book.live_order_count() == 0, "book flat");
    }

    // Touch tracking at range edges and across bitmap word/group boundaries.
    {
        lob::OrderBook book(1, 1'000'000);
        check(book.add_order({1, lob::Side::Buy, 1, 1}), "bid at min tick");
        check(book.add_order({2, lob::Side::Sell, 1'000'000, 1}), "ask at max tick");
        check(book.add_order({3, lob::Side::Buy, 4096 * 3 + 7, 1}), "bid across groups");
        check(book.add_order({4, lob::Side::Sell, 999'000, 1}), "ask far away");
        check(book.best_bid() == uint32_t{4096 * 3 + 7} && book.best_ask() == 999'000u,
              "touch with distant levels");
        auto fills = book.match_order({5, lob::Side::Sell, 1, 3});
        check(fills.size() == 2 && fills[1].price == 1, "sell walks bids across bitmap groups");
        check(!book.best_bid() && book.best_ask() == 1u, "residual sell at min tick");
        check(book.cancel_order(5), "cancel residual");
        check(book.best_ask() == 999'000u, "best ask walks forward across groups");
        check(book.cancel_order(4), "cancel");
        check(book.best_ask() == 1'000'000u, "best ask at max tick");
        check(book.cancel_order(2), "cancel");
        check(!book.best_ask(), "ask side empty");
    }

    // LevelBitmap directly.
    {
        lob::LevelBitmap bits(10'000);
        check(bits.find_next(0) == lob::LevelBitmap::npos, "empty bitmap next");
        check(bits.find_prev(9'999) == lob::LevelBitmap::npos, "empty bitmap prev");
        bits.set(0);
        bits.set(63);
        bits.set(64);
        bits.set(4095);
        bits.set(4096);
        bits.set(9'999);
        check(bits.find_next(1) == 63 && bits.find_next(64) == 64 && bits.find_next(65) == 4095,
              "find_next within and across words");
        check(bits.find_next(4097) == 9'999 && bits.find_next(10'000) == lob::LevelBitmap::npos,
              "find_next across groups and past end");
        check(bits.find_prev(9'998) == 4096 && bits.find_prev(4095) == 4095 &&
                  bits.find_prev(62) == 0,
              "find_prev within and across words/groups");
        bits.clear(0);
        check(bits.find_prev(62) == lob::LevelBitmap::npos, "find_prev after clear");
        bits.clear(4096);
        bits.clear(4095);
        check(bits.find_next(65) == 9'999 && bits.find_prev(9'998) == 64, "clear updates summary");
        check(bits.test(63) && !bits.test(0) && !bits.test(10'000), "test");
    }

    return EXIT_SUCCESS;
}
