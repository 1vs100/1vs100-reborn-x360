#!/usr/bin/env python3
"""
Lint the LOG_* call sites against the loader's printf subset (platform/Format.h), which the
compiler cannot check. Flags:

  * a conversion count that differs from the argument count
  * %l / %ll (accepted but read as 32 bits: a genuine 64-bit argument misaligns the rest)
  * sizeof(...) passed to %d / %i (sizeof is unsigned: use %u)
  * conversions the formatter does not know (anything but d i u x X p s c %)
  * a trailing \\n (Log_Write adds the newline)

Imperfect by design (it reads source text, not types); it catches the bug classes the code
already had. Exit 1 on findings.  Usage: check-log-format.py <sysdll dir>
"""
import pathlib
import re
import sys

CALL = re.compile(r'\bLOG_(?:ERROR|WARN|INFO|DEBUG)\s*\(|\bLOG_AT\s*\(')
SPEC = re.compile(r'%([-0]*)(\d*)(l{0,2})([a-zA-Z%])')
KNOWN = set("diuxXpsc%")


def split_args(text, start):
    """Splits the call starting after '(' at text[start] into top-level arguments."""
    args, depth, cur, i, in_str = [], 0, "", start, None
    while i < len(text):
        c = text[i]
        if in_str:
            cur += c
            if c == "\\":
                cur += text[i + 1]
                i += 2
                continue
            if c == in_str:
                in_str = None
        elif c in "\"'":
            in_str = c
            cur += c
        elif c in "([{":
            depth += 1
            cur += c
        elif c in ")]}":
            if depth == 0:
                args.append(cur.strip())
                return args, i
            depth -= 1
            cur += c
        elif c == "," and depth == 0:
            args.append(cur.strip())
            cur = ""
        else:
            cur += c
        i += 1
    raise ValueError("unterminated call")


def literal_text(arg):
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', arg)
    return "".join(parts) if parts and re.fullmatch(r'(\s*"(?:[^"\\]|\\.)*"\s*)+', arg) else None


def check_file(path):
    text = path.read_text(errors="replace")
    findings = []
    for m in CALL.finditer(text):
        line = text.count("\n", 0, m.start()) + 1
        if text[max(0, m.start() - 8):m.start()].strip().endswith("define"):
            continue
        try:
            args, _ = split_args(text, m.end())
        except ValueError:
            continue
        is_at = m.group(0).startswith("LOG_AT")
        fmt_index = 2 if is_at else 1
        if len(args) <= fmt_index:
            continue
        fmt = literal_text(args[fmt_index])
        if fmt is None:
            continue  # a macro definition or a non-literal format
        values = args[fmt_index + 1:]
        specs = [s for s in SPEC.finditer(fmt) if s.group(4) != "%"]
        where = f"{path}:{line}"
        if len(specs) != len(values):
            findings.append(f"{where}: {len(specs)} conversion(s), {len(values)} argument(s): \"{fmt}\"")
        for s, value in zip(specs, values):
            conv = s.group(4)
            if s.group(3):
                findings.append(f"{where}: %{s.group(3)}{conv} reads 32 bits; split 64-bit values (%08X%08X)")
            if conv not in KNOWN:
                findings.append(f"{where}: %{conv} is not supported by platform/Format.cpp")
            if conv in "di" and value.startswith("sizeof"):
                findings.append(f"{where}: sizeof passed to %{conv}; use %u")
        if fmt.endswith("\\n"):
            findings.append(f"{where}: format ends in \\n; Log_Write adds the newline")
    return findings


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "sysdll")
    findings = []
    for path in sorted(root.rglob("*.cpp")) + sorted(root.rglob("*.h")):
        findings.extend(check_file(path))
    for f in findings:
        print(f)
    print(f"check-log-format: {'OK' if not findings else str(len(findings)) + ' finding(s)'}")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
