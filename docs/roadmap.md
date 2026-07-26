# Solar roadmap

## Current v0.4.6 implementation

The current development tree contains:

- C17 `solar_core` and the thin native CLI;
- project format 2 plus non-mutating format-1 compatibility;
- named Verilog tests, multiple testbenches, profiles, includes, and defines;
- conventional automatic Verilog/SystemVerilog hierarchy discovery and
  transactional `solar scan` synchronization of sources and inferred tops;
- transactional `solar config set` updates for project name, synthesis top,
  and default named test;
- RTL-only validation/build support and optional VCD/FST publication after a
  successful simulation;
- `build rtl`, `build sim`, `build synth`, and shared-context `build full`;
- stage-based `build sim` and full-build simulation progress with TTY redraw,
  linear CI output, `--no-progress`, `--verbose`, heartbeat, and measured stages;
- complete human-readable last-build reporting with host/tool snapshots and
  per-process monotonic wall timings;
- Generic Synthesis Statistics, immutable report history, structured comparison
  sidecars, and `report list`, `show`, and `compare` operations;
- Bash, Zsh, and Fish completion for static CLI syntax and dynamic project/report
  candidates;
- isolated test artifacts and sequential failure summaries;
- Compiler, Test, and Synthesis Services;
- static YANC, Icarus, and Yosys adapters;
- hardened native Verilator VCD/FST simulation with independent real-tool
  coverage and explicit upstream workspace limitations;
- YANC checkout/release resolution, staged compilation, required-artifact
  validation, atomic publication, public CMM/C++ Assembly, generated test
  handoff, and build metadata;
- safe shell-free execution and `.solar/` filesystem policy.
- mandatory bundled YANC 5.2 source build and private installation;
- Verilator simulation, cocotb 2.x tests, VCD/FST, and interactive GTKWave or
  Surfer viewing only through the explicit artifact command.

Real evidence currently includes complete CMM, C++, and Assembly paths through
Solar using YANC v5.2 from a project path containing spaces: compiler stages, artifact
validation/publication, Icarus/VVP, VCD, and Yosys synthesis passed. C++ also
used a local header in a spaced include directory. Assembly uses validated
`app_log.txt` instruction metadata plus staging-only compatibility files; its
neutral PC map cannot correlate back to high-level source lines.

The GCC, Clang, and ASan/UBSan acceptance matrices each register 38 tests and
pass 37. Fish syntax is the sole explicit local skip because Fish is absent;
real Icarus, Yosys, Verilator/cocotb, and YANC integrations pass.

## Version boundaries

Solar 0.4 groups benchmark-oriented reporting, Verilator hardening, RTL-only
projects with optional waveforms, and observable simulation progress with
immediate timing evidence. It does not change the CLI command set, project
manifest generation, supported backend set, or artifact layout. The exact
compatibility boundary is documented in
[Solar 0.3 to 0.4](migration-v0.3-to-v0.4.md).

Solar 0.4.5 adds explicit Surfer selection, transactional configuration edits,
and public registered CMM/C++ Assembly. It keeps manifest format 2 and is
documented in [Solar 0.4 to 0.4.5](migration-v0.4-to-v0.4.5.md).

Solar 0.4.6 adds Generic Synthesis Statistics, immutable build-report history
and comparison, general shell completion, and live progress during the
simulation phase of `build full`. It keeps manifest format 2.

## Release follow-up for v0.4.6

Remaining release evidence is environment-specific:

- run independent leak detection where LeakSanitizer is not blocked by
  `ptrace`, or use an available equivalent such as Valgrind;
- add an explicit supported-version policy if a future YANC release diverges
  from the validated v5.2 command/artifact contract;
- keep the hosted CI and tag-gated release workflow green for supported Linux
  release environments;
- enable GitHub Pages, Discussions, repository protection, and private
  vulnerability reporting through the explicit maintainer checklist.

The opt-in real YANC matrix, native fake-tool tests, GCC/Clang Debug,
ASan/UBSan, CMake installation, release packaging, checksum/rollback installer
tests, ownership/path review, public manual, and community templates are
complete in this tree.

## Stable v0.4.6 acceptance

Before marking v0.4.6 stable:

- format-1 and format-2 tests must pass;
- named tests/profiles must work through real Icarus/Yosys flows;
- CMM, C++, and Assembly must keep passing real Solar
  RTL/simulation/synthesis build flows;
- a failed YANC build must preserve the previous valid publication;
- every real-tool absence must be an explicit skip, not silent success;
- GCC, available Clang, and sanitizer builds must pass without new Solar
  warnings or exercised ownership errors;
- useful artifacts remain in `sim/`, `synth/`, `hardware/`, `simulation/`, or
  the narrowly registered CMM/C++ output `software/<processor>.asm`, while
  logs, state, and transient work remain below `.solar/`.
- RTL-only projects must remain valid for scan, check, RTL elaboration, and
  synthesis, while simulation without a selected test remains actionable.
- a zero-exit simulation without VCD/FST must pass with a warning and must not
  register a stale waveform.
- complete reports must retain host/tool/configuration snapshots and raw
  per-process timing evidence without executing tools during `solar report`.
- progress must remain monotonic, TTY-only for terminal control sequences, and
  responsive to `SIGINT` without inventing simulator-internal percentages.
- report comparison must use persisted structured data, preserve legacy TXT
  reports, distinguish missing values from zero, and execute no EDA backend.
- `build full` must close its simulation progress display before synthesis.

## Later workflow capabilities

Possible later milestones include FPGA place-and-route/programming adapters,
ASIC-oriented orchestration, additional established simulators/synthesizers,
machine-readable output, and a carefully versioned backend/plugin boundary.
Each must remain an adapter around an external tool and retain the Core-first
architecture.

## Future graphical layer

Solar Studio remains future work. Any GUI must be optional and call the same
Core services as the CLI. It must not become a build/runtime dependency of
`solar_core`, project validation, or backend execution.

## Explicitly out of scope

The current milestone does not include an HDL editor, LSP, FPGA programming,
nextpnr, OpenFPGALoader, OpenLane, ASIC implementation, package manager,
automatic downloads/installations, parallel execution, timeouts, coverage,
dynamic plugins, VHDL, YANC multiprocessor scripts, automatic use of
`gen_gtkw`/`comp2gtkw`, incremental cache, database, daemon, web UI, or Aurora
integration.

## Known design limits

- Linux/POSIX only and synchronous processes.
- Explicit paths and a documented TOML subset, not globs/full TOML.
- One source/processor and one generated YANC test.
- Static backend registries and no arbitrary tool flags.
- Source confinement is lexical; generated-data operations use stronger
  descriptor-relative no-follow handling.
