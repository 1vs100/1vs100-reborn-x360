#!/usr/bin/env python3
"""
Enforce the include layering of the restructuring plan ("Layout & layering"):

  platform/     platform/ and the XDK base headers (stdafx.h, crt_guard.h) only
  title/        title/, platform/
  config/       config/, platform/
  registry/     registry/, title/, platform/, config/
  patches/      patches/, registry/, title/, platform/, config/, matchmaking/MatchmakingClient.h
  diagnostics/  diagnostics/, registry/, title/, platform/, config/  (never patches/)
  matchmaking/  matchmaking/, platform/, config/
  core/         anything (it wires the layers together)

matchmaking/MatchmakingClient.h is the matchmaking module's public header; nothing else in
matchmaking/ may be included from outside it.
Exit 1 on a violation.  Usage: check-includes.py <sysdll dir>
"""
import pathlib
import re
import sys

BASE = {"stdafx.h", "crt_guard.h"}
MATCHMAKING_API = "matchmaking/MatchmakingClient.h"
ALLOWED = {
    "platform": {"platform"},
    "title": {"title", "platform"},
    "config": {"config", "platform"},
    "registry": {"registry", "title", "platform", "config"},
    "patches": {"patches", "registry", "title", "platform", "config"},
    "diagnostics": {"diagnostics", "registry", "title", "platform", "config"},
    "matchmaking": {"matchmaking", "platform", "config"},
}
USES_MATCHMAKING_API = {"patches"}
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "sysdll").resolve()
    bad = []
    for layer, allowed in ALLOWED.items():
        for path in sorted((root / layer).glob("*.[ch]*")):
            for inc in INCLUDE.findall(path.read_text(errors="replace")):
                target = (path.parent / inc).resolve()
                rel = target.relative_to(root) if target.is_relative_to(root) else None
                if rel is None:
                    bad.append(f"{path.relative_to(root)}: includes {inc}, outside sysdll/")
                elif len(rel.parts) == 1:
                    if rel.name not in BASE:
                        bad.append(f"{path.relative_to(root)}: includes {inc} (top-level module)")
                elif rel.parts[0] not in allowed and not (
                        str(rel) == MATCHMAKING_API and layer in USES_MATCHMAKING_API):
                    bad.append(f"{path.relative_to(root)}: includes {inc} ({layer}/ may not use {rel.parts[0]}/)")
    for b in bad:
        print(b)
    print(f"check-includes: {'OK' if not bad else str(len(bad)) + ' violation(s)'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
