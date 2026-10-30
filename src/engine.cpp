#include "engine.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace lob {
namespace {

size_t rounded_capacity(size_t requested) {
    if (requested < 2) {
        throw std::invalid_argument("EventRing capacity must be at least 2");
    }

    size_t result = 1;
    while (result < requested) {
        if (result > std::numeric_limits<size_t>::max() / 2) {
            throw std::length_error("EventRing capacity is too large");
        }
        result *= 2;
    }
    return result;
}

} // namespace

EventRing::EventRing(size_t capacity)
    : capacity_(rounded_capacity(capacity)),
      mask_(capacity_ - 1),
      buffer_(capacity_),
      head_(0),
      tail_(0) {}

bool EventRing::push(const OrderEvent &event) {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t next = tail + 1;
    if (next - head_.load(std::memory_order_acquire) > capacity_) {
        return false;
    }
    buffer_[tail & mask_] = event;
    tail_.store(next, std::memory_order_release);
    return true;
}

bool EventRing::pop(OrderEvent &event) {
    const size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) {
        return false;
    }
    event = buffer_[head & mask_];
    head_.store(head + 1, std::memory_order_release);
    return true;
}

size_t EventRing::size() const {
    return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
}

OrderBook::OrderBook(uint32_t min_price, uint32_t max_price)
    : min_price_(min_price), max_price_(max_price) {
    if (max_price < min_price) {
        throw std::invalid_argument("max_price must be greater than or equal to min_price");
    }
    const uint64_t level_count = static_cast<uint64_t>(max_price) - min_price + 1;
    if (level_count > 1'000'000) {
        throw std::invalid_argument("configured price range exceeds the one-million-tick limit");
    }
    bids_.resize(static_cast<size_t>(level_count));
    asks_.resize(static_cast<size_t>(level_count));
}

size_t OrderBook::index(uint32_t price) const noexcept {
    return static_cast<size_t>(static_cast<uint64_t>(price) - min_price_);
}

bool OrderBook::valid_price(uint32_t price) const noexcept {
    return price >= min_price_ && price <= max_price_;
}

bool OrderBook::rest_order(const Order &order) {
    if (!valid_price(order.price) || order.quantity == 0 ||
        (order.side != Side::Buy && order.side != Side::Sell) ||
        orders_.find(order.id) != orders_.end()) {
        return false;
    }

    auto &levels = order.side == Side::Buy ? bids_ : asks_;
    auto &level = levels[index(order.price)];
    level.fifo.push_back(order);
    auto iterator = std::prev(level.fifo.end());
    orders_.emplace(order.id, OrderLocation{order.side, order.price, iterator});
    level.volume += order.quantity;
    if (order.side == Side::Buy) {
        bid_volume_ += order.quantity;
    } else {
        ask_volume_ += order.quantity;
    }
    return true;
}

bool OrderBook::add_order(const Order &order) {
    return rest_order(order);
}

std::vector<Fill> OrderBook::match_order(const Order &incoming) {
    std::vector<Fill> fills;
    if (!valid_price(incoming.price) || incoming.quantity == 0 ||
        (incoming.side != Side::Buy && incoming.side != Side::Sell) ||
        orders_.find(incoming.id) != orders_.end()) {
        return fills;
    }

    uint32_t remaining = incoming.quantity;
    if (incoming.side == Side::Buy) {
        for (uint64_t price = min_price_; price <= incoming.price && remaining > 0; ++price) {
            auto &level = asks_[index(static_cast<uint32_t>(price))];
            while (remaining > 0 && !level.fifo.empty()) {
                Order &maker = level.fifo.front();
                const uint32_t traded = std::min(remaining, maker.quantity);
                fills.push_back(Fill{maker.id, incoming.id, traded, maker.price});
                maker.quantity -= traded;
                remaining -= traded;
                level.volume -= traded;
                ask_volume_ -= traded;
                if (maker.quantity == 0) {
                    orders_.erase(maker.id);
                    level.fifo.pop_front();
                }
            }
        }
    } else {
        uint64_t price = max_price_;
        for (;;) {
            if (price < incoming.price || remaining == 0) {
                break;
            }
            auto &level = bids_[index(static_cast<uint32_t>(price))];
            while (remaining > 0 && !level.fifo.empty()) {
                Order &maker = level.fifo.front();
                const uint32_t traded = std::min(remaining, maker.quantity);
                fills.push_back(Fill{maker.id, incoming.id, traded, maker.price});
                maker.quantity -= traded;
                remaining -= traded;
                level.volume -= traded;
                bid_volume_ -= traded;
                if (maker.quantity == 0) {
                    orders_.erase(maker.id);
                    level.fifo.pop_front();
                }
            }
            if (price == incoming.price) {
                break;
            }
            --price;
        }
    }

    if (remaining > 0) {
        Order resting = incoming;
        resting.quantity = remaining;
        // A fully or partially filled order keeps its ID; it has not rested
        // before this point because duplicate IDs were rejected above.
        rest_order(resting);
    }
    return fills;
}

bool OrderBook::cancel_order(uint64_t order_id, uint32_t quantity) {
    const auto found = orders_.find(order_id);
    if (found == orders_.end()) {
        return false;
    }

    const OrderLocation location = found->second;
    auto &levels = location.side == Side::Buy ? bids_ : asks_;
    auto &level = levels[index(location.price)];
    Order &order = *location.iterator;
    const uint32_t cancel_quantity = quantity == 0 ? order.quantity : quantity;
    if (cancel_quantity == 0 || cancel_quantity > order.quantity) {
        return false;
    }

    order.quantity -= cancel_quantity;
    level.volume -= cancel_quantity;
    if (location.side == Side::Buy) {
        bid_volume_ -= cancel_quantity;
    } else {
        ask_volume_ -= cancel_quantity;
    }
    if (order.quantity == 0) {
        level.fifo.erase(location.iterator);
        orders_.erase(found);
    }
    return true;
}

uint64_t OrderBook::bid_volume() const noexcept {
    return bid_volume_;
}

uint64_t OrderBook::ask_volume() const noexcept {
    return ask_volume_;
}

size_t OrderBook::live_order_count() const noexcept {
    return orders_.size();
}

MatchingEngine::MatchingEngine(uint32_t min_price, uint32_t max_price)
    : book_(min_price, max_price), queue_(1 << 20) {}

bool MatchingEngine::submit(const Order &order) {
    return queue_.push(OrderEvent{order});
}

std::vector<Fill> MatchingEngine::process_events() {
    std::vector<Fill> fills;
    OrderEvent event;
    while (queue_.pop(event)) {
        auto event_fills = book_.match_order(event.order);
        fills.insert(fills.end(), event_fills.begin(), event_fills.end());
    }
    return fills;
}

uint64_t MatchingEngine::total_bid_volume() const noexcept {
    return book_.bid_volume();
}

uint64_t MatchingEngine::total_ask_volume() const noexcept {
    return book_.ask_volume();
}

size_t MatchingEngine::live_order_count() const noexcept {
    return book_.live_order_count();
}

bool MatchingEngine::cancel_order(uint64_t order_id, uint32_t quantity) {
    return book_.cancel_order(order_id, quantity);
}

} // namespace lob
