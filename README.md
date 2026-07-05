# Low-Latency Limit Order Book Engine

A compact C++ prototype for low-latency order matching and limit order book management.

This repository is designed to showcase system-level performance engineering, deterministic matching, and a clean quantitative codebase.

## Highlights

- Cache-aware fixed-price-level book representation.
- Single-producer single-consumer ring buffer for event ingress.
- Atomic price-level quantity updates for low-latency matching.
- Benchmark harness for event throughput and baseline latency measurement.

## Repository layout

- `src/` — engine implementation and sample runner.
- `benchmarks/` — throughput harness for synthetic order flow.
- `tests/` — lightweight regression tests.
- `docs/` — architecture and benchmark summary.
- `CMakeLists.txt` — build definition.

## Build

```bash
mkdir build
cd build
cmake ..
cmake --build . --parallel
```

## Run

```bash
./lob_engine
./benchmark_lob
```

The sample engine runner demonstrates basic order submission and book state reporting.

## Test

```bash
ctest --output-on-failure
```

## Continuous integration

A GitHub Actions workflow is included in `.github/workflows/ci.yml` to build the project and execute tests on every push and pull request to `main`.

## Design notes

The prototype separates event ingestion from order book state, with the goal of minimizing hot-path contention and heap allocations. The implementation is intentionally small, deterministic, and suitable for a quantitative research portfolio.

## License

This project is released under the MIT License. See `LICENSE` for details.
