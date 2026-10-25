# Contributing

The loader patches a running game on a console, so a mistake can hang the console rather than fail
a test.

## Rules

| # | Rule | Checked by |
|---|------|------------|
| R1 | No CRT on any path reachable from a hook: use `platform/Text`, `platform/Format`, `platform/Log` | `/we4995`, `cmake/check-crt-calls.py` |
| R2 | Reads and writes of foreign code go through `platform/MemoryPatch` (`EnsureBytes`, `EnsureJump`) or the site helpers in `registry/PatchHelpers` | review |
| R3 | No build branches outside `title/`: ask the profile (`TitleSite`, policy rows) | `cmake/check-build-branches.py` |
| R4 | Every patch is a registry row with manifest id, switch name, phase, summary and, when it keeps per-title state, a reset | review, `docs/patches.md` |
| R5 | Investigation-only hooks live in `diagnostics/` | review |
| R6 | Shared mutable state names its writer and reader threads and its lock at the definition; locks are `platform/SpinLock` | review |
| R7 | No blocking (`Sleep`, network) in a patch install or in a hook on a title thread: long work belongs in the loader tick | review |
| R8 | Loops in `sysdll/` are `while` + `if`/`break`, with no work in a `for` header | review |
| R9 | A new 64-bit `/` or `%` needs a clean link audit | `cmake/check-crt-calls.py` |
| — | `LOG_*` format strings match their arguments | `cmake/check-log-format.py` |

Style: tabs, `.clang-format`. Win32/XDK types (`DWORD`, `BOOL`, `BYTE`). File-local functions and
data are `static`. `Module_Verb` for public functions in `platform/`. `g_` for file statics, `k` for
constants. Comments state the contract in the present tense; history goes in an ADR.

## Adding a patch

1. Add the site to every supported `title/TitleProfile*.cpp` with its original bytes
   (`SITE(id, address, bytes)`), and a `SiteId` in `title/TitleProfile.h`. A build that does not
   have the site leaves the row out. A function the patch only calls, or an address it only
   compares against, is a `SITE_REF` row (its entry bytes, long enough to occur once in the
   image), and the install lists it in `REQUIRE_SITES`.
2. Write the install in the feature's file under `patches/` (or `diagnostics/`) with
   `ApplyPokeSite` / `InstallJumpSite` / `HOOK_SITE`, reporting with `self->id`. Write a reset if
   the patch keeps state.
3. Add the row: manifest id, a `lower_snake` switch name, the phase, a one-line summary.
4. If a build should not get it although it has the site, add a `PolicyOff` row with the reason to
   that build's profile.
5. Regenerate the reference: `python3 cmake/gen-patch-docs.py .` (the build fails otherwise).
6. Record manifests for both builds (below). Exactly the new line(s) should appear.

Adding a title build: a new `title/TitleProfileNNNN.cpp`, its declaration and list entry in
`title/TitleDetect.cpp`, and the file in `CMakeLists.txt`. The manifest's `skipped why=site`
lines are then the reverse-engineering to-do list.

## Recording a patch manifest

1. Deploy the loader (`XEEDEE_HOST=<console> ./build.sh`, or copy `build/1vs100Reborn.xex`).
2. Capture the console's debug output (XBDM notifications) while launching the title, reach the
   lobby, then exit to the dashboard.
3. `python3 verification/manifest.py capture.log -o manifest-<build>-<launch>.txt`
4. `python3 verification/manifest.py --diff verification/expected/production/ my-manifests/`

Raw captures contain a XUID and LAN addresses: commit only derived manifests. See
[`verification/README.md`](verification/README.md).

## Pull requests

* Feature branches only; at most about 400 changed lines, excluding pure moves.
* Move commits change no logic (review with `--color-moved`). Fix commits say `fix:` and name the
  finding or issue.
* The description gives the manifest result: "manifest identical (both builds)", or a table that
  explains each changed line.
* Definition of done:
  - [ ] clean build of both presets (`xdk`, `xdk-diagnostics`) with all link checks, and the host
        tests;
  - [ ] manifests recorded for both builds, diff empty or explained;
  - [ ] no new `LOG_WARN`/`LOG_ERROR` without explanation;
  - [ ] new per-title state has a reset, new shared state a threads-and-lock comment;
* Changes to `platform/MemoryPatch`, `platform/HookCave`, `platform/SpinLock` or
  `cmake/crt-baseline.txt` need two reviewers: a mistake there hard-locks a console.
