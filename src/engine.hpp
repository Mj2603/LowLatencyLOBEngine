#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lob {

enum class Side : uint8_t { Buy, Sell };

struct Order {
    uint64_t id;
    Side side;
    uint32_t price;
    uint32_t quantity;
};

struct Fill {
    uint64_t maker_id;
    uint64_t taker_id;
    uint32_t quantity;
    uint32_t price;
};

struct OrderEvent {
    Order order;
};

// Bounded single-producer/single-consumer queue. The consumer must be the
// sole owner of OrderBook mutations.
class EventRing {
public:
    explicit EventRing(size_t capacity = 1 << 20);
    bool push(const OrderEvent &event);
    bool pop(OrderEvent &event);
    size_t size() const;

private:
    const size_t capacity_;
    const size_t mask_;
    std::vector<OrderEvent> buffer_;
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
};

// Two-level occupancy bitmap over price-level indices. Finding the next or
// previous non-empty level costs at most a scan of one 64-bit word plus a scan
// of the summary words (one summary bit per 64 levels), using count-zero
// intrinsics, instead of touching every empty level.
class LevelBitmap {
public:
    static constexpr size_t npos = static_cast<size_t>(-1);

    explicit LevelBitmap(size_t size = 0);
    void set(size_t index) noexcept;
    void clear(size_t index) noexcept;
    bool test(size_t index) const noexcept;
    // Lowest set index >= from, or npos.
    size_t find_next(size_t from) const noexcept;
    // Highest set index <= from, or npos.
    size_t find_prev(size_t from) const noexcept;

private:
    size_t size_;
    std::vector<uint64_t> words_;
    std::vector<uint64_t> summary_;
};

// Single-threaded exact-tick price-time FIFO book. Prices are integer ticks in
// the inclusive configured range. The book itself is not thread-safe.
class OrderBook {
public:
    OrderBook(uint32_t min_price = 1, uint32_t max_price = 10000);
    bool add_order(const Order &order);
    std::vector<Fill> match_order(const Order &incoming);
    bool cancel_order(uint64_t order_id, uint32_t quantity = 0);
    uint64_t bid_volume() const noexcept;
    uint64_t ask_volume() const noexcept;
    size_t live_order_count() const noexcept;
    // Best (highest) resting bid / best (lowest) resting ask, if any.
    std::optional<uint32_t> best_bid() const noexcept;
    std::optional<uint32_t> best_ask() const noexcept;
    // Total resting quantity at an exact price on one side (0 if out of range).
    uint64_t volume_at(Side side, uint32_t price) const noexcept;

private:
    struct PriceLevel {
        std::list<Order> fifo;
        uint64_t volume{0};
    };
    struct OrderLocation {
        Side side;
        uint32_t price;
        std::list<Order>::iterator iterator;
    };

    size_t index(uint32_t price) const noexcept;
    bool valid_price(uint32_t price) const noexcept;
    bool rest_order(const Order &order);
    void level_emptied(Side side, size_t level_index) noexcept;

    std::vector<PriceLevel> bids_;
    std::vector<PriceLevel> asks_;
    LevelBitmap bid_occupancy_;
    LevelBitmap ask_occupancy_;
    // Level indices of the current best bid/ask; LevelBitmap::npos when empty.
    size_t best_bid_index_{LevelBitmap::npos};
    size_t best_ask_index_{LevelBitmap::npos};
    std::unordered_map<uint64_t, OrderLocation> orders_;
    const uint32_t min_price_;
    const uint32_t max_price_;
    uint64_t bid_volume_{0};
    uint64_t ask_volume_{0};
};

class MatchingEngine {
public:
    explicit MatchingEngine(uint32_t min_price = 1, uint32_t max_price = 10000);
    bool submit(const Order &order);
    std::vector<Fill> process_events();
    uint64_t total_bid_volume() const noexcept;
    uint64_t total_ask_volume() const noexcept;
    size_t live_order_count() const noexcept;
    bool cancel_order(uint64_t order_id, uint32_t quantity = 0);
    std::optional<uint32_t> best_bid() const noexcept;
    std::optional<uint32_t> best_ask() const noexcept;

private:
    OrderBook book_;
    EventRing queue_;
};

} // namespace lob
