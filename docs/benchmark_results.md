# Benchmark Method

The benchmark runs a deterministic sequence of crossing one-lot orders through the in-process queue and matching engine. It performs a warm-up, then seven measured repetitions. Each run verifies its fill count and requires the book to end flat. It reports median run throughput and p50/p95/p99 latency for batches of 256 submitted events.

The batch latency includes queue submission, matching, and execution-report allocation. It is not an individual order's latency and should not be compared with exchange or HFT system tail latency.

No performance result is checked in yet. To create a useful report, record the CPU, operating system/kernel, compiler version, full build flags, power/performance settings, and system load. Pin the process to a core, repeat on an otherwise idle machine, and compare runs on the same environment. The benchmark is a synthetic microbenchmark and does not model feed handling, cancels, realistic order-flow distributions, or end-to-end network latency.
