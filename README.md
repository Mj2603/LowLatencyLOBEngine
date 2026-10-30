# Low-Latency Limit Order Book Engine

A compact C++17 prototype for bounded event ingestion and aggregate price-level matching experiments.

## Current scope

- A bounded single-producer/single-consumer ring buffer with preallocated storage.
- A fixed-size array of aggregate price levels.
- Basic order submission, quantity aggregation, and a synthetic benchmark executable.
- CMake build, regression tests, and GitHub Actions CI.

This is a systems-learning and research prototype. It is **not** a production matching engine or an exchange-grade limit order book. It does not currently implement order-level FIFO priority, cancellation and replace semantics, exact tick-by-tick price levels, market-by-order depth, a production feed handler, or a validated multi-core execution pipeline. The order-book model aggregates quantities into price buckets, so it must not be used to infer realistic fills or market impact.

## Build

Requires CMake 3.16+ and a C++17 compiler.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Run

```bash
./build/lob_engine
./build/benchmark_lob
```

The benchmark is a synthetic smoke workload. Results depend on compiler, hardware, build flags, and system load; the repository does not claim a portable throughput or tail-latency figure.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

## Repository layout

- `src/` — ring buffer, aggregate book model, and sample runner
- `benchmarks/` — synthetic workload driver
- `tests/` — regression tests
- `docs/` — design and model limitations

## License

MIT. See [LICENSE](LICENSE).
