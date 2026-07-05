#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <optional>
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
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

class OrderBook {
public:
    OrderBook(uint32_t min_price = 1, uint32_t max_price = 10000);
    bool add_order(const Order &order);
    std::optional<Fill> match_order(const Order &incoming);
    uint64_t bid_volume() const;
    uint64_t ask_volume() const;

private:
    static constexpr size_t kPriceLevels = 1024;
    struct PriceLevel {
        std::atomic<uint32_t> quantity{0};
        std::atomic<uint64_t> sequence{0};
    };

    uint32_t index(uint32_t price) const noexcept;
    bool valid_price(uint32_t price) const noexcept;

    std::array<PriceLevel, kPriceLevels> bids_;
    std::array<PriceLevel, kPriceLevels> asks_;
    const uint32_t min_price_;
    const uint32_t max_price_;
};

class MatchingEngine {
public:
    explicit MatchingEngine(uint32_t min_price = 1, uint32_t max_price = 10000);
    bool submit(const Order &order);
    void process_events();
    uint64_t total_bid_volume() const;
    uint64_t total_ask_volume() const;

private:
    OrderBook book_;
    EventRing queue_;
};

} // namespace lob
