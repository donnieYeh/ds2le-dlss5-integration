# Notice and third-party boundaries

This repository contains the open-source DINPUT8 proxy, the project bridge
source, and build/packaging scripts written for this project. It does **not**
contain or redistribute:

- Dark Souls II or any game files;
- DS2LightingEngine/DS2LE;
- ReShade or its `dxgi.dll` host;
- `renodx-dlss5.addon64`;
- NVIDIA NGX/DLSS runtimes such as `nvngx_dlssnr.dll`.

The player package's `dlss5-bridge.addon64` is built from this repository's
bridge source. ReShade SDK headers are a build-only dependency fetched by CI
into a temporary directory and are not part of the repository or package.

The bridge source and carrier include project changes derived from
[NIGos/dlss5-bridge](https://github.com/NIGos/dlss5-bridge), commit `5050b04`,
under its MIT license. ReShade SDK headers, when fetched for a source build,
retain their upstream BSD-3-Clause/MIT license terms.

Those components remain the property of their respective authors and are
obtained by the player from the links in `README.md`. Their terms and licenses
apply independently. This project is not affiliated with or endorsed by
FromSoftware, Bandai Namco, NVIDIA, ReShade, RenoDX, DS2LightingEngine, or
the authors of the Bridge add-on.
