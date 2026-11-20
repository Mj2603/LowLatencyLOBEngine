#include "engine.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace lob {
namespace {

// Index of the least / most significant set bit; `bits` must be non-zero.
inline unsigned lowest_bit(uint64_t bits) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long index;
    _BitScanForward64(&index, bits);
    return static_cast<unsigned>(index);
#else
    return static_cast<unsigned>(__builtin_ctzll(bits));
#endif
}

inline unsigned highest_bit(uint64_t bits) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long index;
    _BitScanReverse64(&index, bits);
    return static_cast<unsigned>(index);
#else
    return 63U - static_cast<unsigned>(__builtin_clzll(bits));
#endif
}

// Mask with bits [0, bit] set.
inline uint64_t mask_up_to(unsigned bit) noexcept {
    return bit >= 63 ? ~uint64_t{0} : ((uint64_t{1} << (bit + 1)) - 1);
}

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

LevelBitmap::LevelBitmap(size_t size)
    : size_(size),
      words_((size + 63) / 64, 0),
      summary_((words_.size() + 63) / 64, 0) {}

void LevelBitmap::set(size_t index) noexcept {
    const size_t word = index >> 6;
    words_[word] |= uint64_t{1} << (index & 63);
    summary_[word >> 6] |= uint64_t{1} << (word & 63);
}

void LevelBitmap::clear(size_t index) noexcept {
    const size_t word = index >> 6;
    words_[word] &= ~(uint64_t{1} << (index & 63));
    if (words_[word] == 0) {
        summary_[word >> 6] &= ~(uint64_t{1} << (word & 63));
    }
}

bool LevelBitmap::test(size_t index) const noexcept {
    return index < size_ && ((words_[index >> 6] >> (index & 63)) & 1U) != 0;
}

size_t LevelBitmap::find_next(size_t from) const noexcept {
    if (from >= size_) {
        return npos;
    }
    size_t word = from >> 6;
    const uint64_t bits = words_[word] & (~uint64_t{0} << (from & 63));
    if (bits != 0) {
        return (word << 6) + lowest_bit(bits);
    }
    ++word;
    if (word >= words_.size()) {
        return npos;
    }
    size_t group = word >> 6;
    uint64_t summary = summary_[group] & (~uint64_t{0} << (word & 63));
    for (;;) {
        if (summary != 0) {
            const size_t found = (group << 6) + lowest_bit(summary);
            return (found << 6) + lowest_bit(words_[found]);
        }
        if (++group >= summary_.size()) {
            return npos;
        }
        summary = summary_[group];
    }
}

size_t LevelBitmap::find_prev(size_t from) const noexcept {
    if (size_ == 0) {
        return npos;
    }
    if (from >= size_) {
        from = size_ - 1;
    }
    size_t word = from >> 6;
    const uint64_t bits = words_[word] & mask_up_to(static_cast<unsigned>(from & 63));
    if (bits != 0) {
        return (word << 6) + highest_bit(bits);
    }
    if (word == 0) {
        return npos;
    }
    --word;
    size_t group = word >> 6;
    uint64_t summary = summary_[group] & mask_up_to(static_cast<unsigned>(word & 63));
    for (;;) {
        if (summary != 0) {
            const size_t found = (group << 6) + highest_bit(summary);
            return (found << 6) + highest_bit(words_[found]);
        }
        if (group == 0) {
            return npos;
        }
        summary = summary_[--group];
    }
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
    bid_occupancy_ = LevelBitmap(static_cast<size_t>(level_count));
    ask_occupancy_ = LevelBitmap(static_cast<size_t>(level_count));
}

size_t OrderBook::index(uint32_t price) const noexcept {
    return static_cast<size_t>(static_cast<uint64_t>(price) - min_price_);
}

bool OrderBook::valid_price(uint32_t price) const noexcept {
    return price >= min_price_ && price <= max_price_;
}

bool OrderBook::rest_order(const Order &order) {
    if (order.id == 0 || !valid_price(order.price) || order.quantity == 0 ||
        (order.side != Side::Buy && order.side != Side::Sell) ||
        orders_.find(order.id) != orders_.end()) {
        return false;
    }

    const size_t level_index = index(order.price);
    auto &levels = order.side == Side::Buy ? bids_ : asks_;
    auto &level = levels[level_index];
    const bool was_empty = level.fifo.empty();
    level.fifo.push_back(order);
    auto iterator = std::prev(level.fifo.end());
    orders_.emplace(order.id, OrderLocation{order.side, order.price, iterator});
    level.volume += order.quantity;
    if (order.side == Side::Buy) {
        bid_volume_ += order.quantity;
        if (was_empty) {
            bid_occupancy_.set(level_index);
            if (best_bid_index_ == LevelBitmap::npos || level_index > best_bid_index_) {
                best_bid_index_ = level_index;
            }
        }
    } else {
        ask_volume_ += order.quantity;
        if (was_empty) {
            ask_occupancy_.set(level_index);
            if (best_ask_index_ == LevelBitmap::npos || level_index < best_ask_index_) {
                best_ask_index_ = level_index;
            }
        }
    }
    return true;
}

// Called after the last order at a level is removed. Clears the occupancy bit
// and, if that level was the touch, moves the touch to the next non-empty level.
void OrderBook::level_emptied(Side side, size_t level_index) noexcept {
    if (side == Side::Buy) {
        bid_occupancy_.clear(level_index);
        if (level_index == best_bid_index_) {
            best_bid_index_ = level_index == 0 ? LevelBitmap::npos
                                               : bid_occupancy_.find_prev(level_index - 1);
        }
    } else {
        ask_occupancy_.clear(level_index);
        if (level_index == best_ask_index_) {
            best_ask_index_ = ask_occupancy_.find_next(level_index + 1);
        }
    }
}

bool OrderBook::add_order(const Order &order) {
    if (!valid_price(order.price) ||
        (order.side != Side::Buy && order.side != Side::Sell)) {
        return false;
    }
    // add_order only rests passive orders: reject anything that would cross.
    const size_t level_index = index(order.price);
    if (order.side == Side::Buy) {
        if (best_ask_index_ != LevelBitmap::npos && best_ask_index_ <= level_index) {
            return false;
        }
    } else {
        if (best_bid_index_ != LevelBitmap::npos && best_bid_index_ >= level_index) {
            return false;
        }
    }
    return rest_order(order);
}

std::vector<Fill> OrderBook::match_order(const Order &incoming) {
    std::vector<Fill> fills;
    if (incoming.id == 0 || !valid_price(incoming.price) || incoming.quantity == 0 ||
        (incoming.side != Side::Buy && incoming.side != Side::Sell) ||
        orders_.find(incoming.id) != orders_.end()) {
        return fills;
    }

    uint32_t remaining = incoming.quantity;
    const size_t limit_index = index(incoming.price);
    const bool is_buy = incoming.side == Side::Buy;
    auto &opposite = is_buy ? asks_ : bids_;
    size_t &best = is_buy ? best_ask_index_ : best_bid_index_;
    uint64_t &opposite_volume = is_buy ? ask_volume_ : bid_volume_;
    const Side maker_side = is_buy ? Side::Sell : Side::Buy;

    // Walk the opposite side from the touch, visiting only non-empty levels.
    while (remaining > 0 && best != LevelBitmap::npos &&
           (is_buy ? best <= limit_index : best >= limit_index)) {
        const size_t level_index = best;
        auto &level = opposite[level_index];
        while (remaining > 0 && !level.fifo.empty()) {
            Order &maker = level.fifo.front();
            const uint32_t traded = std::min(remaining, maker.quantity);
            fills.push_back(Fill{maker.id, incoming.id, traded, maker.price});
            maker.quantity -= traded;
            remaining -= traded;
            level.volume -= traded;
            opposite_volume -= traded;
            if (maker.quantity == 0) {
                orders_.erase(maker.id);
                level.fifo.pop_front();
            }
        }
        if (!level.fifo.empty()) {
            break; // taker exhausted; touch unchanged
        }
        level_emptied(maker_side, level_index); // advances `best`
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
        if (level.fifo.empty()) {
            level_emptied(location.side, index(location.price));
        }
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

std::optional<uint32_t> OrderBook::best_bid() const noexcept {
    if (best_bid_index_ == LevelBitmap::npos) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(min_price_ + best_bid_index_);
}

std::optional<uint32_t> OrderBook::best_ask() const noexcept {
    if (best_ask_index_ == LevelBitmap::npos) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(min_price_ + best_ask_index_);
}

uint64_t OrderBook::volume_at(Side side, uint32_t price) const noexcept {
    if (!valid_price(price)) {
        return 0;
    }
    return (side == Side::Buy ? bids_ : asks_)[index(price)].volume;
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

std::optional<uint32_t> MatchingEngine::best_bid() const noexcept {
    return book_.best_bid();
}

std::optional<uint32_t> MatchingEngine::best_ask() const noexcept {
    return book_.best_ask();
}

} // namespace lob
