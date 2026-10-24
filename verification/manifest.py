#!/usr/bin/env python3
"""
Patch manifest: the loader's per-launch record of which patches landed.

The loader logs one `[patch]` line per patch site (sysdll/PatchHelpers.h, ReportPatch) and
brackets each pass with `[patch] begin/end phase=<kernel|early|title> build=<name>`. This
script turns a captured debug log into a normalised manifest - one pass per block, lines
sorted inside it - and diffs manifests, so a change can be shown not to alter which patches
are applied (restructuring plan, "Verification strategy").

    manifest.py capture.log                      print the manifest
    manifest.py capture.log -o out.txt           write it
    manifest.py --diff baseline.txt current.txt  compare two manifests (exit 1 on difference)
    manifest.py --diff baseline/ current/        compare same-named files in two directories
"""
import argparse
import difflib
import pathlib
import re
import sys

PHASE_RE = re.compile(r"\[patch\] (begin|end|unsupported) phase=(\S+) build=(\S+)")
PATCH_RE = re.compile(
    r'\[patch\] id="([^"]*)" result=(\S+) site=([0-9A-Fa-f]{8}) why=(\S+)')
# Captures from the XBDM debug channel prefix every line with the emitting thread. Passes run on
# different threads (loader loop, title loader thread) and can interleave, so lines are attributed
# to the open pass of their own thread. Thread ids differ per boot and are not printed.
THREAD_RE = re.compile(r"thread=(0x[0-9A-Fa-f]+)")


def build_manifest(lines):
    """Returns the manifest as a list of text lines."""
    passes = []   # in begin order: dict(phase, build, entries, closed, overlaps)
    open_by_thread = {}

    def start(thread, phase, build):
        overlaps = sorted({p["phase"] for t, p in open_by_thread.items() if t != thread})
        for p in open_by_thread.values():
            if p is not open_by_thread.get(thread):
                p["overlaps"].add(phase)
        entry = {"phase": phase, "build": build, "entries": [], "closed": False,
                 "overlaps": set(overlaps), "unsupported": False}
        passes.append(entry)
        open_by_thread[thread] = entry
        return entry

    for line in lines:
        tm = THREAD_RE.search(line)
        thread = tm.group(1).lower() if tm else None
        m = PHASE_RE.search(line)
        if m:
            marker, phase, build = m.groups()
            if marker == "begin":
                start(thread, phase, build)
            elif marker == "end":
                cur = open_by_thread.get(thread)
                if cur is not None and cur["phase"] == phase:
                    cur["closed"] = True
                    del open_by_thread[thread]
            elif marker == "unsupported":
                entry = start(thread, phase, build)
                entry["closed"] = entry["unsupported"] = True
                del open_by_thread[thread]
            continue
        m = PATCH_RE.search(line)
        if m:
            name, result, site, why = m.groups()
            cur = open_by_thread.get(thread)
            if cur is None:
                cur = start(thread, "?", "?")
            cur["entries"].append(f"{name} | {result} | {site.upper()} | {why}")

    out = []
    for p in passes:
        head = f"pass phase={p['phase']} build={p['build']}"
        if p["unsupported"]:
            head += " unsupported"
        if not p["closed"]:
            head += " (no end marker: pass interrupted?)"
        if p["overlaps"]:
            head += " OVERLAPS " + ",".join(sorted(p["overlaps"]))
        out.append(head)
        out.extend(f"  {e}" for e in sorted(p["entries"]))
    return out


def read_manifest_or_log(path):
    text = path.read_text(errors="replace").splitlines()
    # A manifest written by this script starts with "pass "; anything else is a raw log.
    if text and text[0].startswith("pass "):
        return text
    return build_manifest(text)


def diff_files(a, b):
    left, right = read_manifest_or_log(a), read_manifest_or_log(b)
    delta = list(difflib.unified_diff(left, right, str(a), str(b), lineterm=""))
    for line in delta:
        print(line)
    return not delta


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", type=pathlib.Path)
    ap.add_argument("--diff", action="store_true", help="compare two manifests, logs or directories")
    ap.add_argument("-o", "--output", type=pathlib.Path)
    args = ap.parse_args()

    if args.diff:
        if len(args.inputs) != 2:
            ap.error("--diff takes exactly two paths")
        a, b = args.inputs
        if a.is_dir() and b.is_dir():
            same = True
            names = sorted({p.name for p in a.iterdir()} | {p.name for p in b.iterdir()})
            for name in names:
                if not (a / name).exists() or not (b / name).exists():
                    print(f"only in one side: {name}")
                    same = False
                    continue
                same = diff_files(a / name, b / name) and same
        else:
            same = diff_files(a, b)
        print("manifests identical" if same else "manifests differ", file=sys.stderr)
        return 0 if same else 1

    if len(args.inputs) != 1:
        ap.error("give one capture log (or use --diff)")
    text = "\n".join(build_manifest(args.inputs[0].read_text(errors="replace").splitlines()))
    if args.output:
        args.output.write_text(text + "\n")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
