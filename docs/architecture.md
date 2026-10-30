# Architecture

The prototype has a bounded event queue feeding a fixed-size, aggregate price-bucket book.

## Components

- `src/engine.hpp` — order/event types, ring-buffer API, aggregate book, and engine façade.
- `src/engine.cpp` — queue, price-bucket quantity updates, and basic matching path.
- `benchmarks/benchmark_main.cpp` — synthetic queue-and-processing smoke workload.
- `tests/engine_test.cpp` — regression checks for the current prototype.

## Data flow

1. The producer submits events to `EventRing`.
2. A consumer drains the preallocated queue.
3. `OrderBook` aggregates quantity into one of 1,024 buckets per side.

## Current model boundaries

The book does not preserve individual orders or FIFO priority. Prices are mapped into buckets, so distinct prices can share a level; the current matching API therefore cannot provide exact exchange matching semantics. The queue is designed for a single producer and a single consumer. Atomic bucket counters do not make the full engine a multi-producer or multi-consumer system.

The code is useful for exploring bounded queues and fixed-array data structures. It needs an order-level book, explicit lifecycle semantics, concurrency design, and reproducible performance evidence before it can support production or HFT claims.
