# Patch switches

Every loader patch can be switched off for A/B testing without rebuilding. List the switch names
in the switch file:

    1vs100Reborn.disable    (next to the launcher, or on the HDD root)

Format: whitespace-separated names, case-insensitive. Any character other than `a-z`, `0-9` or
`_` counts as a separator, so one name per line works. Only the first 1023 bytes are read.

The launcher looks for it next to itself first (its `game:\` directory, resolved to a device
path), then on the HDD root (`\Device\Harddisk0\Partition1\1vs100Reborn.disable`), reads it and
hands its text to the loader with the config. `Launcher: 1vs100Reborn.disable found …`
shows which copy was used, the loader's `patch switches:` line what it holds, and a switched-off
patch reports `skipped, why=switch` in the patch manifest.

## When the file is read

* Once per launch, by the launcher, before it starts the game. Edits take effect on the next
  launch through the launcher. The kernel phase runs before the hand-over and sees no switches; it
  has no patches.

## Patch switches

**The full list is [`docs/patches.md`](docs/patches.md)**, generated from the patch registry: one
switch per patch, with its phase, the builds that do not get it and what it does. Names that
existed before the registry kept their spelling (`gridurl`, `gridtrace`, `banner`,
`xulumirror`, `nullsub1mirror`, `nullsub2mirror`, `nullsub5mirror`, `printsinkmirror`).

Keep `printsinkmirror` in the file for normal play when running the diagnostics build: the Lua log
volume stalls the title's timing and causes errors later in the session. The production build does
not contain the Lua mirrors at all.

## Behaviour switches inside hooks

These do not turn a patch off; the hook stays and only this behaviour changes.

| Name | Effect when listed | Code |
|------|--------------------|------|
| `zeroxuid` | Online-only queries may return XUID 0 for a local profile (no offline-XUID repair in `XamUserGetSigninInfo`) | `sysdll/patches/LiveIdentity.cpp` |
| `offlinexuid` | Online-only XUID queries fail for a local profile (1149: G012) | `sysdll/patches/LiveIdentity.cpp` |
| `liveflag` | The profile reports its real (local) sign-in state | `sysdll/patches/LiveIdentity.cpp` |
