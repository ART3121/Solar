<p align="center">
  <img src="docs/assets/solar-logo.svg" alt="Solar" width="620" />
</p>

<p align="center">
  <img alt="C17" src="https://img.shields.io/badge/C-17-00599C?style=flat-square&amp;logo=c&amp;logoColor=white" />
  <img alt="Platform: Linux" src="https://img.shields.io/badge/platform-Linux-FCC624?style=flat-square&amp;logo=linux&amp;logoColor=111111" />
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-2EA44F?style=flat-square" /></a>
  <a href="https://github.com/ART3121/solar/releases/tag/v0.4.6"><img alt="Version: 0.4.6" src="https://img.shields.io/badge/version-0.4.6-EF6C00?style=flat-square" /></a>
</p>

# Solar

Solar is a lightweight open hardware design platform and EDA workflow
orchestrator for Linux. It validates one project model, invokes established
tools safely, keeps intermediate data contained, and publishes useful hardware
artifacts with actionable diagnostics.

Solar does not reimplement a compiler, simulator, synthesizer, editor, or
waveform viewer. Its native C17 Core powers the CLI and remains reusable by
future frontends.

## Status

Solar 0.4.6 supports Linux x86_64 and project manifest format 2, with
non-mutating compatibility for format 1.

```text
Project:    init  scan  config  check  doctor  clean
Build:      build rtl  build sim  build synth  build full
Artifacts:  view  report
```

Supported flows include Verilog/SystemVerilog with Icarus Verilog, Verilator,
and Yosys; optional cocotb 2.x tests; and bundled YANC 5.2 compilation for CMM,
C++, and Assembly SAPHO projects. GTKWave and Surfer are optional external
viewers used only by `solar view`.

Solar is intended for trusted local project inputs. The bundled legacy YANC
compiler has received targeted hardening but is not presented as a sandbox for
hostile or multi-tenant source code.

## Install

The recommended release installation needs no compiler and writes below
`~/.local` without `sudo`:

```sh
curl -fsSL https://github.com/ART3121/solar/releases/latest/download/install.sh | sh
```

If `~/.local/bin` is not already on `PATH`, add it in your shell configuration,
then inspect available tools:

```sh
solar --version
solar doctor --all
```

The installer verifies the release archive checksum. It installs Solar and its
private YANC bundle, but never installs or changes system EDA packages. See the
[installation manual](https://art3121.github.io/Site-Solar/docs/#install) for
manual verification, dependencies, upgrades, and uninstall instructions.

Solar installs command completion definitions for Bash, Zsh, and Fish. They
complete the public command tree plus project tests, profiles, registered
waveforms, and stored build IDs. A local-prefix Zsh installation may require
adding `~/.local/share/zsh/site-functions` to `fpath`; Solar never edits shell
startup files automatically.

## Runtime tools

Install only the tools required by your flow:

| Tool | Purpose |
| --- | --- |
| `iverilog`, `vvp` | Icarus RTL elaboration and simulation |
| `yosys` | Verilog synthesis |
| bundled YANC 5.2 | CMM/C++/Assembly hardware generation |
| Verilator 5.x | Optional alternate RTL/simulation backend |
| Python 3, cocotb 2.x | Optional cocotb driver; requires Verilator 5.036+ |
| `gtkwave` or `surfer` | Optional explicit waveform viewing |

Solar never downloads these tools at runtime. Missing tools are reported by
`solar doctor` and fail only flows that require them.

## Quick start

```sh
mkdir counter
cd counter
solar init
solar scan
solar check
solar build sim --list
solar build sim
solar build synth
solar report
solar report list
solar report compare --summary
```

For example, after builds have been recorded, TAB completes the available
baseline IDs:

```sh
solar report compare --against build-<TAB>
```

`solar init` creates a synthesizable and simulatable Verilog project. Files
below `rtl/` and `tb/` are discovered automatically. Run `solar scan` when you
want the discovered sources, tops, tests, and literal `$dumpfile(...)` paths
synchronized into `solar.toml`.

After changing the name or path passed to `$dumpfile`, run `solar scan` again
before building. `solar config set --test <name>` selects an already discovered
test as the default; it does not rescan the testbench.

Useful outputs remain visible:

```text
Verilog: sim/  synth/  .solar/
SAPHO:   software/  hardware/  simulation/  .solar/
```

Executables, logs, scripts, and disposable work stay below `.solar/`. Solar
records generated public files so `solar clean` never owns arbitrary user
files.

## Build reports

Solar keeps the latest readable report and an immutable history below
`.solar/reports/`. Reports include tool versions, host context, measured build
times, generated artifacts and, after Yosys synthesis, Generic Synthesis
Statistics such as wires, memories, processes and generic cell usage.

```sh
solar report                         # latest build
solar report list                    # stored builds
solar report show build-000041       # one stored report
solar report compare --summary       # current versus previous
solar report compare --against build-000041
```

Comparison reads persisted data and never reruns a simulator or Yosys. The
original Yosys statistics remain in `synth/statistics.txt` or
`hardware/statistics.txt`; generic cells are not FPGA utilization estimates.
Simulation wall time describes execution on the host and remains distinct from
the HDL time advanced inside a testbench.

## Documentation and manual

- [Solar website](https://art3121.github.io/Site-Solar/)
- [User manual](https://art3121.github.io/Site-Solar/docs/)
- [Manual em português](https://art3121.github.io/Site-Solar/docs/?lang=pt)
- [Installation](https://art3121.github.io/Site-Solar/docs/#install)
- [Command reference](https://art3121.github.io/Site-Solar/docs/#commands)
- [Project configuration](https://art3121.github.io/Site-Solar/docs/#project)
- [Troubleshooting](https://art3121.github.io/Site-Solar/docs/#troubleshooting)
- [Architecture](docs/architecture.md)
- [Solar Core API](docs/api.md)
- [Stored reports and comparison](docs/report-history.md)
- [Testing and release evidence](docs/testing.md)

The Solar website is the public entry point for documentation. Release
archives also contain the detailed reference matching that exact binary.

## Build from source

Building Solar and its bundled YANC toolchain requires Linux, CMake 3.20+, a
C17 compiler, GNU Make, Flex, and Bison.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

For a local release installation without test targets:

```sh
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build-release
cmake --install build-release
```

## Support and contributing

Use [GitHub Discussions](https://github.com/ART3121/solar/discussions) for
questions and ideas. Use [GitHub Issues](https://github.com/ART3121/solar/issues)
for reproducible defects. Read [SUPPORT.md](SUPPORT.md) and
[CONTRIBUTING.md](CONTRIBUTING.md) before opening a report or pull request.

Report security problems privately as described in [SECURITY.md](SECURITY.md).

## License

Solar is available under the [MIT License](LICENSE). The bundled YANC license
and Solar's local compatibility changes are recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
