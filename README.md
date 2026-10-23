# Sky Circuit

Native PS3 port of the top-down precision driver. The road, car, ramps, speed bumps and HUD are drawn with [tiny3d](https://github.com/wargio/tiny3D) on PSL1GHT. It does not open the PS3 browser.

Built for **firmware 4.92** and **HEN 3.4.1** (Super Slim) as an NPDRM HDD-game package (`CATEGORY HG`, `PS3_SYSTEM_VER 04.9200`).

| | |
|---|---|
| Title ID | `SKYC00001` |
| Content ID | `UP0001-SKYC00001_00-0000000000000000` |
| Package | `build/skycircuit.pkg` (GitHub Actions artifact `sky-circuit-ps3`) |

## Install

1. Let the GitHub Action finish. The first run compiles the PS3 toolchain and takes roughly 30–40 minutes. Later runs reuse the cache.
2. Download `skycircuit.pkg` from the workflow artifacts.
3. On the PS3, enable HEN 3.4.1.
4. Install the pkg with HEN's package manager (or copy it to `packages/` on a USB stick and install from there).
5. Launch **Sky Circuit** under Games.

Official firmware will not run this. It is homebrew, not a Sony-signed retail package.

## Controls

| Action | Button |
|---|---|
| Steer | Left stick, or D-pad left / right |
| Gas | Cross or R2 |
| Reverse | Square or L2 |
| Handbrake | Circle or L1 |
| Burnout | Gas + reverse together. Release one pedal to launch. Hold too long and the car explodes. |
| Pause | Start |
| Menus | D-pad up / down, Cross to confirm |
| Half-width road | Triangle while paused, or the pause-menu entry |

Checkpoints save every five seconds while all four wheels are on the road.

## Crash / debug log (console)

On boot the game appends to **`skycircuit.log`** in both places when possible:

| Path | Notes |
|---|---|
| `/dev_hdd0/tmp/skycircuit.log` | Always preferred; survives without USB |
| `/dev_usb000/skycircuit.log` (…001…007) | First mounted USB root that accepts a write |

Pull the log after a black screen/freeze (multiman / FTP / USB) and open an issue with the last lines.

## Pipeline

- **`build.yml`** — cached `ps3toolchain` core, tiny3d, SELF/PKG. Artifact: `sky-circuit-ps3`.
- **`emu-bench.yml`** — builds the same package, downloads RPCS3 AppImage, boots `EBOOT.BIN` under Xvfb for ~45s, uploads `emu-bench-logs` (RPCS3 + game log tails).

Cairo/ps3libraries are not built; this game does not need them.
