# Pre-SR NR carrier build

Built from `dlss5-bridge` commit `5050b04` with the project-maintained
`pre_sr_nr` bridge path. The resulting carrier was revalidated in a real game
scene on 2026-09-10.

- `pre_sr_nr=0` remains the default.
- `pre_sr_nr=1` creates a same-resolution feature-18 NR pass, inserts the
  intermediate color texture barrier, then evaluates native feature-1 SR.
- The bridge requires the measured RenoDX v4.6 layout and fails closed if the
  NR gate cannot be controlled.
- No file in the game installation was replaced or modified for this build.
- The validated carrier is `dlss5-bridge.addon64` with SHA-256
  `E4DE11B7FA31C5FE7682CFF8F33C158C9FB9C11EE7DB62CE2524FC823A241374`.
- The player package contains this project carrier and proxy only; DS2LE,
  ReShade, RenoDX and NVIDIA runtime files remain separate downloads.

The build output is named `dlss5-bridge.addon64` by the package workflow so it
matches the proxy's loader contract.
