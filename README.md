# Low-Latency Limit Order Book Engine

A C++17 prototype for exact-tick price-time matching, bounded event ingestion, and repeatable synthetic benchmarks.

## Implemented

- Exact integer-tick levels over a configured bounded price range.
- Price-time FIFO matching with partial fills and maker/taker execution reports.
- Partial and full cancellation by order ID.
- A bounded single-producer/single-consumer ingress ring with preallocated storage.
- Regression coverage for matching priority, partial fills, cancellation, invalid input, and queue bounds.
- A repeated synthetic benchmark with correctness checks and batch-latency percentiles.

## Build

Requires CMake 3.16+ and a C++17 compiler.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Run

```bash
./build/lob_engine
./build/benchmark_lob
./build/benchmark_lob 200000
```

The benchmark includes queue submission, matching, and fill-report allocation. It is an in-process synthetic workload, not an exchange-grade latency result. Record the machine, compiler, build flags, kernel, and load before comparing results. Batch percentiles are not per-order tail latency.

## Concurrency contract

The ring is single-producer/single-consumer. The consumer owns all book mutations. The book itself is not thread-safe, and cancellation must run on the same consumer/owner thread. Atomic queue indices do not make the order book concurrently mutable.

## Limitations before production use

This repository remains a research prototype. It does not include a market-data feed handler, protocol/session recovery, sequencing and replay, exchange-specific order semantics, self-trade prevention, risk controls, persistence, or a multi-core matching architecture. It has not been independently performance-audited or validated against exchange conformance cases.

## Repository layout

- `src/` — queue, matching engine, and sample runner
- `benchmarks/` — repeatable synthetic workload driver
- `tests/` — deterministic regression checks
- `docs/` — architecture and benchmark methodology

## License

MIT. See [LICENSE](LICENSE).
