#!/usr/bin/env python3
import json
import math
import sys
from pathlib import Path


def load_benchmark(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def stage_map(document: dict) -> dict:
    stages = {}
    for stage in document.get("stage_timings", []):
        stages[stage["name"]] = stage
    return stages


def fmt_delta(before: float, after: float) -> str:
    if before == 0.0:
        return "n/a"
    return f"{((after - before) / before) * 100.0:+.1f}%"


def print_summary(before: dict, after: dict) -> None:
    for key in ("cold_latency_ms", "avg_latency_ms", "total_duration_ms"):
        if key not in before or key not in after:
            continue
        print(f"{key}: {before[key]:.3f} -> {after[key]:.3f} ({fmt_delta(before[key], after[key])})")


def print_stage_table(before: dict, after: dict) -> None:
    before_stages = stage_map(before)
    after_stages = stage_map(after)
    stage_names = sorted(set(before_stages) | set(after_stages))

    rows = []
    for name in stage_names:
        old = before_stages.get(name, {})
        new = after_stages.get(name, {})
        old_total = float(old.get("total_ms", 0.0))
        new_total = float(new.get("total_ms", 0.0))
        delta = new_total - old_total
        rows.append((abs(delta), name, old_total, new_total, fmt_delta(old_total, new_total)))

    rows.sort(reverse=True)
    print("\nstage,total_before_ms,total_after_ms,delta_pct")
    for _, name, old_total, new_total, delta_pct in rows:
        print(f"{name},{old_total:.3f},{new_total:.3f},{delta_pct}")


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("Usage: compare_benchmarks.py <before.json> <after.json>", file=sys.stderr)
        return 1

    before_path = Path(argv[1])
    after_path = Path(argv[2])
    before = load_benchmark(before_path)
    after = load_benchmark(after_path)

    print(f"before: {before_path}")
    print(f"after:  {after_path}\n")
    print_summary(before, after)
    print_stage_table(before, after)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
