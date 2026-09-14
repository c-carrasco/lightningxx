import argparse
from pathlib import Path
import unittest
from coverage import meets_thresholds, percentage, summarize


def entry(path, covered=3, count=4):
    return {"filename": path, "summary": {
        metric: {"covered": covered, "count": count}
        for metric in ("lines", "branches", "functions")}}


class CoverageTests(unittest.TestCase):
    def test_only_project_sources_and_headers_count(self):
        root = Path("/project")
        files, totals = summarize({"data": [{"files": [
            entry("/project/src/lib/app.cxx"), entry("/project/src/include/lightning/app.h", 1, 1),
            entry("/project/src/test/test_app.cxx", 100, 100),
            entry("/project/src/library/other.cxx", 100, 100),
            entry("/project/build/_deps/logger.h", 100, 100),
            entry("/other/src/lib/app.cxx", 100, 100)]}]}, root)
        self.assertEqual(len(files), 2)
        self.assertEqual(totals["lines"], {"count": 5, "covered": 4, "percent": 80})

    def test_empty_report_fails(self):
        for files in ([], [entry("/project/src/lib/empty.cxx", 0, 0)]):
            with self.assertRaises(ValueError):
                summarize({"data": [{"files": files}]}, Path("/project"))

    def test_uncovered_lines_count_and_thresholds_are_inclusive(self):
        _, totals = summarize({"data": [{"files": [entry("/project/src/lib/app.cxx", 0, 10)]}]}, Path("/project"))
        self.assertFalse(meets_thresholds(totals, 1, 0))
        self.assertFalse(meets_thresholds(totals, 0, 1))
        self.assertTrue(meets_thresholds(totals, 0, 0))
        totals = {"lines": {"percent": 80}, "branches": {"percent": 60}}
        self.assertTrue(meets_thresholds(totals, 80, 60))
        self.assertFalse(meets_thresholds(totals, 80.01, 60))

    def test_invalid_thresholds_fail(self):
        for value in ("-1", "101", "nan", "inf"):
            with self.assertRaises(argparse.ArgumentTypeError):
                percentage(value)
        self.assertEqual(percentage("80.5"), 80.5)


if __name__ == "__main__":
    unittest.main()
