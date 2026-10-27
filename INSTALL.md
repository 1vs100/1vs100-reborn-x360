# Installing 1 vs 100 Reborn

Here's how you can play the game >1 vs 100< again!

- brought to you by Avcracy and tuxuser

## What you need

- An Xbox 360 that runs unsigned code, with [DashLaunch](https://consolemods.org/wiki/Xbox_360:DashLaunch) installed.
- The Avatar data on the console. See [Installing the Avatar Data](https://consolemods.org/wiki/Xbox_360:Updating_your_Firmware/en#Installing_the_Avatar_Data).
- The 1 vs 100 game package and all show and episode packages listed below.

Tested with:

- 1 vs 100 game build 2.0.1149.0
- Kernel 2.0.17559.0
- DashLaunch with `xhttp` and `sockpatch` enabled (the launcher switches both on at start if `launch.ini` leaves them off)

## Steps

1. Create a folder on the console, for example `Hdd:\Games\1vs100Reborn`.
2. Copy `launcher.xex` into that folder and rename it to `default.xex`.
3. Copy `1vs100Reborn.xex` and `1vs100Reborn.ini` into the same folder.
4. Copy the game package and all show and episode packages to the console (see the next section).
5. Sign in with your profile on the first controller.
6. Start `default.xex` from your file manager or dashboard. The game starts by itself.

If the launcher shows an error, it tells you what is missing.

## Where the game files go

All of these go on the console hard drive.

| What | Folder on the console |
|------|------------------------|
| 1 vs 100 game, show and episode DLCs | `Hdd:\Content\0000000000000000\584A07D1\00000002\` |

## Content ids

Check a file before you copy it. Its SHA-256 checksum must match the table. On a PC you get it with `sha256sum <file>` on Linux, or `certutil -hashfile <file> SHA256` on Windows.

| Content id | Package | Language | SHA-256 |
|------------|---------|----------|---------|
| `2D23F392B51E38B1A93FA1E1B9FAE7B2C5DBBD6D58` | 1 vs 100 game | All | `48b869edbb84966934e3dfa350c64d2b9940f00813f3337707997f5f8cbf9180` |
| `988237803712CB87513237A914D367C6AAEE2410` | Launch package | All | `2ed2424e8890af3c6d115d351de4c6933e6bb61dfbac6418c39c0e06170e63d8` |
| `77205BC1241C708A8A10B7303AF90E5EDD0286B1` | Show | English | `a5d957cbcd75df198953a2299606eba54b37746e4ce1d877e5e1aa42848b9dcb` |
| `7BEC809CCC282E8C9D18EFAD8F4407AA6204DFC8` | Show | English | `119e053d95c30d4fa4834695df2081b6bcecc5736eb99c13f869aad51973921e` |
| `79E60F7D68C480ABF11E8834A947B6D77E9AACD8` | Show | French | `0c5354f057090745c4d1f3fd6de68e95142026c2ea0392d79ebeedd51f8d7d5b` |
| `9AF84C64F8D3C775A4C4C35F97674500FCF507A6` | Show | German | `15a35d299d6407e4e67c9044420f2070de9bda67b695c68f3f2a7c6b96bfe915` |
| `83136FF343124795DC1806A2DB044476589BD5C0` | Episode | English | `55828a8d8f1ad7fbf36299074f474bdb0c24d5ec034fae6993323d5287754e75` |
| `B30A40EE667432FA68559FA37B929C29E7985E56` | Episode | English | `025122310aa92aebf3d2b03a758f25a5ac5d2c9b40a66b8ec3e6aede9284ed63` |
| `C965577112F783E8E7F33E8C957263E03DEDB7F7` | Episode | French | `a70aeb4a214d53a69e93ca9c81046f52390d1f877359b66fc09df232d460ef94` |
| `FA6873618900F1613CD72C0FCA3C4B57C8BB02E8` | Episode | German | `d2652a491fbb4c8fe74c5c46b7fb022f6d903a6bb65172598d7ac0f582666bbf` |

## Support

If you are running into issues

- Check for an updated build on https://github.com/1vs100/1vs100-reborn-x360
- Check the issues / discussions sections of the Github repositories
- Visit our Discord: https://discord.gg/tV3ajhwFTf

## Disclaimer

1 vs 100, Xbox, Xbox 360 and Xbox LIVE are trademarks of their owners. This is an unofficial fan project, not affiliated with or endorsed by Microsoft or the game's developers.
