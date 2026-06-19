# Battery Telemetry Validation Harness — Architecture

## Overview

This harness validates synthetic battery management system (BMS) cell telemetry before records reach downstream analytics or safety-critical control logic. It provides deterministic, rule-based quarantine of bad readings.

```
┌─────────────────────┐     ┌──────────────────┐     ┌─────────────────────┐
│ Python generator    │────▶│  CSV / JSON I/O  │────▶│  C++ validator core │
│ (fault injection)   │     │  (bms-validate)    │     │  (7 deterministic   │
└─────────────────────┘     └──────────────────┘     │   rules)            │
                                                        └──────────┬──────────┘
                                                                   │
                                                                   ▼
                                                        ┌─────────────────────┐
                                                        │  JSON validation    │
                                                        │  report + quarantine│
                                                        │  indices            │
                                                        └─────────────────────┘
```

## Components

### C++ validator (`cpp/`)

- **`validator.hpp` / `validator.cpp`** — Core validation engine with no external runtime dependencies beyond the C++ standard library.
- **`main.cpp`** — CLI entry point (`bms-validate`) that loads records, runs validation, and writes a JSON report.
- **`tests/test_validator.cpp`** — GoogleTest unit tests covering each rule and CSV I/O.

### Validation pipeline

1. **Field-level checks** — Required fields, ISO-8601 timestamp format, numeric range checks for voltage, temperature, and SOC.
2. **Batch-level checks** — Duplicate `(timestamp_iso, cell_id)` detection and per-cell non-monotonic timestamp detection.
3. **Report generation** — Aggregates pass rate, rule violations, and quarantined record indices.

### Python generator (`python/`)

Generates mostly valid telemetry with configurable fault injection (out-of-range values, bad timestamps, duplicates, non-monotonic sequences). Uses a fixed random seed by default for reproducible CI runs.

## Data flow

| Stage | Input | Output |
|-------|-------|--------|
| Generate | CLI flags | `examples/generated.csv` |
| Validate | CSV or JSON records | `report.json` |
| Test | Sample + generated data | CTest pass/fail |

## Scope note

This project uses **synthetic telemetry only**. It demonstrates validation patterns relevant to automotive BMS QA workflows but does not interface with production ECU firmware, CAN buses, or live cell hardware.
