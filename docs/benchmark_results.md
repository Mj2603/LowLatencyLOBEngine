# Benchmark Status

`benchmarks/benchmark_main.cpp` is a synthetic smoke workload that submits one million events and drains the event queue. It is not a calibrated performance benchmark for a realistic limit order book: the model uses aggregate price buckets and does not measure feed decoding, order-level matching, cancellation, queue position, or end-to-end latency.

The previously recorded throughput number has been removed because the repository did not preserve the machine, compiler, build flags, repetitions, warm-up, or measurement method needed to reproduce it.

## To publish performance results

A credible benchmark should record:

- CPU model, core pinning, operating system, compiler version, and complete build flags
- warm-up and repeated runs, with median and tail percentiles
- events actually matched and book state checksums
- separate queue, matching, and end-to-end timings
- a realistic workload with inserts, cancels, replacements, and trades

Until that harness exists, treat the executable as a smoke workload only.
