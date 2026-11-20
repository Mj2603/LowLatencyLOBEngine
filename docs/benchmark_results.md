# Benchmark Results

All numbers on this page were measured with `benchmark_lob` from this
repository and are copied from its output. They are specific to the machine
below; expect different absolute numbers on other hardware, compilers, kernels,
power settings and load. Compare runs only within one environment.

## Machine

| Item | Value |
| --- | --- |
| CPU | Intel(R) Xeon(R) Processor @ 2.10GHz (cloud VM, 4 vCPUs, model name as reported by `/proc/cpuinfo`) |
| Memory | 15 GiB |
| OS / kernel | Ubuntu 24.04.5 LTS, Linux 6.18.44 |
| Compiler | GCC 13.3.0 |
| Build | CMake 3.28.3, `CMAKE_BUILD_TYPE=Release` (`-O3 -DNDEBUG`), C++17 |
| Isolation | none: shared virtualised host, no core pinning, no frequency control |
| Timer | `std::chrono::steady_clock`; back-to-back `now()` pair costs ~26 ns (p50) and is included in every per-event sample |

## Commands

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/benchmark_lob                                   # both workloads, 1,000,000 events, 5 runs, seed 42
./build/benchmark_lob --workload random --seed 7        # seed sensitivity check
./build/benchmark_lob --workload random --seed 1234
```

`./build/benchmark_lob` was run three times for each build.

"Before" is commit `863d298` (new benchmark, original engine that scans price
levels from the range edge). "After" is commit `dccbb10` (best-bid/ask
tracking with a level occupancy bitmap). Both use the identical benchmark source and
event streams; fill and cancel counts match exactly between the two engines
(stress: 500,000 fills; random seed 42: 596,587 fills, 199,758 successful
cancels, 1,812 orders resting at the end).

## Method

- **stress**: 1,000,000 alternating one-lot buy/sell orders at a single price
  (price range 1..1000). Every second order crosses; the book holds at most one
  order. Degenerate, kept for continuity with earlier versions.
- **random** (seeded, deterministic): 1,000,000 events over price range
  1..20000. 60% passive limit adds 1..50 ticks behind the touch midpoint,
  quantity 1..100; 20% marketable limits priced 0..30 ticks through the
  midpoint, quantity 50..400 (can sweep several levels, residual rests);
  20% cancels of a uniformly chosen resting order. The stream is generated
  once before timing; the book stays at roughly 1,500-2,000 resting orders.
- Each invocation: one warm-up run (discarded), then 5 measured runs. Each run
  replays the stream on a fresh engine twice: a throughput pass without
  per-event timing, and a latency pass with `steady_clock::now()` around every
  individual event (`submit` + `process_events` for orders, `cancel_order` for
  cancels). Percentiles pool all 5,000,000 latency samples.
- Throughput is the median of the 5 throughput passes. Everything is single
  threaded; the SPSC ring is pushed and drained on the same thread.

## Results

Each row is one invocation of `./build/benchmark_lob` (throughput = median of
5 runs; latency in nanoseconds over 5,000,000 per-event samples).

### stress workload

| Build | Run | Throughput (events/s) | p50 | p90 | p99 | p99.9 | max |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| before | 1 | 1921342 | 791 | 911 | 1046 | 5367 | 2829032 |
| before | 2 | 1978906 | 780 | 869 | 1113 | 5696 | 7835987 |
| before | 3 | 1971565 | 700 | 866 | 1019 | 5713 | 2947382 |
| after | 1 | 18943642 | 81 | 85 | 119 | 261 | 2140151 |
| after | 2 | 19380994 | 81 | 85 | 112 | 251 | 413711 |
| after | 3 | 19610776 | 78 | 84 | 112 | 260 | 2460240 |

### random workload (seed 42)

| Build | Run | Throughput (events/s) | p50 | p90 | p99 | p99.9 | max |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| before | 1 | 123001 | 8823 | 10551 | 20440 | 38814 | 25114249 |
| before | 2 | 120460 | 8836 | 10575 | 20928 | 41236 | 19954751 |
| before | 3 | 126492 | 8848 | 10593 | 21065 | 50187 | 10123642 |
| after | 1 | 10602911 | 88 | 219 | 493 | 720 | 769472 |
| after | 2 | 10349676 | 91 | 227 | 513 | 781 | 2218238 |
| after | 3 | 10780443 | 86 | 215 | 489 | 750 | 2257236 |

### random workload, other seeds (after)

| Seed | Throughput (events/s) | p50 | p90 | p99 | p99.9 | max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 7 | 10058153 | 92 | 229 | 523 | 808 | 4964491 |
| 1234 | 10010688 | 93 | 231 | 523 | 780 | 2884226 |

### Summary (ranges across the three invocations)

| Workload | Build | Throughput (events/s) | p50 (ns) | p99 (ns) | p99.9 (ns) |
| --- | --- | --- | --- | --- | --- |
| stress | before | 1.92M - 1.98M | 700 - 791 | 1,019 - 1,113 | 5,367 - 5,713 |
| stress | after | 18.9M - 19.6M | 78 - 81 | 112 - 119 | 251 - 261 |
| random | before | 120k - 126k | 8,823 - 8,848 | 20,440 - 21,065 | 38,814 - 50,187 |
| random | after | 10.3M - 10.8M | 86 - 91 | 489 - 513 | 720 - 781 |

The gain comes from removing the linear scan of empty levels from the edge of
the configured price range to the touch: with a 1..20000 range and a touch near
10,000, the old engine touched thousands of empty levels per order.

## Caveats

- Max latency (0.4 - 5.0 ms after, up to 25 ms before) is dominated by OS /
  hypervisor preemption and allocator behaviour on an unisolated VM; it is not a
  property of the matching algorithm and should not be quoted.
- Per-event latency includes the ~26 ns timer pair, the ring push/pop, and the
  heap allocation of the returned `std::vector<Fill>`.
- This is a single-threaded in-process microbenchmark: no network, feed
  handling, serialization, cross-thread hand-off, or persistence.

## Raw output (after, run 1)

```text
cpu=Intel(R) Xeon(R) Processor @ 2.10GHz
compiler=gcc 13.3.0
build_type=Release cxx_flags= -O3 -DNDEBUG
seed=42
steady_clock_overhead_ns_p50=26
note=single-threaded in-process synthetic benchmark; per-event latency includes ring submit, matching, fill-vector allocation and one steady_clock::now() pair
[workload stress]
events_per_run=1000000 measured_runs=5 price_range=1..1000
fills_per_run=500000 cancels_ok=0 cancels_rejected=0 live_orders_at_end=0
throughput_events_per_sec median=18943642 min=18433977 max=19088328
per_event_latency_ns samples=5000000 mean=76 p50=81 p90=85 p99=119 p99.9=261 max=2140151
[workload random]
events_per_run=1000000 measured_runs=5 price_range=1..20000
fills_per_run=596587 cancels_ok=199758 cancels_rejected=0 live_orders_at_end=1812
throughput_events_per_sec median=10602911 min=10240542 max=10865266
per_event_latency_ns samples=5000000 mean=120 p50=88 p90=219 p99=493 p99.9=720 max=769472
```

## Raw output (before, run 1)

```text
cpu=Intel(R) Xeon(R) Processor @ 2.10GHz
compiler=gcc 13.3.0
build_type=Release cxx_flags= -O3 -DNDEBUG
seed=42
steady_clock_overhead_ns_p50=26
note=single-threaded in-process synthetic benchmark; per-event latency includes ring submit, matching, fill-vector allocation and one steady_clock::now() pair
[workload stress]
events_per_run=1000000 measured_runs=5 price_range=1..1000
fills_per_run=500000 cancels_ok=0 cancels_rejected=0 live_orders_at_end=0
throughput_events_per_sec median=1921342 min=1891199 max=1973748
per_event_latency_ns samples=5000000 mean=540 p50=791 p90=911 p99=1046 p99.9=5367 max=2829032
[workload random]
events_per_run=1000000 measured_runs=5 price_range=1..20000
fills_per_run=596587 cancels_ok=199758 cancels_rejected=0 live_orders_at_end=1812
throughput_events_per_sec median=123001 min=122053 max=128164
per_event_latency_ns samples=5000000 mean=8054 p50=8823 p90=10551 p99=20440 p99.9=38814 max=25114249
```
