#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import sys

THRESHOLDS_US = {
    'm6_page_source_roundtrip': 10_000_000,
    'm6_huge_page_prefer_fallback': 2_000_000,
    'm6_system_resource_aligned_64': 2_000_000,
}


def parse_line(line: str) -> tuple[str, dict[str, str]]:
    parts = [part.strip() for part in line.strip().split(',') if part.strip()]
    if not parts:
        return '', {}
    values: dict[str, str] = {}
    for part in parts[1:]:
        key, separator, value = part.partition('=')
        if separator:
            values[key] = value
    return parts[0], values


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Check VMem release benchmark smoke output against conservative thresholds.')
    parser.add_argument('output', type=Path, help='Benchmark output text file.')
    args = parser.parse_args()

    text = args.output.read_text(encoding='utf-8')
    seen: set[str] = set()
    errors: list[str] = []
    for line in text.splitlines():
        name, values = parse_line(line)
        if name not in THRESHOLDS_US:
            continue
        seen.add(name)
        try:
            micros = int(values['micros'])
        except (KeyError, ValueError):
            errors.append(f'{name}: missing numeric micros field')
            continue
        if micros > THRESHOLDS_US[name]:
            errors.append(f'{name}: {micros}us exceeds {THRESHOLDS_US[name]}us')

    missing = sorted(set(THRESHOLDS_US) - seen)
    errors.extend(f'{name}: missing benchmark line' for name in missing)

    if errors:
        print('Release benchmark threshold check failed:')
        for error in errors:
            print(f'  - {error}')
        return 1

    print('Release benchmark threshold check passed.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
