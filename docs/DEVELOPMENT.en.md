# Development and release notes

[中文](DEVELOPMENT.md) · [English (this page)](DEVELOPMENT.en.md)

## Repository boundary

This repository owns the DINPUT8 proxy, the accepted single pre-SR NR carrier
source, its linker definition, configuration template, and packaging workflow.
It has no game files, DS2LE files, RenoDX binaries, or NVIDIA runtime binaries;
ReShade SDK headers are fetched only as a temporary build dependency.

## Build architecture

`src/proxy/dinput8.c` is compiled without the C runtime. It forwards the six
native DINPUT8 exports through `dinput8.def` and starts a worker after the
DS2LE/ReShade `dxgi.dll` host is present. The worker loads the Bridge and RenoDX
add-ons from the game directory. The proxy then supplies the missing ReShade
entry points, redirects the six LE stubs, and maintains the event slots that
LE resets during device initialization.

The carrier source lives under `src/bridge`. The scene-validated release
carrier is pinned under `build-artifacts/pre-sr-nr/` with SHA-256
`E4DE11B7FA31C5FE7682CFF8F33C158C9FB9C11EE7DB62CE2524FC823A241374`.
The configuration poke table is tied to RenoDX DLSS5 4.60. If a new add-on
changes its layout, the table must be re-derived and tested before release;
never hide that change behind an unqualified "latest" download.

`src/tests/fake_dxgi.c` is a tiny offline export fixture for linker/export
experiments. It does not emulate a game or certify runtime compatibility.

## Local checks

```powershell
cmd /c src\proxy\build.cmd
$env:DLSS5_RESHADE_INCLUDE = 'C:\path\to\include-root'
cmd /c src\bridge\build.cmd
.\tools\package-release.ps1 -Version local
git diff --check
```

Before opening a pull request, confirm that no extra `.dll`, `.addon64`, `.rar`,
or game archive has entered Git; the carrier is a build output, not a checked-in
binary. The workflow audits both the repository and the final ZIP.

## Release checklist

1. Test the exact DS2LE, carrier, RenoDX and DLSSNR versions listed in the
   README on a clean game-folder backup.
2. Record the tested GPU/driver and update the compatibility table.
3. Run the build and inspect the SHA-256 output.
4. Commit the change and create an annotated `vX.Y.Z` tag.
5. Push the branch and tag. The tag workflow publishes the zip and
   `SHA256SUMS.txt`.
6. In the GitHub Release notes, call out compatibility changes and known
   runtime limitations, distinguishing the project carrier from unbundled
   third-party runtimes.

## Design advice

- Prefer a new offset manifest and an explicit compatibility error over
  silently poking an unknown add-on build.
- Keep installation reversible: backup names should be outside the release
  archive and a future installer should never delete an existing game file.
- Treat logs as diagnostics, not as proof that a particular visual style is
  active. For regression reports, collect `dlss5-loader.log`, `ReShade.log`,
  `DirectXHook.log`, the exact tag, and the runtime hashes.
- Keep CI deterministic. Pin action major versions, build on a Windows runner,
  and publish only artifacts produced by the tagged commit.
