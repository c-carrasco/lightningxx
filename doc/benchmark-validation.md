# Benchmark validation

Validated locally on 2026-09-13 using macOS arm64, AppleClang
21.0.0.21000101, C++20, and Python 3.11. The working tree included uncommitted
framework and benchmark changes; reports record this explicitly. These short
local runs validate the harness and report format, not a release performance
baseline. No dedicated-machine capacity or latency target has been certified.

## Correctness checks

```bash
./build.sh bench test

# In the existing Debug ASan+UBSan configuration:
cmake -S . -B build/debug -DLIGHTNING_BUILD_BENCHMARKS=ON
cmake --build build/debug --parallel 4
ctest --test-dir build/debug -L benchmark --output-on-failure
```

Release passed all four CTest entries: the existing 272 GoogleTests, installed
consumer validation, 159 component benchmark smoke cases, and nine Python
harness tests. Both benchmark CTest entries also passed under AddressSanitizer
and UndefinedBehaviorSanitizer. The Debug component executable rejected a
measurement invocation without `--smoke` with exit status 2.

The harness tests exercise all five HTTP profiles, binary bodies through 1 MiB,
chunking, fragmented writes, connection churn, failure validation, latency
statistics, and comparison acceptance/rejection. Smoke tests use fixed operation
counts and do not assert performance thresholds. CI now includes these checks
for the configured Linux and macOS jobs; hosted CI was not executed locally.

Logs are local ignored artifacts:

- `build/benchmark-tests.log`
- `build/benchmark-sanitizer-configure.log`
- `build/benchmark-sanitizer-build.log`
- `build/benchmark-sanitizer-tests.log`

## Sample measurements

Measured workloads ran sequentially, without concurrent builds or tests.

```bash
build/benchmarks/release/bin/bench_lightning \
  --benchmark_min_warmup_time=0.01 --benchmark_min_time=0.05s \
  --benchmark_repetitions=3 \
  --benchmark_out=build/benchmarks/release/components-local.json \
  --benchmark_out_format=json

python3 benchmarks/scripts/http_bench.py \
  --server build/benchmarks/release/bin/bench_http_server \
  --scenario plaintext --workers 1 --connections 4 \
  --warmup 0.1 --duration 0.5 --repetitions 3 \
  --output build/benchmarks/release/http-plaintext-local.json

python3 benchmarks/scripts/http_bench.py \
  --server build/benchmarks/release/bin/bench_http_server \
  --scenario echo --workers 2 --connections 4 \
  --payload-bytes 65536 --chunked --fragment 4096 \
  --warmup 0.1 --duration 0.5 --repetitions 3 \
  --output build/benchmarks/release/http-echo-local.json

# Check that actual Google Benchmark output is accepted by the comparator.
python3 benchmarks/scripts/compare.py \
  build/benchmarks/release/components-local.json \
  build/benchmarks/release/components-local.json \
  --output build/benchmarks/release/comparison-self-check.json
```

All 159 component cases completed three repetitions (477 raw samples), with no
correctness errors. Both HTTP profiles produced three valid repetitions with
zero response, timeout, connection, or setup errors. Plaintext repetitions
validated 14,661 / 15,380 / 15,608 measured responses; binary echo repetitions
validated 2,811 / 2,833 / 2,755. These counts confirm that the measured paths ran;
they are not capacity claims. Self-comparison accepted all 159 cases and reported
zero regressions; this checks format compatibility, not a before/after change.

The JSON files retain timing samples or histograms, build provenance, workload
parameters, and failure counts. HTTP reports also include server CPU and peak
RSS. They are generated under ignored `build/`, not checked-in baselines.

The Python HTTP client uses closed-loop loopback traffic with one outstanding
request per connection. Client overhead and shared CPU affect its measurements;
there is no offered-rate control or coordinated-omission correction. Longer,
interleaved runs on controlled hosts are needed for performance comparisons.
See the [benchmark guide](../benchmarks/README.md) for measurement boundaries and
the [performance plan](performance-plan.md) for remaining capacity, allocation,
memory, overload/recovery, and soak work.
