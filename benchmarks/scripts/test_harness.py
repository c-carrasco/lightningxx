#!/usr/bin/env python3
import argparse
import copy
import http.client
import http.server
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import Mock
import http_bench
import compare

cli = argparse.ArgumentParser()
cli.add_argument("--server", type=Path, required=True)
options, rest = cli.parse_known_args()


def document(values):
    return {"context": {"mode": "measurement", "timing_eligible": "true", "host_name": "reference",
                        "num_cpus": 4, "compiler": "compiler", "compile_flags": "-O3", "build_type": "Release",
                        "cxx_standard": "20", "dependencies": "pinned"},
            "benchmarks": [{"name": "case", "run_type": "iteration", "real_time": value,
                            "iterations": 100, "time_unit": "ns"} for value in values]}


class MetricsTests(unittest.TestCase):
    def test_percentiles_histogram_and_sample_floor(self):
        result = http_bench.latency_summary([1000, 2000, 3000, 4000])
        self.assertEqual(result["p50"], 2)
        self.assertEqual(result["p99"], 4)
        self.assertIsNone(result["p99_9"])
        self.assertEqual(sum(count for _, count in result["histogram"]["upper_bound_counts"]), 4)
        self.assertEqual(http_bench.latency_summary([1000] * 100000)["p99_9"], 1)
        self.assertIsNone(http_bench.latency_summary([])["p50"])

    def test_bad_status_body_and_framing_are_rejected(self):
        for status, lengths, body in [(500, ["2"], b"ok"), (200, ["3"], b"ok"),
                                      (200, ["2", "2"], b"ok"), (200, ["2"], b"no")]:
            response = Mock(version=11, status=status, will_close=False)
            response.headers.get_all.return_value = lengths
            response.getheader.return_value = None
            response.read.return_value = body
            with self.assertRaises(http_bench.BadResponse):
                http_bench.validate(response, b"ok")

    def test_invalid_duration_and_count(self):
        for value in ["nan", "inf", "-1", "0"]:
            with self.assertRaises(argparse.ArgumentTypeError):
                http_bench.positive(value)
        with self.assertRaises(argparse.ArgumentTypeError):
            http_bench.integer(1024)("1025")

    def test_comparison_rejects_wrong_environment_smoke_errors_and_missing_samples(self):
        baseline = document([100, 101, 99])
        for mutate in [lambda x: x["context"].update(mode="smoke"),
                       lambda x: x["context"].update(compiler="different"),
                       lambda x: x["benchmarks"][0].update(error_occurred=True),
                       lambda x: x.update(benchmarks=x["benchmarks"][:1]),
                       lambda x: x["benchmarks"][0].update(real_time=float("nan"))]:
            candidate = copy.deepcopy(baseline)
            mutate(candidate)
            with self.assertRaises(ValueError):
                compare.compare(baseline, candidate)

    def test_comparison_distinguishes_regressions_noise_and_equal_results(self):
        self.assertEqual(compare.compare(document([99, 100, 101]), document([120, 121, 122]))["regressions"], 1)
        self.assertEqual(compare.compare(document([90, 100, 130]), document([120, 121, 122]))["comparisons"][0]["status"], "inconclusive")
        self.assertEqual(compare.compare(document([99, 100, 101]), document([99, 100, 101]))["regressions"], 0)


class HttpSmokeTests(unittest.TestCase):
    def arguments(self, scenario, *extra):
        return http_bench.parser().parse_args(["--server", str(options.server), "--scenario", scenario,
            "--connections", "2", "--workers", "2", "--duration", "0.02", "--warmup", "0.01",
            "--repetitions", "1", "--smoke", *extra])

    def test_every_workload_validates_and_reports_matching_counts(self):
        for scenario in http_bench.SPEC["scenarios"]:
            with self.subTest(scenario=scenario):
                result = http_bench.run(self.arguments(scenario))
                self.assertTrue(result["valid"], result)
                measurement = result["runs"][0]["measurement"]
                self.assertGreater(measurement["successful"], 0)
                self.assertEqual(measurement["successful"], measurement["attempted"])
                self.assertEqual(measurement["successful"], measurement["latency"]["samples"])
                self.assertGreater(measurement["server_peak_rss_bytes"], 0)
                self.assertGreaterEqual(measurement["server_cpu_seconds"], 0)
                self.assertFalse(result["coordinated_omission_corrected"])

    def test_binary_payload_sizes_chunking_fragmentation_and_churn(self):
        for size, flags in [(1024, ["--chunked", "--fragment", "64"]),
                            (65536, ["--churn"]), (1048576, ["--chunked"])]:
            result = http_bench.run(self.arguments("echo", "--payload-bytes", str(size), *flags))
            self.assertTrue(result["valid"], result)

    def test_wrong_responses_invalidate_a_phase_instead_of_improving_throughput(self):
        class Wrong(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"
            def do_GET(self):
                self.send_response(200)
                self.send_header("Content-Length", "3")
                self.end_headers()
                self.wfile.write(b"bad")
            def log_message(self, *args):
                pass
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Wrong)
        worker = threading.Thread(target=server.serve_forever)
        worker.start()
        try:
            args = self.arguments("plaintext")
            case, body, expected = http_bench.workload("plaintext", 1024)
            result = http_bench.phase(server.server_port, case, body, expected, args, .01)
            self.assertFalse(result["valid"])
            self.assertEqual(result["successful"], 0)
            self.assertGreater(result["errors"]["setup"], 0)
        finally:
            server.shutdown()
            worker.join(timeout=2)
            server.server_close()

    def test_server_rejects_unknown_scenario_and_exits(self):
        with self.assertRaises(RuntimeError):
            http_bench.Server(options.server, "does-not-exist", 1)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0], *rest])
