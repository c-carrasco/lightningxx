#!/usr/bin/env python3
"""Validated, closed-loop loopback diagnostics; not an offered-rate capacity test."""
import argparse
import concurrent.futures
import datetime
import hashlib
import http.client
import json
import math
import os
from pathlib import Path
import platform
import select
import socket
import statistics
import subprocess
import threading
import time

SPEC = json.loads((Path(__file__).resolve().parents[1] / "workloads/http.json").read_text())


class BadResponse(Exception):
    pass


def workload(name, payload_bytes):
    case = SPEC["scenarios"][name]
    body = bytes(i % 256 for i in range(payload_bytes)) if case.get("echo") else b""
    expected = body if case.get("echo") else (case.get("body") or case["repeat"] * case["response_bytes"]).encode()
    return case, body, expected


def validate(response, expected, status=200, churn=False):
    if response.version != 11 or response.status != status:
        raise BadResponse(f"Wrong protocol/status: {response.version}/{response.status}")
    if response.headers.get_all("Content-Length") != [str(len(expected))] or response.getheader("Transfer-Encoding"):
        raise BadResponse("Wrong or ambiguous response framing")
    if response.read(len(expected) + 1) != expected:
        raise BadResponse("Wrong response body")
    if response.will_close and not churn:
        raise BadResponse("Keep-alive connection unexpectedly closed")


def transaction(connection, case, body, expected, chunked, fragment, churn):
    connection.putrequest(case["method"], case["path"], skip_host=True, skip_accept_encoding=True)
    connection.putheader("Host", "localhost")
    if churn:
        connection.putheader("Connection", "close")
    if case["method"] == "POST":
        connection.putheader("Content-Type", "application/octet-stream")
        connection.putheader("Transfer-Encoding" if chunked else "Content-Length", "chunked" if chunked else str(len(body)))
    connection.endheaders()
    for offset in range(0, len(body), fragment):
        part = body[offset:offset + fragment]
        connection.send(f"{len(part):x}\r\n".encode() + part + b"\r\n" if chunked else part)
    if chunked:
        connection.send(b"0\r\n\r\n")
    validate(connection.getresponse(), expected, case["status"], churn)
    if churn:
        connection.close()


class Server:
    def __init__(self, binary, scenario, workers):
        self.process = subprocess.Popen([str(binary), "--scenario", scenario, "--workers", str(workers), "--port", "0"],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            self.info = self.read()
            if not self.info.get("ready"):
                raise RuntimeError("Server did not announce readiness")
        except BaseException:
            self.close()
            raise

    def read(self):
        if not select.select([self.process.stdout], [], [], 5)[0]:
            raise RuntimeError("Server control response timed out")
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("Server exited before its control response")
        return json.loads(line)

    def stats(self):
        self.process.stdin.write("stats\n")
        self.process.stdin.flush()
        return self.read()

    def close(self):
        try:
            if self.process.poll() is None:
                try:
                    self.process.stdin.write("stop\n")
                    self.process.stdin.flush()
                except (BrokenPipeError, OSError):
                    pass
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=1)
                    except subprocess.TimeoutExpired:
                        self.process.kill()
                        self.process.wait(timeout=1)
        finally:
            for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
                stream.close()


def latency_summary(samples):
    ordered = sorted(samples)
    def percentile(fraction):
        return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)] / 1000 if ordered else None
    histogram = {}
    for value in samples:
        upper = 1 << max(0, (value - 1).bit_length())
        histogram[upper] = histogram.get(upper, 0) + 1
    return {"samples": len(samples), "unit": "microseconds", "p50": percentile(.5), "p95": percentile(.95),
            "p99": percentile(.99), "p99_9": percentile(.999) if len(samples) >= 100000 else None,
            "max": ordered[-1] / 1000 if ordered else None,
            "histogram": {"unit": "nanoseconds", "buckets": "exclusive lower, inclusive power-of-two upper bounds",
                          "upper_bound_counts": [[key, histogram[key]] for key in sorted(histogram)]}}


def phase(port, case, body, expected, args, duration, usage=None):
    ready = threading.Barrier(args.connections + 1)
    start = threading.Event()
    window = {}

    def client():
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=args.timeout)
        samples, errors = [], {"wrong_response": 0, "timeout": 0, "connection": 0, "setup": 0}
        detail = None
        try:
            # Connect and validate once outside the timing window for every client.
            transaction(connection, case, body, expected, args.chunked, args.fragment, args.churn)
            ready.wait(timeout=args.timeout + 5)
            if not start.wait(timeout=args.timeout + 5):
                raise RuntimeError("Start barrier timed out")
            first = True
            while (args.smoke and first) or (not args.smoke and time.perf_counter_ns() < window["stop"]):
                first = False
                before = time.perf_counter_ns()
                try:
                    transaction(connection, case, body, expected, args.chunked, args.fragment, args.churn)
                    samples.append(time.perf_counter_ns() - before)
                except (BadResponse, OSError, http.client.HTTPException) as error:
                    key = "wrong_response" if isinstance(error, BadResponse) else "timeout" if isinstance(error, TimeoutError) else "connection"
                    errors[key] += 1
                    detail = str(error)
                    break
        except (BadResponse, OSError, http.client.HTTPException, threading.BrokenBarrierError, RuntimeError) as error:
            errors["setup"] += 1
            detail = str(error)
            ready.abort()
        finally:
            connection.close()
        return samples, errors, detail

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.connections) as pool:
        jobs = [pool.submit(client) for _ in range(args.connections)]
        try:
            ready.wait(timeout=args.timeout + 5)
        except threading.BrokenBarrierError:
            pass
        cpu_before = usage() if usage else None
        before = time.perf_counter_ns()
        window["stop"] = before + int(duration * 1e9)
        start.set()
        results = [job.result() for job in jobs]
        elapsed = (time.perf_counter_ns() - before) / 1e9
    samples = [value for values, _, _ in results for value in values]
    errors = {key: sum(item[1][key] for item in results) for key in results[0][1]}
    successful = len(samples)
    result = {"valid": successful > 0 and not any(errors.values()), "successful": successful,
            "attempted": successful + sum(value for key, value in errors.items() if key != "setup"),
            "errors": errors, "error_details": [detail for _, _, detail in results if detail],
            "elapsed_seconds": elapsed, "successful_requests_per_second": successful / elapsed,
            "payload_mib_per_second": successful * len(expected) / elapsed / (1024 * 1024),
            "latency": latency_summary(samples)}
    if usage:
        after = usage()
        result["server_cpu_seconds"] = after["cpu_seconds"] - cpu_before["cpu_seconds"]
        result["server_cpu_us_per_success"] = result["server_cpu_seconds"] * 1e6 / successful if successful else None
        result["server_peak_rss_bytes"] = after["peak_rss_bytes"]
    return result


def run(args):
    case, body, expected = workload(args.scenario, args.payload_bytes)
    request_headers = dict(SPEC["request_headers"])
    if args.churn:
        request_headers["Connection"] = "close"
    if body:
        request_headers["Content-Type"] = "application/octet-stream"
        request_headers["Transfer-Encoding" if args.chunked else "Content-Length"] = "chunked" if args.chunked else str(len(body))
    runs = []
    for repetition in range(args.repetitions):
        server = Server(args.server, args.scenario, args.workers)
        try:
            if not args.smoke and not server.info["build"]["timing_eligible"]:
                raise ValueError("Measurements require an uninstrumented Release server; use --smoke for correctness")
            warmup = phase(server.info["port"], case, body, expected, args, args.warmup)
            if not warmup["valid"]:
                runs.append({"repetition": repetition, "server": server.info, "warmup": warmup, "measurement": None})
                continue
            measurement = phase(server.info["port"], case, body, expected, args, args.duration, server.stats)
            runs.append({"repetition": repetition, "server": server.info, "warmup": warmup, "measurement": measurement})
        finally:
            server.close()
    valid = all(item["measurement"] and item["measurement"]["valid"] for item in runs)
    rates = [item["measurement"]["successful_requests_per_second"] for item in runs if item["measurement"]]
    return {"schema_version": 1, "kind": "http_closed_loop", "mode": "smoke" if args.smoke else "measurement",
            "valid": bool(valid), "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "client": {"system": platform.platform(), "python": platform.python_version(), "logical_cpus": os.cpu_count()},
            "scenario": args.scenario, "specification": case,
            "request_headers": request_headers,
            "request_body_bytes": len(body), "request_body_sha256": hashlib.sha256(body).hexdigest(),
            "expected_response_bytes": len(expected), "expected_response_sha256": hashlib.sha256(expected).hexdigest(),
            "configuration": {key: value for key, value in vars(args).items() if key not in ("server", "output")},
            "offered_requests_per_second": None, "coordinated_omission_corrected": False,
            "connection_model": "one outstanding request per connection", "runs": runs,
            "summary": {"median_successful_requests_per_second": statistics.median(rates) if valid else None,
                        "min_successful_requests_per_second": min(rates) if valid else None,
                        "max_successful_requests_per_second": max(rates) if valid else None}}


def positive(value):
    result = float(value)
    if not math.isfinite(result) or not 0 < result <= 3600:
        raise argparse.ArgumentTypeError("Expected a finite duration in (0, 3600] seconds")
    return result


def integer(maximum):
    def parse(value):
        result = int(value)
        if not 0 < result <= maximum:
            raise argparse.ArgumentTypeError(f"Expected an integer in [1, {maximum}]")
        return result
    return parse


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--server", type=Path, required=True)
    result.add_argument("--scenario", choices=SPEC["scenarios"], default="plaintext")
    result.add_argument("--workers", type=integer(1024), default=1)
    result.add_argument("--connections", type=integer(1024), default=16)
    result.add_argument("--duration", type=positive, default=5)
    result.add_argument("--warmup", type=positive, default=1)
    result.add_argument("--timeout", type=positive, default=5)
    result.add_argument("--repetitions", type=integer(100), default=3)
    result.add_argument("--payload-bytes", type=integer(1048576), default=1024)
    result.add_argument("--fragment", type=integer(1048576), default=4096)
    result.add_argument("--chunked", action="store_true")
    result.add_argument("--churn", action="store_true")
    result.add_argument("--smoke", action="store_true")
    result.add_argument("--output", type=Path)
    return result


def main():
    cli = parser()
    args = cli.parse_args()
    if args.chunked and args.scenario != "echo":
        cli.error("--chunked requires --scenario echo")
    report = run(args)
    serialized = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(serialized)
    else:
        print(serialized, end="")
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
