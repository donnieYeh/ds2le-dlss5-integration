# DS2LE DLSS5 Integration

[中文](README.md) · [English (this page)](README.en.md)

An open-source DINPUT8 proxy that connects the DS2LightingEngine (DS2LE)
DirectX 11 DLSS path to the community DLSS Neural Rendering (DLSSNR) stack.
It is aimed at **Dark Souls II: Scholar of the First Sin** players who already
use the DS2LE Path Tracing build. A release also carries the accepted
single-pass pre-SR NR carrier from `9ae8e89`.

The project builds the proxy and packages the accepted carrier. The proxy loads
the carrier and RenoDX add-ons,
fills the ReShade API surface that DS2LE's host omits, and keeps the add-on
callbacks alive while the game initializes. It does not contain DS2LE, the
game, ReShade, or any NVIDIA/community runtime.

## What is in a release?

Each GitHub Release contains:

- `DINPUT8.dll` — this project's proxy;
- `dlss5-bridge.addon64` — the accepted single pre-SR NR carrier from `9ae8e89`;
- `ReShade.ini.example` — settings to merge into the game's existing file;
- `dlss5-bridge.cfg.example` — the single pre-SR NR setting;
- `PRE-SR-NR-BUILD.md` — carrier build record;
- `README.md` / `README.en.md`, `INSTALL.txt` / `INSTALL.en.txt`, `LICENSE`, and `NOTICE.md`.

The release archive does not redistribute DS2LE, ReShade, RenoDX, or NVIDIA
runtime files. Those remain under their own terms and must be obtained from
their upstream pages below.

## Requirements

- Windows 10/11 x64.
- The Steam 64-bit edition of *Dark Souls II: Scholar of the First Sin*.
- DS2LightingEngine (DS2LE) with its Path Tracing build installed first. Get it
  from the [DS2LightingEngine Nexus page](https://www.nexusmods.com/darksouls2/mods/1146)
  and follow that project's installation instructions.
- An NVIDIA RTX GPU and a DLSSNR runtime build that supports that GPU. The
  tested baseline is RTX 40 + `310.8.0-RTX40`.

### Tested compatibility baseline

| Component | Tested value |
| --- | --- |
| DS2LE | Path Tracing public build `0.1` |
| RenoDX DLSS5 | `4.60` (the add-on commonly called v4.6) |
| DLSS5 Bridge | This project's single pre-SR NR carrier, based on `v1.4.12` / `5050b04` |
| DLSSNR | `310.8.0-RTX40` |
| GPU / driver | RTX 4070 SUPER / NVIDIA 616.64 |

The proxy contains version-specific offsets for the tested RenoDX build. Do
not silently replace `renodx-dlss5.addon64` with another build and assume it is
compatible. Treat an add-on upgrade as a new compatibility test.

## Runtime files to obtain separately

Download these files yourself and keep their original release notes and
licenses:

| File | Source | Where it goes |
| --- | --- | --- |
| DS2LE Path Tracing files, including the ReShade `dxgi.dll` host | [DS2LightingEngine on Nexus](https://www.nexusmods.com/darksouls2/mods/1146) | Install the complete DS2LE package as documented by its author |
| `renodx-dlss5.addon64` (`4.60`) | [RankFTW/rhi-repo — RenoDX DLSS5 4.60](https://github.com/RankFTW/rhi-repo/releases/tag/renodx-dlss5-4.60) | `Game\` |
| `nvngx_dlssnr.dll` (`310.8.0-RTX40`) | [RankFTW/rhi-repo releases](https://github.com/RankFTW/rhi-repo/releases) | `Game\` |

The release's `dlss5-bridge.addon64` is the project's accepted single pre-SR NR
carrier. It is based on upstream Bridge `v1.4.12` / commit `5050b04` and is
already named for the proxy's loader; do not download a second Bridge copy.

The DS2LE package's own `nvngx_dlss.dll`, `nvngx_dlssd.dll`, and
`nvngx_dlssg.dll` are not replaced by this project. The proxy only adds the
DLSSNR path. Keep exactly one copy of each add-on in `Game\`.

## Installation

1. Close the game and Steam.
2. Install and launch DS2LE once so its `Game\dxgi.dll` and ReShade setup are
   known to work. Confirm that the game starts before adding this project.
3. Back up the existing `Game\DINPUT8.dll`, `Game\dxgi.dll`, and
   `Game\ReShade.ini` outside the game folder.
4. Rename the original game input proxy to `Game\dinput8_orig.dll`. This is
   required because the new proxy forwards the game's six native DINPUT8
   exports to that filename.
5. Copy `DINPUT8.dll` from the release archive into `Game\`.
6. Copy the package's `dlss5-bridge.addon64`, plus `renodx-dlss5.addon64` and
   the matching `nvngx_dlssnr.dll` downloaded from the upstream links, into the
   same `Game\` folder.
7. Merge `pre_sr_nr=1` from `dlss5-bridge.cfg.example` into
   `Game\dlss5-bridge.cfg`; preserve the Bridge's other settings.
8. Open the existing `Game\ReShade.ini` and merge the sections from
   `ReShade.ini.example`. Do not overwrite the whole file; preserve your DS2LE
   settings. The important tested values are:

   ```ini
   [RenoDX.DLSS5]
   EnableHooks=2
   NRStyle=1
   NREnableUpscaling=0
   NeuralUplift=1

   [DLSS5Proxy]
   EventsBridge=0
   EventsRenodx=1
   EventsDxgi=1
   ```

9. Start the game with its normal Steam shortcut. In the DS2LE F1 menu, keep
   the game's antialiasing method on **NVIDIA DLSS**. Set the final resolution
   and display mode before enabling neural rendering.

The proxy writes `dlss5-loader.log` beside the executable. A successful run
also records DLSSNR initialization in `ReShade.log` and DLSS5 feature activity
in `DirectXHook.log`/`dlss5-bridge.log`.

## In-game controls

- **F1** — DS2LE settings menu (owned by DS2LE).
- **F2** — this project's small live configuration window.
- **F5** — RenoDX screenshot key (hold for about one second).
- **F6** — toggle neural rendering (hold for about one second).

The F2 panel writes the RenoDX values to `ReShade.ini`. Enable the single
pre-SR NR pass with `pre_sr_nr=1` in the Bridge config: it evaluates same-size
feature 18 first and native feature 1 second. The tested RTX 40 runtime can
reject the upscaling contract (`0xBAD00005`) and then fall back to native
neural rendering. That is expected on the tested hardware, not a reason to
replace the runtime with an unverified DLL.

## Troubleshooting and rollback

**The game crashes during startup or when entering a scene**

- Confirm that there is only one `dlss5-bridge.addon64` and one
  `renodx-dlss5.addon64`.
- Confirm that the Bridge file is the package's accepted pre-SR NR carrier,
  not an unverified upstream Bridge replacement.
- Confirm that the RenoDX add-on is the tested `4.60` build.
- Keep `[DLSS5Proxy] EventsBridge=0`; bridge event callbacks are not safe in
  the current LE host even though the Bridge's frame-mirroring path works.
- Set `NRStyle=0` temporarily, then inspect the fresh log files.

**No DLSSNR activity appears in the logs**

- Check that DS2LE's `dxgi.dll` is still in `Game\` and that the original game
  DLL is named `dinput8_orig.dll`.
- Verify that the game is using NVIDIA DLSS in the F1 menu.
- Recheck each third-party file's architecture (all must be 64-bit) and keep
  the add-ons directly in `Game\`, not in a subfolder.

**Returning to the unmodified setup**

1. Close the game.
2. Remove this project's `DINPUT8.dll` and the package's `dlss5-bridge.addon64`.
3. Restore the original `DINPUT8.dll` and your backed-up `ReShade.ini` (and
   `dxgi.dll` if you changed it). The game files themselves are not modified by
   this project.

## Build from source

The proxy is CRT-free C and uses the Microsoft x64 compiler plus GNU `ld` for
the DINPUT8 forwarders.

Prerequisites:

- Visual Studio 2022 C++ x64 build tools;
- [MSYS2](https://www.msys2.org/) UCRT64 `binutils` (`ld.exe`).

From a Developer PowerShell or ordinary PowerShell prompt:

```powershell
cmd /c src\proxy\build.cmd
```

The script writes `src\proxy\DINPUT8.dll`. The accepted single-pass carrier is
kept under `build-artifacts\pre-sr-nr\` and is verified and packaged by CI; it
is not re-patched locally from an unpinned Bridge binary. The script locates
Visual Studio with `vswhere` and accepts `DLSS5_LD`/`DLSS5_LIB` environment
overrides, which is how the CI job supplies the MSYS2 linker.

## GitHub Actions releases

`.github/workflows/build-release.yml` runs on pull requests and on `v*` tags.
It compiles the proxy on a Windows runner, verifies the accepted carrier hash,
rejects accidental bundled runtime binaries, creates a player zip containing
both project runtime files and a SHA-256 manifest, and uploads the zip as a
published GitHub Release asset for a tag.

To publish a release after pushing the repository to GitHub:

```powershell
git add .
git commit -m "Prepare release"
git tag -a v0.1.0 -m "Initial player release"
git push origin main --follow-tags
```

Use a new semantic version tag for each compatibility change. If the RenoDX
layout or DS2LE host changes, update the pinned baseline and retest before
cutting a release.

## Development notes

- The proxy edits the loaded LE `dxgi.dll` export table in memory and writes a
  small event dispatch table. This is intentionally narrow and version-bound;
  it is not a general ReShade compatibility layer.
- The source is kept separate from the proprietary runtime files so a clean
  checkout is reproducible and the release archive has a clear license
  boundary.
- The most useful future improvements are automated smoke tests for the PE
  exports, a versioned offset manifest for each RenoDX build, and a small
  installer that performs backups without downloading third-party binaries.

## Credits and licensing

The proxy source is MIT-licensed; see [LICENSE](LICENSE). Third-party
components retain their own terms. See [NOTICE.md](NOTICE.md) for the
non-redistribution boundary.

- [DS2LightingEngine](https://www.nexusmods.com/darksouls2/mods/1146)
- [NIGos/dlss5-bridge](https://github.com/NIGos/dlss5-bridge)
- [RankFTW/rhi-repo](https://github.com/RankFTW/rhi-repo)
- [ReShade](https://reshade.me/)
