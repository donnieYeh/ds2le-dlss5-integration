# Project bridge carrier

This directory contains the source for the project-maintained pre-SR NR
carrier. It is based on upstream `dlss5-bridge` commit `5050b04` plus the
project's verified `pre_sr_nr` implementation.

- validated carrier SHA-256: `E4DE11B7FA31C5FE7682CFF8F33C158C9FB9C11EE7DB62CE2524FC823A241374`
- carrier version: `1.4.12.0`
- tested path: same-resolution feature-18 NR followed by native feature-1 SR
- configuration: `pre_sr_nr=1` in `Game\\dlss5-bridge.cfg`

The ReShade SDK headers are not copied into this repository. `build.cmd`
accepts `DLSS5_RESHADE_INCLUDE`, which must point to a directory containing
the `reshade` header folder. GitHub Actions obtains a pinned header set in its
temporary workspace; neither those headers nor any third-party runtime is
included in the player package.
