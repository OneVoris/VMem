#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.1.0'
LICENSE_ID = 'GPL-3.0-only'
RELEASE_DATE = '2026-06-24'


def read_text(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def definition_of_done_items(todo: str) -> list[str]:
    match = re.search(r'## Definition of Done\n(?P<body>.*)', todo, flags=re.S)
    if not match:
        return []
    return re.findall(r'^- \[(?P<mark>[ xX])\] (?P<text>.+)$', match.group('body'), flags=re.M)


def main() -> int:
    errors: list[str] = []

    xmake = read_text('xmake.lua')
    require('set_xmakever' not in xmake, 'xmake.lua must not pin a minimum XMake version', errors)
    require(f'set_version("{VERSION}")' in xmake, f'xmake.lua must set version {VERSION}', errors)
    require('--sanitize=address-undefined' in read_text('.github/workflows/ci.yml'),
            'CI must run an ASan+UBSan configuration', errors)
    require('--sanitize=thread' in read_text('.github/workflows/ci.yml'),
            'CI must run a TSan configuration', errors)

    package = tomllib.loads(read_text('voris-package.toml'))
    require(package.get('version') == VERSION, f'voris-package.toml version must be {VERSION}', errors)
    require(package.get('license') == LICENSE_ID,
            f'voris-package.toml license must be {LICENSE_ID}', errors)
    require('xmake_minimum' not in package, 'voris-package.toml must not constrain XMake', errors)

    version_header = read_text('include/voris/mem/version.hpp')
    require(f'library_version = "{VERSION}"' in version_header,
            f'library_version must be {VERSION}', errors)

    license_text = read_text('LICENSE')
    require('GNU GENERAL PUBLIC LICENSE' in license_text and 'Version 3' in license_text,
            'LICENSE must contain GPL version 3 text', errors)

    for relative in ('README.md', 'docs/LICENSING.md', 'docs/RELEASES.md'):
        text = read_text(relative)
        require('GPLv3' in text or 'GNU General Public License version 3' in text,
                f'{relative} must state the GPLv3 license', errors)
        require('commercial license' in text.lower(),
                f'{relative} must mention separate commercial licensing', errors)
        require('No license is selected' not in text and 'does not choose a license' not in text,
                f'{relative} must not describe the license as undecided', errors)

    changelog = read_text('CHANGELOG.md')
    require(f'## [{VERSION}] - {RELEASE_DATE}' in changelog,
            f'CHANGELOG.md must have a {VERSION} release section dated {RELEASE_DATE}', errors)
    require('[Unreleased]' not in changelog,
            'CHANGELOG.md must not leave v0.1.0 content under Unreleased', errors)

    migration = read_text('docs/MIGRATION.md')
    require('## v0.1.0' in migration, 'docs/MIGRATION.md must name v0.1.0', errors)
    require('Unreleased' not in migration, 'docs/MIGRATION.md must not call v0.1.0 unreleased', errors)

    todo = read_text('TODO.md')
    dod = definition_of_done_items(todo)
    require(len(dod) == 6, 'TODO.md must list six Definition of Done items', errors)
    require(all(mark.lower() == 'x' for mark, _ in dod),
            'all Definition of Done items must be checked', errors)

    evidence = read_text('docs/RELEASE_EVIDENCE.md') if (ROOT / 'docs/RELEASE_EVIDENCE.md').exists() else ''
    for required in (
        'Debug and Release',
        'ASan+UBSan',
        'TSan',
        'fuzz smoke',
        'benchmark smoke',
        'public header',
        'GPLv3',
    ):
        require(required in evidence, f'docs/RELEASE_EVIDENCE.md must record {required} evidence', errors)

    if errors:
        print('Release readiness check failed:')
        for error in errors:
            print(f'  - {error}')
        return 1

    print('Release readiness check passed.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
