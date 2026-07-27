# Solar 0.5.0

Solar 0.5.0 improves SAPHO project synchronization and generated waveform
inspection while preserving the format-2 project model and public CLI.

## Highlights

- `solar scan` now synchronizes CMM/SAPHO projects from the source `#PRNAME`,
  including project name, source path, processor name, and synthesis top.
- SAPHO waveform viewing gains a generated GTKWave layout derived from the
  validated Assembly output.
- Project validation reports actionable CMM processor-name mismatches.
- Existing Verilog scanning, builds, reports, viewers, and YANC pipelines
  remain supported.

## Install

```bash
curl -fsSL https://github.com/ART3121/solar/releases/download/v0.5.0/install.sh \
  | sh -s -- --version v0.5.0
```

Solar supports GNU/Linux x86_64. Icarus Verilog, Verilator, Yosys, GTKWave,
and Surfer remain external tools selected according to the requested flow.
