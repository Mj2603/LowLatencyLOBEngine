# Architecture

The prototype has a single-consumer matching path and a bounded SPSC event ingress queue.

## Components

- `src/engine.hpp` — order and fill types, ingress queue, level occupancy bitmap, FIFO book, matching-engine façade.
- `src/engine.cpp` — queue operations, bitmap search, exact-tick levels, best bid/ask maintenance, price-time matching, and cancels.
- `benchmarks/benchmark_main.cpp` — stress and seeded randomized workloads with throughput and per-event latency reporting.
- `tests/engine_test.cpp` — deterministic behavior checks.
- `tests/differential_test.cpp` — randomized comparison against a `std::map` reference book.

## Matching flow

1. A producer submits an order to `EventRing`.
2. The owner thread drains events in FIFO submission order.
3. A buy order sweeps asks from the best (lowest) ask upward until its limit or quantity is exhausted; a sell sweeps bids from the best (highest) bid downward. Only non-empty levels are visited.
4. Resting orders at each exact tick are consumed in FIFO order.
5. Any unfilled limit quantity rests at the back of its price queue.
6. Fills identify maker, taker, quantity, and execution price.

## Best bid/ask tracking

Each side keeps a `LevelBitmap`: one bit per price level plus one summary bit per 64-level word. The book caches the best bid and best ask level index. Resting an order into an empty level sets its bit and raises the touch if needed. When a fill or cancel empties a level its bit is cleared; if it was the touch, the next non-empty level is found by masking the current word and, if that is empty, scanning summary words (one per 4096 levels) with ctz/clz. With the one-million-tick maximum this is at most 245 summary words, and in practice usually a single word.

## Ownership and thread safety

The SPSC queue publishes completed events with release/acquire ordering. Only its single consumer may mutate the order book. Direct cancellation is owner-thread-only. The book uses lists and a hash map for clarity and correct FIFO/cancel semantics; this is not yet a cache-optimized production data structure.

## Boundaries

Prices are integer ticks in the configured inclusive range, limited to one million levels to bound memory use. Market orders, replace semantics, exchange protocol details, recovery/replay, and cross-thread cancellation are not implemented.
