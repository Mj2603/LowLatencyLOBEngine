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
    : bids_{}, asks_{}, min_price_(min_price), max_price_(max_price) {
    if (max_price < min_price) {
        throw std::invalid_argument("max_price must be greater than or equal to min_price");
    }
}

size_t OrderBook::index(uint32_t price) const noexcept {
    const uint64_t range = static_cast<uint64_t>(max_price_) - min_price_ + 1;
    const uint64_t offset = static_cast<uint64_t>(price) - min_price_;
    const uint64_t level = offset * kPriceLevels / range;
    return static_cast<size_t>(std::min<uint64_t>(level, kPriceLevels - 1));
}

bool OrderBook::valid_price(uint32_t price) const noexcept {
    return price >= min_price_ && price <= max_price_;
}

bool OrderBook::add_order(const Order &order) {
    if (!valid_price(order.price) || order.quantity == 0) {
        return false;
    }
    auto &level = order.side == Side::Buy ? bids_[index(order.price)] : asks_[index(order.price)];
    uint32_t current = level.quantity.load(std::memory_order_relaxed);
    do {
        if (order.quantity > std::numeric_limits<uint32_t>::max() - current) {
            return false;
        }
    } while (!level.quantity.compare_exchange_weak(
        current, current + order.quantity, std::memory_order_relaxed));
    level.sequence.fetch_add(1, std::memory_order_release);
    return true;
}

std::optional<Fill> OrderBook::match_order(const Order &incoming) {
    if (!valid_price(incoming.price) || incoming.quantity == 0) {
        return std::nullopt;
    }
    auto &taker = incoming.side == Side::Buy ? asks_[index(incoming.price)] : bids_[index(incoming.price)];
    uint32_t available = taker.quantity.load(std::memory_order_acquire);
    while (available != 0) {
        const uint32_t traded = std::min(available, incoming.quantity);
        if (taker.quantity.compare_exchange_weak(
                available, available - traded, std::memory_order_acq_rel)) {
            taker.sequence.fetch_add(1, std::memory_order_release);
            return Fill{0, incoming.id, traded, incoming.price};
        }
    }
    return std::nullopt;
}

uint64_t OrderBook::bid_volume() const {
    uint64_t total = 0;
    for (const auto &level : bids_) {
        total += level.quantity.load(std::memory_order_acquire);
    }
    return total;
}

uint64_t OrderBook::ask_volume() const {
    uint64_t total = 0;
    for (const auto &level : asks_) {
        total += level.quantity.load(std::memory_order_acquire);
    }
    return total;
}

MatchingEngine::MatchingEngine(uint32_t min_price, uint32_t max_price)
    : book_(min_price, max_price), queue_(1 << 20) {}

bool MatchingEngine::submit(const Order &order) {
    return queue_.push(OrderEvent{order});
}

void MatchingEngine::process_events() {
    OrderEvent event;
    while (queue_.pop(event)) {
        if (!book_.match_order(event.order)) {
            book_.add_order(event.order);
        }
    }
}

uint64_t MatchingEngine::total_bid_volume() const {
    return book_.bid_volume();
}

uint64_t MatchingEngine::total_ask_volume() const {
    return book_.ask_volume();
}

} // namespace lob
