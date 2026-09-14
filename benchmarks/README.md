# Benchmarks

Build the optional benchmark targets and run their correctness checks:

```bash
./build.sh bench test
```

`bench` selects an isolated **Release** build at `build/benchmarks/release` and
rejects sanitizer/coverage options. `DEFAULT_BUILD_DIR` can select another root.
Direct CMake builds can use `-DLIGHTNING_BUILD_BENCHMARKS=ON`. Google Benchmark
1.9.1 is fetched only when that option is enabled; it is not a library or
installed-package dependency. `bench` builds the executables, without starting
a long measurement run. CI runs correctness smoke checks, with no timing gates.

## Component measurements

```bash
build/benchmarks/release/bin/bench_lightning \
  --benchmark_min_warmup_time=0.01 --benchmark_min_time=0.05s \
  --benchmark_repetitions=3 \
  --benchmark_out=build/benchmarks/release/components.json \
  --benchmark_out_format=json

# List or select cases using Google Benchmark's regular-expression filter:
build/benchmarks/release/bin/bench_lightning --benchmark_list_tests=true
build/benchmarks/release/bin/bench_lightning --benchmark_filter='^dispatch/routes=1000/'
```

These short settings are developer diagnostics. Use longer intervals and at least
five repetitions on an otherwise idle reference host for a baseline. Keep the raw
iteration records; the comparison tool requires at least three repetitions per
case. Google Benchmark also records median, standard deviation, and coefficient
of variation. Wall time is the primary ns/op metric; CPU time is also retained.

The 159 cases vary important axes independently instead of constructing their
full Cartesian product:

| Group | Cases | Measured operation |
| --- | --- | --- |
| Parser | Small GET; 8/32/128 extra headers; 1 KiB/64 KiB/1 MiB binary bodies; fixed/chunked framing; 1/64/4096-byte fragments | Construct owned request/parser, consume fragments, check completion, destroy request |
| Dispatch | 1/10/100/1000 routes; first/middle/last/missing; exact/parameter/wildcard; mutable/const requests | Dispatch a prebuilt request and construct/destroy the buffered response |
| Middleware/routers | 100 parameter routes, depths 0/1/4, middleware counts 0/1/5/10, mutable/const requests | Snapshot, route, decode captures, execute middleware, and buffer response |
| Response | Empty/13-byte/1 KiB/64 KiB/1 MiB bodies; 0/8/32 extra headers | Serialize an already-buffered response to an owned wire string |
| Coroutine | Immediate completion and one executor post | Restart a local event loop, spawn/dispatch/run the coroutine, and buffer response |

Fixtures, route registration, wire generation, and complete correctness checks
are outside the timed loops. Parser result/completion checks remain inside its
loop, as production code must consume those results. Mutable dispatch reuses a
request; const dispatch includes the framework's request copy. Dispatch timings
exclude HTTP parsing and serialization. Response timings exclude initial
`send()`/body construction. Coroutine timings include event-loop/spawn overhead
and are not socket latency. Byte throughput counts complete wire bytes for parser
and serializer cases, not just payload bytes.

Parser header counts exclude Host and framing headers. The 128-header fixture
raises its explicit parser limits to 256 fields/64 KiB headers; production
defaults remain unchanged. Chunked bodies use 4096-byte chunks, independently of
the parser fragment size. Payloads preserve all byte values, including nulls.
Every case validates its expected output before measuring and consumes results
through Google Benchmark optimization barriers. Validation errors and filters
matching no cases return a nonzero exit status.

`--smoke` performs one measured operation per case, allows instrumented/Debug
builds, and tags the output as correctness-only. Its ns/op values must not be
used for comparisons. Timing runs refuse non-Release or instrumented builds.

## HTTP diagnostics

```bash
python3 benchmarks/scripts/http_bench.py \
  --server build/benchmarks/release/bin/bench_http_server \
  --scenario plaintext --workers 1 --connections 16 \
  --warmup 1 --duration 5 --repetitions 3 \
  --output build/benchmarks/release/http-plaintext.json

# A fragmented, chunked binary echo workload:
python3 benchmarks/scripts/http_bench.py \
  --server build/benchmarks/release/bin/bench_http_server \
  --scenario echo --payload-bytes 65536 --chunked --fragment 4096 \
  --workers 4 --connections 16 \
  --output build/benchmarks/release/http-echo.json
```

The [machine-readable specifications](workloads/http.json) define plaintext,
fixed 1 KiB, application (100 routes, one router, five middleware), binary echo,
and coroutine-post profiles. The primary plaintext server registers exactly one
route and sends the fixed 13-byte `Hello, World!` body. Requests use HTTP/1.1 and
Host `localhost`; no automatic User-Agent or Accept-Encoding is added. POST adds
Content-Type `application/octet-stream` and either Content-Length or chunked
Transfer-Encoding. `--churn` adds `Connection: close` and measures one request per
connection, including reconnection. `--fragment` controls client writes, not TCP
packet boundaries. The default is one outstanding request per connection; no TLS,
compression, application database calls, or request logging is enabled.

Each repetition starts a fresh server on an ephemeral loopback port, waits for
JSON readiness, warms it up, and measures a fresh connection pool. Every pool
connection completes a validated preflight request before the start barrier.
Warm-up and preflight requests are excluded from measured counts and latency.
The pool runs until the duration expires, then drains in-flight requests; elapsed
time includes that drain and client teardown. Smoke mode uses one operation per
connection in each phase, avoiding timing-dependent CI assertions.

Every response is checked for HTTP version, status, unambiguous Content-Length,
complete binary body, and expected connection persistence. Wrong responses,
timeouts, connection/protocol errors, and setup failures invalidate the run.
Failures remain in the JSON report beside successful counts; invalid runs have
no headline median throughput. Connection failures end that client's phase.
Startup/control reads, socket operations, and shutdown have explicit timeouts;
the fixture is always stopped on success or failure.

Reports contain all repetitions, min/median/max successful throughput, payload
MiB/s, failure counters, sample counts, nearest-rank p50/p95/p99/max latency, and
power-of-two latency histograms. p99.9 is withheld below 100,000 successful
samples. Server CPU time includes all server threads and control/cleanup overhead;
the CPU window excludes preflight but includes connection teardown. RSS is the
process **peak** since startup, including warm-up, not current or idle RSS.

This Python client is a **closed-loop loopback diagnostic**. Its GIL, body
validation, threads, and shared CPU can limit throughput. Latency includes client
work from request construction to validated complete response. It is not corrected
for coordinated omission, has no configured offered rate, and cannot establish
the sustainable capacity or latency gates in the performance plan. Do not compare
its rates with results from wrk2 or a separate load-generator machine.

For an external load generator, start the same fixture explicitly:

```bash
build/benchmarks/release/bin/bench_http_server \
  --scenario plaintext --workers 4 --address 0.0.0.0 --port 3000
```

The foreground process accepts `stats` and `stop` on stdin; EOF also stops it.
Use a separately pinned rate-controlled generator on the reference network for
capacity curves. Dedicated-machine offered-rate sweeps, pipelining, allocation
profiling, idle-connection memory, overload/recovery, and soak orchestration remain
future work in the [performance plan](../doc/performance-plan.md).

## Provenance and comparisons

Outputs record the configured source revision and dirty-tree status, compiler,
standard, global/configuration flags, dependency pins, and timing eligibility.
Reconfigure after source changes. `compile_commands.json` in the benchmark build
records full per-target compile commands, including transitive usage requirements.
Google Benchmark records CPU/cache/load information; HTTP results record server
OS/kernel/architecture and client Python/platform information. Record allocator,
affinity, frequency policy, limits, and background load separately when establishing
a reference baseline; this harness does not configure them.

```bash
python3 benchmarks/scripts/compare.py baseline.json candidate.json \
  --output build/benchmarks/comparison.json
```

The comparator rejects mismatched hosts, CPUs, toolchains, flags, case sets,
instrumented/smoke runs, failed cases, and fewer than three raw repetitions. It
compares median real ns/op, retaining every sample. A slowdown above the default
10% threshold is flagged as a regression only when the candidate's entire sample
range exceeds the baseline's; overlapping ranges are inconclusive. This is a
conservative diagnostic, not a statistical confidence test. Comparisons are
advisory unless `--fail-on-regression` is explicitly supplied. Neither input is
modified, and output cannot overwrite an input. Interleave baseline and candidate
runs on a controlled host before accepting or rejecting a performance change.

See [local validation](../doc/benchmark-validation.md) for commands and observed
checks. Local sample artifacts are examples, not an accepted release baseline.
