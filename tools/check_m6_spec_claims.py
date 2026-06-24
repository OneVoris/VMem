#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

errors: list[str] = []

todo = (ROOT / 'TODO.md').read_text(encoding='utf-8')
if 'target-runner runtime validation remains release evidence' not in todo:
    errors.append('TODO.md must avoid claiming arm64 runtime validation without a target runner')

testing = (ROOT / 'docs/TESTING.md').read_text(encoding='utf-8')
if re.search(r'M6 platform and release tests cover[^\n]*(example builds|release benchmark smoke alerts)', testing):
    errors.append('docs/TESTING.md must not describe optional examples/benchmarks as normal tests')
if 'Release verification, outside normal `xmake test`' not in testing:
    errors.append('docs/TESTING.md must document optional release verification commands separately')

platform_sim_sources = [
    ROOT / 'tests/m6_platform_assumption_x86_64.cpp',
    ROOT / 'tests/m6_platform_assumption_arm64.cpp',
]
if not all(path.exists() for path in platform_sim_sources):
    errors.append('missing preprocessor simulation coverage for x86_64 and arm64 platform assumptions')

source = (ROOT / 'src/page_source.cpp').read_text(encoding='utf-8')
if '/proc/meminfo' not in source or 'Hugepagesize:' not in source:
    errors.append('Linux huge_page_size() must query /proc/meminfo Hugepagesize before fallback')
if 'linux_default_huge_page_size = 2U * 1024U * 1024U' in source:
    errors.append('Linux huge_page_size() still uses the old hard-coded 2 MiB implementation')

platform = (ROOT / 'include/voris/mem/platform.hpp').read_text(encoding='utf-8')
if 'cache_line_assumption_validated' in platform:
    errors.append('platform.hpp must not expose runtime-sounding cache_line_assumption_validated')
if 'cache_line_assumption_available' not in platform:
    errors.append('platform.hpp must expose cache_line_assumption_available')

if errors:
    print('M6 spec-claim check failed:')
    for error in errors:
        print(f'  - {error}')
    sys.exit(1)

print('M6 spec-claim check passed.')
