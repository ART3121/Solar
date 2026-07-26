# Solar 0.4.6

Solar 0.4.6 is a compatible reporting and terminal-experience update for the
lightweight open hardware design platform and EDA workflow orchestrator.

## Highlights

- Generic Synthesis Statistics from Yosys, including design totals, generic cell
  usage, a typed Solar Core API, and preservation of the original text artifact.
- Immutable build-report history with `solar report list`, `show`, and
  overflow-safe `compare` operations for synthesis statistics and simulation
  wall times.
- Technical-context warnings when builds use different sources, profiles, tool
  versions, simulation settings, or execution environments.
- Bash, Zsh, and Fish completion for commands plus dynamic tests, profiles,
  waveforms, and stored build IDs.
- Live simulation progress inside `solar build full`, with TTY redraw, linear CI
  output, `--no-progress`, and `--verbose`; synthesis begins only after the
  simulation display closes.

## Install

```sh
curl -fsSL https://github.com/ART3121/solar/releases/latest/download/install.sh | sh
```

The prebuilt asset targets GNU/Linux x86_64 with glibc 2.35 or newer and
installs below `~/.local` without `sudo`. It includes Solar's private YANC 5.2
bundle. Icarus, Yosys, Verilator, cocotb, GTKWave, and Surfer remain
user-managed external tools.

## Compatibility and limitations

Project manifest format remains 2, format-1 normalization remains available,
and project layouts and public artifact paths are unchanged. Older TXT-only
reports can still be displayed but do not gain comparison data retroactively.
Generic Yosys cells are not FPGA utilization, physical timing, place-and-route,
or bitstream information.

Solar supports trusted local project inputs. The bundled legacy YANC compiler
is not a sandbox for hostile or multi-tenant source. ARM64, Alpine/musl, macOS,
Windows, FPGA programming, and ASIC flows remain outside this release.

## Resumo em português

O Solar 0.4.6 adiciona estatísticas genéricas de síntese, histórico e comparação
de builds, autocomplete para Bash/Zsh/Fish e progresso visível durante a fase de
simulação do `solar build full`. O formato do projeto continua sendo 2 e não há
migração obrigatória para projetos existentes.
