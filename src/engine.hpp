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

    std::vector<PriceLevel> bids_;
    std::vector<PriceLevel> asks_;
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

private:
    OrderBook book_;
    EventRing queue_;
};

} // namespace lob
