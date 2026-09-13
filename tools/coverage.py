#!/usr/bin/env python3
"""Run a fresh LLVM-instrumented suite, report project coverage, enforce floors."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import os


def summarize(export, source):
    roots = [(source / name).resolve() for name in ("src/lib", "src/include/lightning")]
    files = {}
    for unit in export["data"]:
        for item in unit["files"]:
            path = Path(item["filename"]).resolve()
            if any(path.is_relative_to(root) for root in roots):
                files[str(path)] = item
    totals = {}
    for metric in ("lines", "branches", "functions"):
        count = sum(item["summary"][metric]["count"] for item in files.values())
        covered = sum(item["summary"][metric]["covered"] for item in files.values())
        totals[metric] = {"count": count, "covered": covered,
                          "percent": 100 * covered / count if count else 100.0}
    if not totals["lines"]["count"]:
        raise ValueError("No project line coverage found; refusing an empty report")
    return sorted(files), totals


def meets_thresholds(totals, lines, branches):
    return totals["lines"]["percent"] >= lines and totals["branches"]["percent"] >= branches


def percentage(value):
    number = float(value)
    if not 0 <= number <= 100:
        raise argparse.ArgumentTypeError("Coverage thresholds must be between 0 and 100")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("binary", "source", "output", "llvm-cov", "llvm-profdata"):
        parser.add_argument("--" + option, required=True)
    parser.add_argument("--min-lines", type=percentage, default=90)
    parser.add_argument("--min-branches", type=percentage, default=85)
    args = parser.parse_args()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    # Per-run profiles avoid inflating coverage with stale or previously filtered tests.
    with tempfile.TemporaryDirectory(prefix="profiles-", dir=output) as temporary:
        profiles = Path(temporary)
        env = dict(os.environ, LLVM_PROFILE_FILE=str(profiles / "%p-%m.profraw"))
        for key in ("GTEST_FILTER", "GTEST_TOTAL_SHARDS", "GTEST_SHARD_INDEX", "GTEST_SHARD_STATUS_FILE"):
            env.pop(key, None)
        subprocess.run([args.binary, "--gtest_filter=*", "--gtest_repeat=1"], env=env, check=True, timeout=60)
        raw = sorted(profiles.glob("*.profraw"))
        if not raw:
            raise ValueError("Test executable did not produce LLVM profiles")
        merged = profiles / "merged.profdata"
        subprocess.run([args.llvm_profdata, "merge", "-sparse", *map(str, raw), "-o", str(merged)], check=True)
        command = [args.llvm_cov, "export", args.binary, "-instr-profile=" + str(merged)]
        export = json.loads(subprocess.check_output(command, text=True))
        files, totals = summarize(export, Path(args.source))
        (output / "summary.json").write_text(json.dumps(totals, indent=2) + "\n")
        with (output / "coverage.lcov").open("w") as report:
            subprocess.run([*command, "-format=lcov", *files], stdout=report, check=True)
        subprocess.run([args.llvm_cov, "show", args.binary, "-instr-profile=" + str(merged),
                        "-format=html", "-show-branches=count", "-output-dir=" + str(output / "html"),
                        *files], check=True)
    for metric, result in totals.items():
        print(f"Project {metric}: {result['covered']}/{result['count']} ({result['percent']:.2f}%)")
    if not meets_thresholds(totals, args.min_lines, args.min_branches):
        parser.exit(1, f"Coverage below required floors: lines {args.min_lines}%, branches {args.min_branches}%\n")


if __name__ == "__main__":
    main()
