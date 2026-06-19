#!/usr/bin/env python3
"""Integration tests for malformed telemetry input handling."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "cpp" / "build" / "bms-validate"
FIXTURES = ROOT / "cpp" / "tests" / "fixtures"


class MalformedInputCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not VALIDATOR.exists():
            raise unittest.SkipTest("bms-validate binary not built; run make build first")

    def run_validator(self, input_path: Path, file_format: str) -> int:
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "report.json"
            result = subprocess.run(
                [
                    str(VALIDATOR),
                    "--input",
                    str(input_path),
                    "--output",
                    str(output),
                    "--format",
                    file_format,
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            return result.returncode

    def test_csv_unterminated_quote_exits_2(self) -> None:
        self.assertEqual(
            self.run_validator(FIXTURES / "malformed_unterminated_quote.csv", "csv"),
            2,
        )

    def test_csv_missing_column_exits_2(self) -> None:
        self.assertEqual(
            self.run_validator(FIXTURES / "malformed_missing_column.csv", "csv"),
            2,
        )

    def test_json_unicode_escape_exits_2(self) -> None:
        self.assertEqual(
            self.run_validator(FIXTURES / "malformed_unicode_escape.json", "json"),
            2,
        )

    def test_json_trailing_content_exits_2(self) -> None:
        self.assertEqual(
            self.run_validator(FIXTURES / "malformed_trailing_content.json", "json"),
            2,
        )


if __name__ == "__main__":
    unittest.main()
