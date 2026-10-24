#!/usr/bin/env python3
"""
No build branches outside sysdll/title/ (restructuring plan, rule R3 / VAR-1): patch code asks the
profile for sites (TitleSite) and policy (PolicySkips), it never names a build. Fails on any
reference to a concrete profile or a build-specific identifier outside title/.
Usage: check-build-branches.py <sysdll dir>
"""
import pathlib
import re
import sys

FORBIDDEN = re.compile(r"\b(kTitleProfile\d+|TITLE_BUILD_\w+|GetTitleBuild\w*|TitleBuild)\b")


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "sysdll").resolve()
    bad = []
    for path in sorted(root.rglob("*.[ch]*")):
        rel = path.relative_to(root)
        if rel.parts[0] == "title":
            continue
        for n, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            m = FORBIDDEN.search(line)
            if m:
                bad.append(f"{rel}:{n}: {m.group(1)} outside title/")
    for b in bad:
        print(b)
    print(f"check-build-branches: {'OK' if not bad else str(len(bad)) + ' violation(s)'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
