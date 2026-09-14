# Performance measurement plan

Status: the optional component and HTTP diagnostic harness is implemented; see
[benchmark usage](../benchmarks/README.md) and [local validation](benchmark-validation.md).
A dedicated reference-machine capacity baseline has not been established.
All numeric targets below remain initial engineering
goals, not published results or universal definitions of a fast HTTP framework.

The objective is to deliver high **sustained successful throughput**, predictable
tail latency, low CPU and memory cost, and useful scaling as workers are added.
Correct responses are a prerequisite: a faster run that loses requests or sends
incorrect responses is a failure.

## Reference environment and measurement rules

- Establish a dedicated Linux reference machine with at least four physical CPU
  cores and 16 GiB RAM. Record the exact CPU, OS/kernel, compiler, allocator,
  dependency versions, CPU affinity, frequency policy, and resource limits.
- Use a separate load-generator machine on a network of at least 10 Gbit/s, with
  measured idle RTT below 0.2 ms. Confirm the client and network have spare
  capacity. Loopback runs on developer machines are useful for investigation,
  but have a separate baseline and no release performance gate.
- Build Release, with ASan, UBSan, TSan, and coverage disabled. Verify the actual
  compile and link flags; use a separate build directory. Keep sanitizer tests
  as independent correctness jobs. Disable request logging for the primary
  profile and measure logging overhead separately.
- Start with HTTP/1.1, keep-alive, no TLS, no compression, and one outstanding
  request per connection. Report pipelining separately. TLS, database access,
  and application work require their own future profiles.
- Sweep 1, 2, and 4 workers; 1, 16, 64, 256, and 1,024 connections; and offered
  request rates. Use at least 30 seconds of warm-up and 60 seconds of measurement,
  with five independent runs. Restart the server between independent runs.
  Confirm candidate capacity with a five-minute run; use a 30-minute soak test.
- Compare the candidate and baseline commits on the same machine, interleaving
  their runs. Save all runs, not just the fastest. Report median results and
  dispersion; record individual latency histograms and sample counts. Extend
  low-rate tests until there are at least 100,000 latency samples for p99.9.

Use a rate-controlled HTTP load generator such as a pinned version of
[wrk2](https://github.com/giltene/wrk2). Its intended-arrival-time latency
measurement addresses coordinated omission: a slowing server must not make
the client stop generating load and hide queueing delays. Validate tool timing
precision before setting fine-grained latency gates. Count unsent scheduled
requests, timeouts, and incomplete responses explicitly; a generator that
cannot sustain the configured rate makes the run invalid.

## Metrics and initial acceptance targets

These targets apply only to the reference environment above. Phase 1 fixes the
exact hardware and collects the baseline. Retain the initial goals alongside
any revised goals and explain changes; do not lower a gate silently to pass.

The primary workload is `GET /plaintext`, a fixed 13-byte `Hello, World!` body,
one registered route, no middleware, and no application I/O. Record the exact
request headers and response bytes in the workload specification.

| Metric | Definition | Initial target |
| --- | --- | --- |
| Sustainable capacity | Highest offered rate sustained for five minutes with p99 <= 5 ms, p99.9 <= 20 ms, errors <= 0.01%, and no increasing backlog | >= 25,000 successful requests/s with one worker; >= 80,000 with four workers |
| Latency | Client-observed intended arrival to complete response; report p50, p95, p99, p99.9, and maximum | At 70% of measured capacity: p95 <= 2 ms, p99 <= 5 ms, p99.9 <= 20 ms |
| CPU efficiency | Total server process CPU seconds, including every thread, divided by successful requests | <= 40 microseconds of CPU per plaintext request at 70% capacity |
| Worker scaling | Four-worker sustainable capacity divided by one-worker capacity, with enough connections and load | >= 3.2x; report two-worker scaling as well |
| Reliability | Wrong status/body/framing, timeouts, resets, connection failures, and unfinished requests | Zero wrong responses; operational failures <= 0.01% at supported load |
| Idle memory | Server RSS after startup and warm-up, with four workers and no clients | <= 64 MiB |
| Connection memory | Incremental server RSS divided by established idle keep-alive connections, measured after each client completes one request | <= 32 KiB/connection at 10,000 connections; total server RSS <= 512 MiB |
| Soak stability | Compare five-minute windows after warm-up during a 30-minute fixed-rate run at 70% capacity | Throughput drift <= 5%; p99 drift <= 10%; RSS growth <= max(5% of starting RSS, 8 MiB), with no sustained upward trend |
| Overload recovery | Hold 150% of measured capacity for 30 seconds, then return to 50% | Within 10 seconds, meet the latency/error gates again; no crash or continuing queue/memory growth after recovery |

For latency regression comparisons, use the **same absolute offered rate**, based
on 70% of the baseline's capacity. Also report each version's capacity curve.
Comparing only percentages of each version's own capacity could hide a slowdown.
Keep timeout/error counts beside successful-request latency percentiles.

Measure these diagnostic metrics from the beginning, then assign budgets once
the baseline establishes their cost:

- Parser and response serialization: ns/request, CPU ns/byte, and MiB/s for
  larger bodies; allocation count and allocated bytes/request.
- Dispatch: ns/request versus route count, match position, middleware count,
  router depth, and concurrent dispatch threads.
- Connection churn: successful new connections/s, handshake-to-response p99,
  file-descriptor count, and accept-loop CPU cost.
- Large bodies: successful payload MiB/s, CPU cost/byte, peak RSS, and p99 by size.
- Resource pressure: kernel socket memory, context switches, queued work,
  retransmissions, server/client CPU, and network utilization.

Allocation profiling and detailed tracing run separately from the headline
timing tests so instrumentation does not distort the published numbers.

## Workloads to add

| Suite | Cases | Purpose |
| --- | --- | --- |
| Parser microbenchmarks | Small GET; 8/32/128 headers; 1 KiB/64 KiB/1 MiB bodies; fixed-length and chunked encoding; 1/64/4,096-byte fragments | Separate parsing, copying, and allocation costs; expose nonlinear fragmented-input behavior |
| Dispatch microbenchmarks | 1/10/100/1,000 routes; first/middle/last/missing match; exact/parameter/wildcard routes; router depths 0/1/4; 0/1/5/10 pass-through middleware | Expose lookup, snapshot, parameter decoding, and middleware overhead; exercise mutable and const request overloads separately |
| Response microbenchmarks | Empty/13-byte/1 KiB/64 KiB/1 MiB bodies and 0/8/32 application headers | Measure formatting and body-copy costs |
| HTTP baseline | Plaintext and fixed 1 KiB response, one and four workers | Establish throughput/latency capacity curves through the public `App` API |
| HTTP application profile | 100 routes, parameter extraction, one mounted router, and five pass-through middleware | Quantify realistic framework overhead with identical response sizes |
| HTTP body handling | Binary POST echo at 1 KiB/64 KiB/1 MiB, including chunked and fragmented uploads | Measure useful byte throughput and memory pressure |
| Connection lifecycle | Keep-alive versus one request/connection; pipeline depths 1/8/16; 10,000 idle connections; churn while idle clients remain connected | Measure accept/cleanup scaling and connection footprint |
| Contention and stress | Route updates during traffic; bursts; slow readers/writers; a small fraction of deliberately blocking handlers | Reveal tail-latency spikes, lock contention, and resource growth |

The main HTTP runs must use `App`, dispatch, the real parser, and response
serialization. A raw Asio response loop can be a diagnostic lower bound, but
does not replace the framework benchmark. Fixed JSON bytes can be a payload
profile; do not label them JSON serialization until actual serialization occurs.
Plaintext and JSON are also workloads in the
[TechEmpower benchmark suite](https://github.com/TechEmpower/FrameworkBenchmarks),
but results are comparable only when the workload and environment match.

Validate each profile before timing: status, headers, body length/content,
keep-alive behavior, and response ordering. Use full validation in a companion
correctness run and lightweight sampled validation during load. Include
intentional error scenarios in separate profiles with their expected statuses.

## Implementation sequence

1. **Add the harness and capture the baseline.** Add an optional
   `LIGHTNING_BUILD_BENCHMARKS` CMake switch, defaulting to OFF. Put the server
   fixture and component benchmarks under `benchmarks/`, and the runner,
   workloads, and result comparison under `benchmarks/scripts/` and
   `benchmarks/workloads/`. Add a pinned, optional
   [Google Benchmark](https://github.com/google/benchmark/blob/main/docs/user_guide.md)
   dependency for component timings, repetitions, and JSON output. Keep it out
   of the normal library dependency path. Add a proposed `./build.sh bench`
   entry point that selects an isolated Release build and rejects sanitizers.
   First deliver plaintext load curves and parser/dispatch/response timings.
   Exit criterion: reproducible raw results for a recorded baseline commit.

2. **Cover framework behavior.** Add routing, middleware, body, and connection
   profiles from the matrix. Configure workers, bind address, payload, and routes
   on the fixture command line. Add readiness detection, bounded startup/shutdown,
   cleanup on failure, correctness validation, and explicit run timeouts.
   Use an HTTP-aware client for fragmented, pipelined, and slow-client cases that
   the primary load generator cannot express accurately. Exit criterion: each
   scenario has a machine-readable specification and validated output.

3. **Measure scaling and stability.** Add allocation and CPU profiles, the worker
   sweep, connection churn, soak, and overload/recovery tests. Diagnose problems
   before optimizing. Exit criterion: a report identifying which target gaps
   come from parsing, dispatch, allocation, synchronization, or networking.

4. **Automate regression detection.** Ordinary pull-request CI builds and smoke
   tests the harness, but does not enforce absolute timing on shared runners.
   A dedicated performance runner compares baseline and candidate; nightly runs
   execute the wider matrix, and release runs include the soak and stress suite.
   Publish raw JSON, histograms, environment metadata, and a Markdown report.
   Component/HTTP correctness smoke tests now run in ordinary CI. The dedicated
   runner, offered-rate automation, nightly matrix, and release soak jobs remain planned.

Each result must identify the commit (and dirty-tree status), scenario,
environment, flags, workers, connections, offered/achieved rates, validated
successful responses, all failure counts, latency histograms, CPU seconds, RSS,
and repetitions. Keep platform baselines separate. Preserve the accepted baseline
when a regression is detected; updates require a documented explanation.

Initial regression thresholds on the dedicated runner: investigate a >5% loss
in capacity or >10% increase in p99, CPU/request, memory, or component ns/op.
Require a repeatable difference beyond measured run-to-run noise before blocking
a change; unstable results are inconclusive and rerun. Correctness failures
always block. Start timing checks as advisory while collecting enough baseline
runs to characterize noise, then enable the confirmed gates.

## Likely investigation points in the current implementation

These are code-based hypotheses, not measured bottlenecks:

- `Dispatcher::_capture()` copies layer snapshots under mutexes and traverses
  mounted routers for each request; dispatch then scans layers in order.
- Middleware invocation creates shared continuation state; parameter and router
  context handling introduce additional ownership and copying costs.
- `HttpConnection` allocates new request state after each response, while
  response generation creates an owned wire-format string.
- `HttpServer::_acceptNext()` scans tracked weak connections on each accept,
  making churn with many live connections an important test.
- Handler execution occupies I/O workers, while coroutine handlers may suspend.
  Blocking application work can
  occupy workers and inflate latency even when socket I/O is asynchronous.

Optimize these paths only after the measurements show their impact, and retain
the existing ownership, concurrency, and protocol correctness guarantees.
