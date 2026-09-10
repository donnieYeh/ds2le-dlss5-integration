# Pre-SR NR build

Built locally on 2026-09-08 from `dlss5-bridge` commit `5050b04` with the
experimental `pre_sr_nr` bridge path.

- `pre_sr_nr=0` remains the default.
- `pre_sr_nr=1` creates a same-resolution feature-18 NR pass, inserts the
  intermediate color texture barrier, then evaluates native feature-1 SR.
- The bridge requires the measured RenoDX v4.6 layout and fails closed if the
  NR gate cannot be controlled.
- No file in the game installation was replaced or modified for this build.

The custom bridge is intentionally named `dlss5-bridge-pre-sr-nr.addon64` so
it cannot be confused with the currently installed `dlss5-bridge.addon64`.
