#!/usr/bin/env python3

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MARKDOWN_LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")


def project_markdown_files() -> list[Path]:
    files = [
        ROOT / "README.md",
        ROOT / "CONTRIBUTING.md",
        ROOT / "AGENTS.md",
        ROOT / "CLAUDE.md",
        ROOT / ".github" / "PULL_REQUEST_TEMPLATE.md",
    ]
    files.extend(sorted((ROOT / "docs").glob("*.md")))
    files.extend(sorted((ROOT / "help").glob("*.md")))
    return files


def check_agent_files(errors: list[str]) -> None:
    agents = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
    claude = (ROOT / "CLAUDE.md").read_text(encoding="utf-8")
    if agents != claude:
        errors.append("AGENTS.md and CLAUDE.md must stay identical.")


def check_markdown_links(errors: list[str]) -> None:
    for markdown_file in project_markdown_files():
        text = markdown_file.read_text(encoding="utf-8")
        for link in MARKDOWN_LINK_RE.findall(text):
            if "://" in link or link.startswith("#") or link.startswith("mailto:"):
                continue

            target = link.split("#", 1)[0]
            if not target:
                continue

            resolved = (markdown_file.parent / target).resolve()
            if not resolved.exists():
                rel_file = markdown_file.relative_to(ROOT)
                errors.append(f"{rel_file}: broken local link -> {link}")


def main() -> int:
    errors: list[str] = []
    check_agent_files(errors)
    check_markdown_links(errors)

    if errors:
        for error in errors:
            print(error)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
