#!/usr/bin/env python3
"""Render a recruiter-friendly HTML validation report from bms-validate JSON output."""

from __future__ import annotations

import argparse
import json
from html import escape
from pathlib import Path


def render(report: dict) -> str:
    violations = report.get("rule_violations", [])
    rows = "".join(
        f"<tr><td>{escape(str(v.get('record_index', '')))}</td>"
        f"<td><code>{escape(str(v.get('cell_id', '')))}</code></td>"
        f"<td>{escape(str(v.get('rule', '')))}</td>"
        f"<td>{escape(str(v.get('detail', '')))}</td></tr>"
        for v in violations
    ) or "<tr><td colspan='4'>No rule violations — all records passed.</td></tr>"

    pass_rate = float(report.get("pass_rate", 0)) * 100
    total = report.get("total_records", 0)
    valid = report.get("valid_records", 0)
    invalid = report.get("invalid_records", 0)

    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>BMS Telemetry Validation Report</title>
  <style>
    :root {{ font-family: Inter, ui-sans-serif, system-ui, sans-serif; color: #14213d; background: #f4f7fb; }}
    body {{ max-width: 960px; margin: 0 auto; padding: 48px 24px; }}
    header {{ border-bottom: 4px solid #155e75; padding-bottom: 20px; margin-bottom: 28px; }}
    h1 {{ margin: 0; font-size: clamp(1.8rem, 4vw, 2.6rem); letter-spacing: -.03em; }}
    p {{ color: #52606d; line-height: 1.6; }}
    .metrics {{ display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 14px; margin: 24px 0 32px; }}
    .card {{ background: #fff; border: 1px solid #d9e2ec; border-radius: 12px; padding: 18px; }}
    .card span {{ display: block; color: #52606d; font-size: .85rem; font-weight: 600; text-transform: uppercase; letter-spacing: .05em; }}
    .card strong {{ display: block; font-size: 1.9rem; margin-top: 6px; }}
    table {{ width: 100%; border-collapse: collapse; background: #fff; border: 1px solid #d9e2ec; border-radius: 12px; overflow: hidden; }}
    th, td {{ text-align: left; padding: 12px 14px; border-bottom: 1px solid #e5edf5; }}
    th {{ background: #e7f3f5; color: #155e75; font-size: .78rem; text-transform: uppercase; letter-spacing: .05em; }}
    code {{ background: #e9eef5; padding: 2px 5px; border-radius: 4px; }}
    footer {{ margin-top: 28px; color: #6b7c93; font-size: .9rem; }}
    @media (max-width: 720px) {{ .metrics {{ grid-template-columns: repeat(2, minmax(0, 1fr)); }} }}
  </style>
</head>
<body>
  <header>
    <h1>BMS Telemetry Validation Report</h1>
    <p>Deterministic quarantine results for a synthetic cell telemetry batch. Invalid readings are withheld before downstream analytics.</p>
  </header>
  <section class="metrics" aria-label="Validation summary">
    <article class="card"><span>Total records</span><strong>{total}</strong></article>
    <article class="card"><span>Valid</span><strong>{valid}</strong></article>
    <article class="card"><span>Quarantined</span><strong>{invalid}</strong></article>
    <article class="card"><span>Pass rate</span><strong>{pass_rate:.1f}%</strong></article>
  </section>
  <section>
    <h2>Rule violations</h2>
    <table>
      <thead><tr><th>Index</th><th>Cell</th><th>Rule</th><th>Detail</th></tr></thead>
      <tbody>{rows}</tbody>
    </table>
  </section>
  <footer>Portfolio demonstrator — synthetic BMS telemetry only, not production hardware data.</footer>
</body>
</html>"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="JSON report from bms-validate")
    parser.add_argument("--output", type=Path, required=True, help="HTML output path")
    args = parser.parse_args()

    report = json.loads(args.input.read_text(encoding="utf-8"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render(report), encoding="utf-8")
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
