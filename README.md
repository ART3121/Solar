<p align="center">
  <img src="docs/assets/solar-logo.svg" alt="Solar" width="620" />
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

Solar 0.4.5 supports Linux x86_64 and project manifest format 2, with
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

## Documentation and manual
## Generic Synthesis Statistics

After a successful Yosys synthesis, Solar analyzes the dedicated `stat -top`
artifact and stores a typed **Generic Synthesis Statistics** snapshot in the
last-build report. `solar report` shows reported design totals and cell usage.
Missing optional fields are shown as `not reported`, not zero. The original
Yosys text remains available as `synth/statistics.txt` for Verilog projects or
`hardware/statistics.txt` for SAPHO projects. These are generic Yosys cells and
structures, not FPGA resource estimates or place-and-route data.


- [Solar website](https://art3121.github.io/Site-Solar/)
- [User manual](https://art3121.github.io/Site-Solar/docs/)
- [Manual em português](https://art3121.github.io/Site-Solar/docs/?lang=pt)
- [Installation](https://art3121.github.io/Site-Solar/docs/#install)
- [Command reference](https://art3121.github.io/Site-Solar/docs/#commands)
- [Project configuration](https://art3121.github.io/Site-Solar/docs/#project)
- [Troubleshooting](https://art3121.github.io/Site-Solar/docs/#troubleshooting)
- [Architecture](docs/architecture.md)
- [Solar Core API](docs/api.md)
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
