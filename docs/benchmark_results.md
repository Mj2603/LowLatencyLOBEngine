# Benchmark Results

The benchmark harness evaluates order ingestion and processing throughput for a synthetic event stream.

## Benchmark configuration

- Event count: 1,000,000
- Price range: 100–109
- Quantity per order: 1
- Single-threaded processing path

## Measured throughput

- Total event processing time: `0.0168` seconds
- Effective throughput: `59.5M events/sec`

## Interpretation

The benchmark shows the prototype can sustain tens of millions of synthetic order events per second on a single core. It validates the value of a compact engine design with preallocated event storage and atomic price-level updates.

## Future improvements

- expand book depth and price-level resolution
- add multi-threaded matching and execution pipelines
- support full order-level FIFO matching semantics
