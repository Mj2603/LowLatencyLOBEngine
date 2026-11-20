# Low-Latency Limit Order Book Engine

A single-threaded C++17 limit order book with exact integer-tick price levels,
price-time (FIFO) matching, bitmap-based best bid/ask tracking, and a
reproducible per-event latency benchmark.

## Features

- Exact integer-tick price levels over a configured inclusive range (up to one
  million ticks), preallocated per side.
- Price-time FIFO matching: incoming limit orders sweep the opposite side from
  the touch, fill makers in arrival order at the maker's price, and rest any
  residual. Fills report maker id, taker id, quantity and price.
- Best bid / best ask cached and maintained on every add, fill and cancel. A
  two-level occupancy bitmap (one bit per level, one summary bit per 64 levels)
  finds the next non-empty level with count-leading/trailing-zero instructions,
  so matching never walks empty levels.
- `add_order` for passive-only orders (rejects anything that would cross).
- O(1) cancel and partial cancel by order id (hash map to list iterator).
- Accessors for best bid/ask, total and per-level resting volume, live order count.
- A bounded single-producer/single-consumer ring (`EventRing`) in front of the
  book. In this repository it is pushed and drained on the same thread.
- Tests: deterministic unit checks (touch maintenance, partial fills, FIFO
  priority, cancels, range edges, invalid input, ring bounds) plus a randomized
  differential test that replays 270,000 seeded operations against a simple
  `std::map`/`std::deque` reference book and requires identical fills, return
  values, touch and depth after every operation.

## Build, test, benchmark

Requires CMake 3.16+ and a C++17 compiler (GCC or Clang; MSVC intrinsics are
provided but untested).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/lob_engine
./build/benchmark_lob
./build/benchmark_lob --workload random --events 1000000 --runs 5 --seed 42
```

`benchmark_lob` options: `--workload stress|random|all` (default `all`),
`--events N` (default 1000000), `--runs R` (default 5), `--seed S` (default 42).
It prints the CPU model, compiler and build flags, then for each workload the
median throughput and per-event latency percentiles (p50/p90/p99/p99.9/max).

## Results

Measured on a shared cloud VM (Intel Xeon @ 2.10GHz, 4 vCPUs, Ubuntu 24.04,
GCC 13.3, `-O3 -DNDEBUG`, no core pinning). Ranges span three invocations of
`./build/benchmark_lob`; each invocation is 5 runs of 1,000,000 events. Latency
is per event, measured with `steady_clock` around each submit+match or cancel
(the ~26 ns timer cost is included). Full data, method and raw output:
[docs/benchmark_results.md](docs/benchmark_results.md).

| Workload | Engine | Throughput (events/s) | p50 | p99 | p99.9 |
| --- | --- | --- | --- | --- | --- |
| random (seed 42) | edge-scan (before) | 120k - 126k | 8.8 us | 20.4 - 21.1 us | 38.8 - 50.2 us |
| random (seed 42) | bitmap touch (current) | 10.3M - 10.8M | 86 - 91 ns | 489 - 513 ns | 720 - 781 ns |
| stress | edge-scan (before) | 1.92M - 1.98M | 700 - 791 ns | 1.02 - 1.11 us | 5.4 - 5.7 us |
| stress | bitmap touch (current) | 18.9M - 19.6M | 78 - 81 ns | 112 - 119 ns | 251 - 261 ns |

The **random** workload is seeded and deterministic: 60% passive limit adds
spread 1..50 ticks behind the touch midpoint, 20% marketable limits that can
sweep several levels, and 20% cancels of resting orders, over a 20,000-tick
range. The **stress** workload alternates one-lot buys and sells at a single
price and is a best case, not a realistic flow. These numbers depend heavily on
hardware and environment; rerun the benchmark on your own machine before
comparing.

## Limitations

- Single-threaded: ingestion, matching and cancels all run on one thread. The
  SPSC ring is a bounded handoff primitive but there is no multi-threaded
  producer/consumer pipeline, and the book is not thread-safe.
- Limit orders only: no market, IOC/FOK, stop, iceberg or replace/modify order
  types; no self-trade prevention, risk checks or order-id reuse policy beyond
  rejecting duplicate live ids.
- Allocation on the hot path: per-level `std::list` nodes, an
  `std::unordered_map` for order lookup, and a `std::vector<Fill>` per call.
  No custom allocator or object pool.
- Bounded, preallocated tick range (max one million ticks); prices outside the
  configured range are rejected.
- No market-data feed handler, network/session protocol, sequencing, replay,
  persistence or exchange conformance testing.
- Benchmarks are in-process synthetic microbenchmarks on an unisolated VM; max
  latency reflects OS/hypervisor noise rather than the algorithm.

## Repository layout

- `src/` - order book, occupancy bitmap, SPSC ring, matching-engine facade, sample runner
- `benchmarks/` - stress and randomized workloads with per-event latency reporting
- `tests/` - unit tests and the randomized differential test
- `docs/` - architecture notes and measured benchmark results

## License

MIT. See [LICENSE](LICENSE).
