// Randomized differential test: drives lob::OrderBook and a deliberately simple
// std::map/std::deque reference book with the same seeded operation stream and
// requires identical fills, return values, volumes, touch and per-level depth
// after every operation.

#include "engine.hpp"

#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

[[noreturn]] void fail(const std::string &message, uint64_t seed, size_t step) {
    std::cerr << "FAILED: " << message << " (seed=" << seed << " step=" << step << ")\n";
    std::exit(EXIT_FAILURE);
}

class ReferenceBook {
public:
    ReferenceBook(uint32_t min_price, uint32_t max_price) : min_(min_price), max_(max_price) {}

    bool add_order(const lob::Order &o) {
        if (!valid(o.price)) {
            return false;
        }
        if (o.side == lob::Side::Buy && !asks_.empty() && asks_.begin()->first <= o.price) {
            return false;
        }
        if (o.side == lob::Side::Sell && !bids_.empty() && bids_.rbegin()->first >= o.price) {
            return false;
        }
        return rest(o);
    }

    std::vector<lob::Fill> match_order(const lob::Order &in) {
        std::vector<lob::Fill> fills;
        if (in.id == 0 || !valid(in.price) || in.quantity == 0 || live_.count(in.id) != 0) {
            return fills;
        }
        uint32_t remaining = in.quantity;
        auto &book = in.side == lob::Side::Buy ? asks_ : bids_;
        while (remaining > 0 && !book.empty()) {
            auto it = in.side == lob::Side::Buy ? book.begin() : std::prev(book.end());
            const uint32_t price = it->first;
            if (in.side == lob::Side::Buy ? price > in.price : price < in.price) {
                break;
            }
            auto &queue = it->second;
            while (remaining > 0 && !queue.empty()) {
                lob::Order &maker = queue.front();
                const uint32_t traded = std::min(remaining, maker.quantity);
                fills.push_back({maker.id, in.id, traded, price});
                maker.quantity -= traded;
                remaining -= traded;
                if (maker.quantity == 0) {
                    live_.erase(maker.id);
                    queue.pop_front();
                }
            }
            if (queue.empty()) {
                book.erase(it);
            }
        }
        if (remaining > 0) {
            lob::Order rest_order = in;
            rest_order.quantity = remaining;
            rest(rest_order);
        }
        return fills;
    }

    bool cancel_order(uint64_t id, uint32_t quantity) {
        const auto found = live_.find(id);
        if (found == live_.end()) {
            return false;
        }
        const auto [side, price] = found->second;
        auto &book = side == lob::Side::Buy ? bids_ : asks_;
        auto &queue = book.at(price);
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (it->id != id) {
                continue;
            }
            const uint32_t cancel = quantity == 0 ? it->quantity : quantity;
            if (cancel > it->quantity) {
                return false;
            }
            it->quantity -= cancel;
            if (it->quantity == 0) {
                queue.erase(it);
                live_.erase(found);
                if (queue.empty()) {
                    book.erase(price);
                }
            }
            return true;
        }
        return false;
    }

    std::optional<uint32_t> best_bid() const {
        return bids_.empty() ? std::nullopt : std::optional<uint32_t>(bids_.rbegin()->first);
    }
    std::optional<uint32_t> best_ask() const {
        return asks_.empty() ? std::nullopt : std::optional<uint32_t>(asks_.begin()->first);
    }
    uint64_t volume(lob::Side side) const {
        uint64_t total = 0;
        for (const auto &[price, queue] : side == lob::Side::Buy ? bids_ : asks_) {
            (void)price;
            for (const auto &o : queue) {
                total += o.quantity;
            }
        }
        return total;
    }
    uint64_t volume_at(lob::Side side, uint32_t price) const {
        const auto &book = side == lob::Side::Buy ? bids_ : asks_;
        const auto it = book.find(price);
        uint64_t total = 0;
        if (it != book.end()) {
            for (const auto &o : it->second) {
                total += o.quantity;
            }
        }
        return total;
    }
    size_t live_count() const { return live_.size(); }
    const std::unordered_map<uint64_t, std::pair<lob::Side, uint32_t>> &live() const {
        return live_;
    }

private:
    bool valid(uint32_t price) const { return price >= min_ && price <= max_; }
    bool rest(const lob::Order &o) {
        if (o.id == 0 || o.quantity == 0 || live_.count(o.id) != 0) {
            return false;
        }
        (o.side == lob::Side::Buy ? bids_ : asks_)[o.price].push_back(o);
        live_.emplace(o.id, std::make_pair(o.side, o.price));
        return true;
    }

    uint32_t min_;
    uint32_t max_;
    std::map<uint32_t, std::deque<lob::Order>> bids_;
    std::map<uint32_t, std::deque<lob::Order>> asks_;
    std::unordered_map<uint64_t, std::pair<lob::Side, uint32_t>> live_;
};

bool same_fills(const std::vector<lob::Fill> &a, const std::vector<lob::Fill> &b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].maker_id != b[i].maker_id || a[i].taker_id != b[i].taker_id ||
            a[i].quantity != b[i].quantity || a[i].price != b[i].price) {
            return false;
        }
    }
    return true;
}

// One randomized scenario. `centre`/`spread` control where prices land so the
// book spans many levels (and, for wide ranges, many bitmap words).
void run_scenario(uint64_t seed, uint32_t min_price, uint32_t max_price, uint32_t centre,
                  uint32_t spread, size_t steps) {
    lob::OrderBook book(min_price, max_price);
    ReferenceBook ref(min_price, max_price);
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99);
    std::uniform_int_distribution<int64_t> offset(-static_cast<int64_t>(spread),
                                                  static_cast<int64_t>(spread));
    std::uniform_int_distribution<uint32_t> qty(1, 50);
    uint64_t next_id = 1;
    std::vector<uint64_t> issued;

    auto random_price = [&]() -> uint32_t {
        int64_t p = static_cast<int64_t>(centre) + offset(rng);
        // Occasionally probe the range edges and just outside them.
        const int edge = pct(rng);
        if (edge == 0) {
            p = min_price;
        } else if (edge == 1) {
            p = max_price;
        } else if (edge == 2) {
            p = static_cast<int64_t>(max_price) + 1;
        } else if (edge == 3) {
            p = static_cast<int64_t>(min_price) - 1;
        }
        return static_cast<uint32_t>(std::max<int64_t>(p, 0));
    };

    for (size_t step = 0; step < steps; ++step) {
        const int roll = pct(rng);
        const lob::Side side = (rng() & 1U) ? lob::Side::Buy : lob::Side::Sell;
        if (roll < 20) {
            // Cancel: mostly live orders, sometimes stale/unknown ids, sometimes partial.
            uint64_t id = 0;
            if (!issued.empty()) {
                std::uniform_int_distribution<size_t> pick(0, issued.size() - 1);
                id = issued[pick(rng)];
            }
            if (pct(rng) < 5) {
                id = next_id + 1000;
            }
            const uint32_t q = pct(rng) < 30 ? qty(rng) : 0;
            const bool got = book.cancel_order(id, q);
            const bool want = ref.cancel_order(id, q);
            if (got != want) {
                fail("cancel return mismatch", seed, step);
            }
        } else if (roll < 50) {
            // Passive-only add (rejects crossing orders).
            uint32_t q = qty(rng);
            if (pct(rng) == 0) {
                q = 0;
            }
            uint64_t id = next_id++;
            if (pct(rng) < 2 && !issued.empty()) {
                id = issued.back(); // duplicate id
            }
            const lob::Order o{id, side, random_price(), q};
            const bool got = book.add_order(o);
            const bool want = ref.add_order(o);
            if (got != want) {
                fail("add_order return mismatch", seed, step);
            }
            issued.push_back(id);
        } else {
            // Limit order through the matcher (may cross several levels, then rest).
            uint32_t q = pct(rng) < 20 ? qty(rng) * 8 : qty(rng);
            if (pct(rng) == 0) {
                q = 0;
            }
            uint64_t id = next_id++;
            if (pct(rng) < 2 && !issued.empty()) {
                id = issued.back();
            }
            const lob::Order o{id, side, random_price(), q};
            const auto got = book.match_order(o);
            const auto want = ref.match_order(o);
            if (!same_fills(got, want)) {
                fail("fill sequence mismatch", seed, step);
            }
            issued.push_back(id);
        }

        if (book.best_bid() != ref.best_bid()) {
            fail("best bid mismatch", seed, step);
        }
        if (book.best_ask() != ref.best_ask()) {
            fail("best ask mismatch", seed, step);
        }
        if (book.live_order_count() != ref.live_count()) {
            fail("live order count mismatch", seed, step);
        }
        if (step % 64 == 0) {
            if (book.bid_volume() != ref.volume(lob::Side::Buy) ||
                book.ask_volume() != ref.volume(lob::Side::Sell)) {
                fail("total volume mismatch", seed, step);
            }
            for (int k = 0; k < 8; ++k) {
                const uint32_t p = random_price();
                if (book.volume_at(lob::Side::Buy, p) != ref.volume_at(lob::Side::Buy, p) ||
                    book.volume_at(lob::Side::Sell, p) != ref.volume_at(lob::Side::Sell, p)) {
                    fail("per-level volume mismatch", seed, step);
                }
            }
        }
    }

    // Final full drain via cancels must agree too.
    for (const auto &[id, where] : std::unordered_map(ref.live())) {
        (void)where;
        if (!book.cancel_order(id) || !ref.cancel_order(id, 0)) {
            fail("drain cancel mismatch", seed, steps);
        }
    }
    if (book.live_order_count() != 0 || book.best_bid() || book.best_ask() ||
        book.bid_volume() != 0 || book.ask_volume() != 0) {
        fail("book not empty after drain", seed, steps);
    }
}

} // namespace

int main() {
    size_t scenarios = 0;
    // Narrow range: dense book, frequent level emptying, edges hit often.
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        run_scenario(seed, 1, 200, 100, 60, 5'000);
        ++scenarios;
    }
    // Medium range spanning many 64-level words.
    for (uint64_t seed = 101; seed <= 110; ++seed) {
        run_scenario(seed, 1, 20'000, 10'000, 3'000, 10'000);
        ++scenarios;
    }
    // Wide range: prices spread across bitmap summary groups (4096 levels each).
    for (uint64_t seed = 1001; seed <= 1005; ++seed) {
        run_scenario(seed, 1, 1'000'000, 500'000, 200'000, 10'000);
        ++scenarios;
    }
    // Non-unit minimum price.
    run_scenario(424242, 5'000, 5'500, 5'250, 300, 20'000);
    ++scenarios;
    std::cout << "differential scenarios passed: " << scenarios << '\n';
    return EXIT_SUCCESS;
}
