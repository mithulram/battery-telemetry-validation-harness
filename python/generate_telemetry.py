#!/usr/bin/env python3
"""Generate synthetic BMS cell telemetry CSV with optional fault injection."""

from __future__ import annotations

import argparse
import csv
import random
import sys
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
        records[index]["cell_id"] = records[index - 1]["cell_id"]
        earlier = datetime.strptime(str(previous), "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        records[index]["timestamp_iso"] = iso_timestamp(earlier, -120)


RecordInjector = Callable[[dict[str, str | float]], None]
BatchInjector = Callable[[list[dict[str, str | float]], int], None]

FAULT_TYPES: dict[str, RecordInjector | BatchInjector] = {
    "voltage": inject_voltage_fault,
    "temperature": inject_temperature_fault,
    "soc": inject_soc_fault,
    "bad_timestamp": inject_bad_timestamp,
    "missing_cell_id": inject_missing_cell_id,
    "duplicate": inject_duplicate,
    "non_monotonic": inject_non_monotonic,
}

BATCH_FAULT_TYPES = {"duplicate", "non_monotonic"}


def apply_injector(
    injector: RecordInjector | BatchInjector,
    records: list[dict[str, str | float]],
    index: int,
) -> None:
    if injector.__name__ in {"inject_duplicate", "inject_non_monotonic"}:
        injector(records, index)  # type: ignore[call-arg]
    else:
        injector(records[index])  # type: ignore[call-arg]


def parse_fault_types(raw: str | None) -> list[str] | None:
    if raw is None:
        return None
    fault_types = [part.strip() for part in raw.split(",") if part.strip()]
    if not fault_types:
        raise ValueError("--fault-types must list at least one fault type")
    unknown = sorted({name for name in fault_types if name not in FAULT_TYPES})
    if unknown:
        raise ValueError(f"Unknown fault type(s): {', '.join(unknown)}")
    return fault_types


def choose_fault_indices(count: int, fault_count: int, seed: int) -> list[int]:
    rng = random.Random(seed)
    sample_size = min(fault_count, count)
    return sorted(rng.sample(range(count), sample_size))


def generate_records(
    count: int,
    fault_count: int,
    seed: int,
    fault_types: list[str] | None = None,
) -> list[dict[str, str | float]]:
    random.seed(seed)
    base = datetime(2024, 6, 1, 12, 0, 0, tzinfo=timezone.utc)
    records: list[dict[str, str | float]] = []

    for index in range(count):
        cell_id = CELL_IDS[index % len(CELL_IDS)]
        records.append(valid_record(base, index, cell_id))

    if fault_types is not None:
        injectors = [FAULT_TYPES[name] for name in fault_types]
        fault_indices = choose_fault_indices(count, len(injectors), seed + 1)
        for fault_index, injector in zip(fault_indices, injectors):
            apply_injector(injector, records, fault_index)
        return records

    if fault_count <= 0:
        return records

    injectors = list(FAULT_TYPES.values())
    fault_indices = choose_fault_indices(count, fault_count, seed + 1)
    for offset, fault_index in enumerate(fault_indices):
        injector = injectors[offset % len(injectors)]
        apply_injector(injector, records, fault_index)

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
    parser.add_argument(
        "--fault-types",
        default=None,
        help="Comma-separated fault types to inject (voltage,temperature,soc,bad_timestamp,"
        "missing_cell_id,duplicate,non_monotonic)",
    )
    parser.add_argument("--seed", type=int, default=42, help="Random seed for deterministic output")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    try:
        fault_types = parse_fault_types(args.fault_types)
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc

    if fault_types is not None:
        fault_count = len(fault_types)
    else:
        fault_count = args.inject_faults

    records = generate_records(args.records, fault_count, args.seed, fault_types)
    write_csv(args.output, records)
    if fault_types is not None:
        print(
            f"Wrote {len(records)} records to {args.output} with fault types: "
            f"{', '.join(fault_types)}"
        )
    else:
        print(f"Wrote {len(records)} records to {args.output} with {fault_count} injected faults")


if __name__ == "__main__":
    main()
