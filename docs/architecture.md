# Architecture

The engine is designed for deterministic execution with an emphasis on low-latency throughput.

## Components

- `src/engine.hpp` — API for orders, fills, matching engine, and ring buffer.
- `src/engine.cpp` — implementation of stateful matching, price-level aggregation, and event queue.
- `benchmarks/benchmark_main.cpp` — synthetic workload generator for throughput measurement.
- `tests/engine_test.cpp` — regression validation for matching semantics.

## Data flow

1. Order events are submitted to `MatchingEngine`.
2. `EventRing` buffers events in a preallocated circular queue.
3. `OrderBook` updates price-level quantities using atomic counters.
4. Incoming orders attempt to match against the opposite side before being added to the book.

## Design choices

- Preallocated arrays avoid heap pressure in the core path.
- Atomic counters provide thread-safe quantity updates while preserving data locality.
- The design assumes conservative matching at aggregate price levels rather than full order-by-order depth.
