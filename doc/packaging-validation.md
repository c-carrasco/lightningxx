# Packaging and testing stage validation

Validated on macOS arm64 with AppleClang 21.0.0 on September 13, 2026.

The full suite now contains 248 GoogleTests. Seven new application-client tests
cover nested middleware/JSON routes, every HTTP method, binary ownership,
chunked requests and trailers, original HEAD framing, fallback/error handling,
automatic OPTIONS, malformed input and limits, and client/result lifetimes.

These checks passed:

```bash
./build.sh release test
./build.sh test asan=on ubsan=on
DEFAULT_BUILD_DIR=build/coverage ./build.sh coverage=on test
python3 tools/test_coverage.py
```

After raising the initial coverage floors, the final report was regenerated with
`cmake --build build/coverage/debug --parallel 4 --target coverage`, using
`LIGHTNING_COVERAGE_MIN_LINES=90` and `LIGHTNING_COVERAGE_MIN_BRANCHES=85`.

| Metric | Covered / total | Coverage | Required |
| --- | --- | --- | --- |
| Lines | 1528 / 1584 | 96.46% | 90% |
| Branches | 703 / 766 | 91.78% | 85% |
| Functions | 203 / 217 | 93.55% | Reported only |

The totals include instrumented project sources and public headers; tests and
third-party code are excluded. Fresh per-run profiles prevent prior runs or
filtered tests from inflating results. Four Python regressions check source
filtering, empty coverage rejection, uncovered-line accounting, inclusive floors,
and invalid thresholds. Reports are under `build/coverage/debug/coverage/`.

The Release CTest `installed_consumer` also passed. It configures and builds
Lightning++ with `BUILD_TESTING=OFF`, `LIGHTNING_USE_CONAN=OFF`, and GoogleTest
discovery explicitly disabled, using the already-provisioned dependencies. It
then installs the main build, moves its prefix, checks exported targets for
source/build-path leaks, and configures/builds/runs `test/consumer` against the
relocated installation. The consumer requests the package twice, links only
`lightning::lightning`, inherits C++20, and exercises routers, JSON middleware,
and the testing client. No GoogleTest target is imported. Direct CMake
configuration with no build type was separately checked to default to Release.

Release compilation treats warnings as errors. ASan/UBSan completed all 248
tests without reports. Workflow YAML parsing, build-script shell syntax, and
`git diff --check` also passed. The GitHub-hosted Linux/macOS jobs and documentation
deployment were not executed in this local session; CI will run them on push or
pull request. TSan was not repeated for this stage; the existing Asio/macOS report
and reproducer remain in [transport validation](transport-validation.md).

The next roadmap stage is coroutine handlers. Performance work remains described
separately in [the measurement plan](performance-plan.md).
