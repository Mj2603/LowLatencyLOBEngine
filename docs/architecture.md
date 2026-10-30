# Architecture

The prototype has a single-consumer matching path and a bounded SPSC event ingress queue.

## Components

- `src/engine.hpp` — order and fill types, ingress queue, FIFO book, matching-engine façade.
- `src/engine.cpp` — queue operations, exact-tick levels, price-time matching, and cancels.
- `benchmarks/benchmark_main.cpp` — repeated synthetic cross workload and batch-latency reporting.
- `tests/engine_test.cpp` — deterministic behavior checks.

## Matching flow

1. A producer submits an order to `EventRing`.
2. The owner thread drains events in FIFO submission order.
3. A buy order sweeps asks from lowest price upward until its limit or quantity is exhausted; a sell sweeps bids from highest price downward.
4. Resting orders at each exact tick are consumed in FIFO order.
5. Any unfilled limit quantity rests at the back of its price queue.
6. Fills identify maker, taker, quantity, and execution price.

## Ownership and thread safety

The SPSC queue publishes completed events with release/acquire ordering. Only its single consumer may mutate the order book. Direct cancellation is owner-thread-only. The book uses lists and a hash map for clarity and correct FIFO/cancel semantics; this is not yet a cache-optimized production data structure.

## Boundaries

Prices are integer ticks in the configured inclusive range, limited to one million levels to bound memory use. Market orders, replace semantics, exchange protocol details, recovery/replay, and cross-thread cancellation are not implemented.
