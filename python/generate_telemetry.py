#!/usr/bin/env python3
"""Generate synthetic BMS cell telemetry CSV with optional fault injection."""

from __future__ import annotations

import argparse
import csv
import random
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Callable

FIELDS = ["timestamp_iso", "cell_id", "voltage_v", "temperature_c", "soc_percent"]
CELL_IDS = ["CELL-01", "CELL-02", "CELL-03"]


def iso_timestamp(base: datetime, offset_seconds: int) -> str:
    return (base + timedelta(seconds=offset_seconds)).strftime("%Y-%m-%dT%H:%M:%SZ")


def valid_record(base: datetime, index: int, cell_id: str) -> dict[str, str | float]:
    return {
        "timestamp_iso": iso_timestamp(base, index * 60),
        "cell_id": cell_id,
        "voltage_v": round(random.uniform(3.2, 4.1), 3),
        "temperature_c": round(random.uniform(15.0, 35.0), 2),
        "soc_percent": round(random.uniform(20.0, 95.0), 2),
    }


def inject_voltage_fault(record: dict[str, str | float]) -> None:
    record["voltage_v"] = 5.0


def inject_temperature_fault(record: dict[str, str | float]) -> None:
    record["temperature_c"] = 75.0


def inject_soc_fault(record: dict[str, str | float]) -> None:
    record["soc_percent"] = 110.0


def inject_bad_timestamp(record: dict[str, str | float]) -> None:
    record["timestamp_iso"] = "2024/06/01 12:00:00"


def inject_missing_cell_id(record: dict[str, str | float]) -> None:
    record["cell_id"] = ""


def inject_duplicate(records: list[dict[str, str | float]], index: int) -> None:
    if index > 0:
        records[index] = dict(records[index - 1])


def inject_non_monotonic(records: list[dict[str, str | float]], index: int) -> None:
    if index > 0:
        previous = records[index - 1]["timestamp_iso"]
        records[index]["timestamp_iso"] = previous
        records[index]["cell_id"] = records[index - 1]["cell_id"]
        earlier = datetime.strptime(str(previous), "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        records[index]["timestamp_iso"] = iso_timestamp(earlier, -120)


FAULT_INJECTORS: list[Callable[..., None]] = [
    inject_voltage_fault,
    inject_temperature_fault,
    inject_soc_fault,
    inject_bad_timestamp,
    inject_missing_cell_id,
]


def generate_records(count: int, fault_count: int, seed: int) -> list[dict[str, str | float]]:
    random.seed(seed)
    base = datetime(2024, 6, 1, 12, 0, 0, tzinfo=timezone.utc)
    records: list[dict[str, str | float]] = []

    for index in range(count):
        cell_id = CELL_IDS[index % len(CELL_IDS)]
        records.append(valid_record(base, index, cell_id))

    fault_indices = random.sample(range(count), min(fault_count, count))
    for fault_index, injector in zip(fault_indices, FAULT_INJECTORS * ((fault_count // len(FAULT_INJECTORS)) + 1)):
        if injector in (inject_duplicate, inject_non_monotonic):
            injector(records, fault_index)
        else:
            injector(records[fault_index])

    if fault_count > len(fault_indices):
        extra = fault_count - len(fault_indices)
        for offset in range(extra):
            target = (fault_indices[-1] + offset + 1) % count if fault_indices else offset % count
            if target > 0:
                inject_duplicate(records, target)

    return records


def write_csv(path: Path, records: list[dict[str, str | float]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(records)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="Output CSV path")
    parser.add_argument("--records", type=int, default=20, help="Number of telemetry records")
    parser.add_argument("--inject-faults", type=int, default=0, help="Number of faults to inject")
    parser.add_argument("--seed", type=int, default=42, help="Random seed for deterministic output")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    records = generate_records(args.records, args.inject_faults, args.seed)
    write_csv(args.output, records)
    print(f"Wrote {len(records)} records to {args.output} with {args.inject_faults} injected faults")


if __name__ == "__main__":
    main()
