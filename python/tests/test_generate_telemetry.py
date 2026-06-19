#!/usr/bin/env python3
"""Tests for synthetic telemetry generation and fault injection."""

from __future__ import annotations

import csv
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "python" / "generate_telemetry.py"
VALIDATOR = ROOT / "cpp" / "build" / "bms-validate"

FAULT_RULES = {
    "voltage": "voltage_range",
    "temperature": "temperature_range",
    "soc": "soc_range",
    "bad_timestamp": "timestamp_format",
    "missing_cell_id": "cell_id_required",
    "duplicate": "duplicate_reading",
    "non_monotonic": "non_monotonic_timestamp",
}


def run_generator(output: Path, *, records: int = 20, seed: int = 42, **kwargs: object) -> None:
    command = [
        sys.executable,
        str(GENERATOR),
        "--output",
        str(output),
        "--records",
        str(records),
        "--seed",
        str(seed),
    ]
    if "inject_faults" in kwargs:
        command.extend(["--inject-faults", str(kwargs["inject_faults"])])
    if "fault_types" in kwargs:
        command.extend(["--fault-types", str(kwargs["fault_types"])])
    subprocess.run(command, check=True, cwd=ROOT)


def validate_csv(path: Path) -> dict:
    result = subprocess.run(
        [str(VALIDATOR), "--input", str(path), "--output", str(path.with_suffix(".json")), "--format", "csv"],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    report = json.loads(path.with_suffix(".json").read_text(encoding="utf-8"))
    report["exit_code"] = result.returncode
    return report


class GenerateTelemetryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not VALIDATOR.exists():
            raise unittest.SkipTest("bms-validate binary not built; run make build first")

    def test_all_fault_types_are_injectable(self) -> None:
        for fault_type, expected_rule in FAULT_RULES.items():
            with self.subTest(fault_type=fault_type):
                with tempfile.TemporaryDirectory() as tmpdir:
                    output = Path(tmpdir) / f"{fault_type}.csv"
                    run_generator(output, records=20, seed=42, fault_types=fault_type)
                    report = validate_csv(output)
                    rules = {violation["rule"] for violation in report["rule_violations"]}
                    self.assertIn(expected_rule, rules)

    def test_fault_types_list_is_reproducible(self) -> None:
        fault_types = "voltage,duplicate,non_monotonic"
        with tempfile.TemporaryDirectory() as tmpdir:
            first = Path(tmpdir) / "first.csv"
            second = Path(tmpdir) / "second.csv"
            run_generator(first, records=30, seed=7, fault_types=fault_types)
            run_generator(second, records=30, seed=7, fault_types=fault_types)
            self.assertEqual(first.read_text(encoding="utf-8"), second.read_text(encoding="utf-8"))

    def test_inject_faults_cycles_all_injectors(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "mixed.csv"
            run_generator(output, records=30, seed=99, inject_faults=7)
            report = validate_csv(output)
            rules = {violation["rule"] for violation in report["rule_violations"]}
            self.assertTrue(
                {
                    "voltage_range",
                    "temperature_range",
                    "soc_range",
                    "timestamp_format",
                    "cell_id_required",
                    "duplicate_reading",
                    "non_monotonic_timestamp",
                }.issubset(rules)
            )

    def test_unknown_fault_type_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "bad.csv"
            result = subprocess.run(
                [
                    sys.executable,
                    str(GENERATOR),
                    "--output",
                    str(output),
                    "--records",
                    "5",
                    "--fault-types",
                    "not_a_fault",
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Unknown fault type", result.stderr)


if __name__ == "__main__":
    unittest.main()
