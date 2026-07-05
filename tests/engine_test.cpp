#include "engine.hpp"
#include <cassert>

int main() {
    lob::OrderBook book;
    const lob::Order buy{1, lob::Side::Buy, 100, 10};
    const lob::Order sell{2, lob::Side::Sell, 100, 5};

    assert(book.add_order(buy));
    auto fill = book.match_order(sell);
    assert(fill.has_value());
    assert(fill->quantity == 5);
    assert(book.bid_volume() == 5);
    assert(book.ask_volume() == 0);

    assert(book.add_order(sell));
    assert(book.match_order(buy).has_value());
    return 0;
}
