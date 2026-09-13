#!/usr/bin/env python3
"""Compare repeated component measurements; advisory unless explicitly gated."""
import argparse
import json
import math
from pathlib import Path
import statistics


def samples(document):
    context = document["context"]
    if context.get("mode") != "measurement" or context.get("timing_eligible") not in (True, "true"):
        raise ValueError("Only uninstrumented Release measurements can be compared")
    result = {}
    factors = {"ns": 1, "us": 1000, "ms": 1000000, "s": 1000000000}
    for item in document["benchmarks"]:
        if item.get("error_occurred"):
            raise ValueError("Benchmark contains a correctness error")
        if item.get("run_type", "iteration") != "iteration":
            continue
        value = item["real_time"] * factors[item["time_unit"]]
        if not math.isfinite(value) or value <= 0 or item["iterations"] <= 0:
            raise ValueError("Invalid benchmark sample")
        result.setdefault(item.get("run_name", item["name"]), []).append(value)
    if not result:
        raise ValueError("No raw benchmark samples found")
    if any(len(values) < 3 for values in result.values()):
        raise ValueError("At least three raw repetitions per case are required")
    return result


def compare(baseline, candidate, threshold=10):
    if not math.isfinite(threshold) or threshold < 0:
        raise ValueError("Threshold must be finite and nonnegative")
    keys = ("host_name", "num_cpus", "compiler", "compile_flags", "build_type", "cxx_standard", "dependencies")
    for key in keys:
        if key not in baseline["context"] or baseline["context"].get(key) != candidate["context"].get(key):
            raise ValueError(f"Incompatible benchmark environments: {key}")
    old, new = samples(baseline), samples(candidate)
    if old.keys() != new.keys():
        raise ValueError("Benchmark case sets differ; compare matching filters")
    rows = []
    for name in sorted(old):
        left, right = statistics.median(old[name]), statistics.median(new[name])
        change = 100 * (right / left - 1)
        status = "regression" if change > threshold and min(new[name]) > max(old[name]) else "inconclusive" if change > threshold else "within_threshold"
        rows.append({"name": name, "baseline_median_ns": left, "candidate_median_ns": right,
                     "change_percent": change, "status": status,
                     "baseline_samples_ns": old[name], "candidate_samples_ns": new[name]})
    return {"schema_version": 1, "kind": "component_comparison", "threshold_percent": threshold,
            "baseline_revision": baseline["context"].get("revision"),
            "candidate_revision": candidate["context"].get("revision"), "comparisons": rows,
            "regressions": sum(row["status"] == "regression" for row in rows)}


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("baseline", type=Path)
    cli.add_argument("candidate", type=Path)
    cli.add_argument("--threshold-percent", type=float, default=10)
    cli.add_argument("--fail-on-regression", action="store_true")
    cli.add_argument("--output", type=Path)
    args = cli.parse_args()
    if args.output and args.output.resolve() in (args.baseline.resolve(), args.candidate.resolve()):
        cli.error("Output must not overwrite either input")
    try:
        report = compare(json.loads(args.baseline.read_text()), json.loads(args.candidate.read_text()), args.threshold_percent)
    except (KeyError, ValueError) as error:
        cli.error(str(error))
    data = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(data)
    else:
        print(data, end="")
    return 1 if args.fail_on_regression and report["regressions"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
