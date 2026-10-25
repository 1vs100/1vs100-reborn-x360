# Verification

`manifest.py` turns a captured loader debug log into a **patch manifest**: for every patch pass
(`kernel`, `early`, `title`), the sorted list of `name | result | site | why` lines the loader
logged. Comparing manifests before and after a change shows that the change did not alter which
patches land. See the restructuring plan, chapter "Verification strategy".

## Recording a manifest

1. Build and deploy the loader (`build.sh`).
2. Capture the console's debug-string output (XBDM notification channel) to a file.
3. Launch the title, reach the lobby, exit to the dashboard.
4. `python3 verification/manifest.py capture.log -o manifest-<build>-<launch>.txt`

A log file from the HDD (`Hdd:\1vs100Reborn\logs\run-NNNNNN.log`) has the same line
shape as a capture and can be passed instead; with `[log] level=info` it lacks the debug lines a
diagnostics capture has, so record manifests with the level the baseline was taken at.

Record one per combination: build `1149` / `1317` × launch `dash` / `guide`.

## Comparing

    python3 verification/manifest.py --diff verification/baseline/ my-run/

Exit code 0 means identical. In a pull request, either state "manifest identical" or explain
each changed line.

## Reading a manifest

| result | meaning |
|--------|---------|
| `installed` | bytes written / hook placed this pass |
| `already` | found already in place |
| `skipped` | not applicable: `why=site` (no such address in this build), `build`, `switch`, `no_match` (import slot not found: not imported, or another hook owns it) |
| `refused` | signature mismatch: nothing written |
| `failed` | could not complete: `why=hook` (cave/write), `verify`, `resolve`, `unresolved` |

`site` is the patched address, or the XAM/kernel ordinal for import and export hooks.

Lines are attributed to the pass of the thread that logged them (the capture's `thread=`
field), so passes on different threads that interleave are still separated. A pass header
ending in `OVERLAPS <phase>` means another thread ran a patch pass at the same time: the CON-1
race (two threads hooking the same site). It must not appear once single-flight is in place.

## Baseline

Recorded 2026-10-21 on this branch (`task/tool-1-patch-manifest`), through the launcher. Only
the derived manifests are committed; the raw captures contain a XUID and LAN addresses.

| File | Build | Launch |
|------|-------|--------|
| `manifest-1149-launcher-package.txt` | v2.0.1149.0 | launcher, package mode |
| `manifest-1317-launcher-directory.txt` | v2.0.1317.0 | launcher, directory mode (loose XEX) |

What the baseline shows:

* both builds: the early pass (title thread) overlaps the title pass (loader thread); on 1149 both
  installed the four avatar-init hooks on the same sites (CON-1, observed);
* 1317: `XMsgInProcessCall (Launch content fallback) | skipped | 000001F4 | no_match` - the
  content enumeration fallback never installed (BUG-11, observed); the title then failed with
  `DEVICE_ID, PKG(launch) ERC(0x00000012)` (G014).

The 1317 manifest was re-recorded later on 2026-10-21 with the Phase 1 fixes
(`fix/printsinkmirror-lua-log-write`, switch file containing `printsinkmirror`). The title reached
matchmaking. Against the first recording:

* `Content enumeration fallback | installed | 000001F4 | dispatch` (BUG-11 fixed);
* `ContentId UserIndex fix revert to 0xFF` is gone (BUG-10);
* no `OVERLAPS`: the early pass logs `another pass in progress, skipped` and the avatar-init
  diagnostics install once, in the title pass (CON-1 fixed);
* `LuaLogWrite | skipped | 00000000 | switch`: keep `printsinkmirror` in the switch file, the
  Lua log volume stalls the title;
* the capture starts earlier, so the `kernel` pass is recorded too.

Both manifests were recorded again on 2026-10-21/22 with the Phase 2 stack
(`docs/status-phase-2`). 1317 is identical to its re-recorded baseline. The 1149 manifest was
re-recorded from that run; against the first recording:

* no `OVERLAPS`: the early pass is skipped and the four avatar-init hooks install once
  (`avatar init device log` is now `installed`, not `refused` by the second thread) (CON-1);
* `ContentId UserIndex fix` is gone (BUG-10, YAG-2);
* `LuaLogWrite | skipped | 00000000 | switch` (`printsinkmirror`).

## Expected manifests after Phase 4

`verification/expected/` holds what a run of the current code should record, derived from the
baselines (not recorded on hardware):

| Directory | Build | Derivation |
|-----------|-------|------------|
| `expected/production/` | `xdk` preset | the baseline minus the diagnostics-only ids of `docs/patches.md` and the removed XAM patches, plus `NtClose (LSP enumerator close)` (BUG-3) |
| `expected/diagnostics/` | `xdk-diagnostics` preset | the baseline minus the removed XAM patches, plus `NtClose (LSP enumerator close)` |

The NtClose line assumes the title imports `NtClose` directly; if a run shows it as
`skipped | no_match`, the title closes handles another way and the BUG-3 gap remains. Every other
difference from these files is unexpected. Once a hardware run confirms them, they become the new
baselines.
