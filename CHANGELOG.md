# Changelog

All notable changes to Solar are recorded here. No tag or release is implied by
this development changelog.

## [0.4.6]

### Added

- Generic Synthesis Statistics collected from the dedicated Yosys `stat -top`
  artifact, with typed Core access, per-field availability, generic cell usage,
  persistent sidecars, and preservation of the original text report.
- Immutable build-report history with ordered IDs, atomic `latest`, TXT reports,
  versioned GSS/timing/metadata sidecars, and records for failed builds.
- `solar report list`, `solar report show`, and overflow-safe
  `solar report compare`, including explicit baselines, compact summaries, cell
  additions/removals, simulation timings, and technical-context warnings.
- Bash, Zsh, and Fish completion for the complete public CLI and dynamic project
  tests, profiles, registered waveforms, and stored report IDs.

### Changed

- `solar build full` now shows the same live simulation progress used by
  `solar build sim`, including TTY redraw, linear redirected output,
  `--no-progress`, and `--verbose`, and closes it before synthesis begins.
- Simulation compilation, execution, and enclosing-step wall times are persisted
  as integer nanoseconds for report comparison. Simulated HDL duration remains
  a separate optional metric.
- `solar report` presents Generic Synthesis Statistics with aligned metadata,
  compact narrow-terminal wrapping, and explicit metric/value and cell/count
  tables.
- The product version is `0.4.6`; project manifest format remains `2` and no
  manifest migration is required from Solar 0.4.5.

### Fixed

- The checksum-validating release installer accepts the standard Bash, Zsh, and
  Fish completion directories while retaining strict traversal, special-file,
  and path-character validation.
- Uninstall removes empty release documentation asset and shell-completion
  directories while preserving unrelated files and non-empty directories.

### Compatibility

- Existing format-1 and format-2 projects continue to load without rewrites.
- Existing latest-build TXT reports remain readable. Older reports without
  structured sidecars are listed as unavailable for comparison rather than
  being reconstructed from formatted text.
- Simulation, synthesis, SAPHO generation, public artifact paths, and external
  viewer behavior are unchanged.

## [0.4.5]

### Added

- Linux x86_64 CPack release archives, a checksum-validating `~/.local`
  installer with safe upgrade/uninstall behavior, and tag-gated draft release
  automation for `ART3121/solar`.
- A navigable GitHub Pages manual, PT-BR getting started guide, support policy,
  issue forms, pull-request template, and explicit release procedure.
- Optional Surfer 0.7 viewer support through
  `solar view ... --viewer surfer`, with detached shell-free launch and
  `doctor --all` version discovery. GTKWave remains the compatibility default.
- Transactional `solar config set` updates for `[project].name`,
  `[synthesis].top`, and `[project].default_test`; the final default-test option
  is `--test`.
- Public, registered `software/<processor>.asm` output for successful YANC CMM
  and C++ builds, with transactional replacement and report visibility.

### Changed

- The product version is `0.4.5`; project manifest format remains `2` and no
  manifest migration is required from Solar 0.4.0.
- The public SAPHO initializer is now `solar init --template sapho`; the old
  `yanc-cmm`, `yanc-cpp`, and `yanc-asm` names are no longer accepted by the
  CLI.
- The current Project command group includes `solar config`; waveform viewing
  remains an explicit artifact operation and never runs automatically.

### Fixed

- YANC no longer keeps the useful CMM/C++ Assembly output exclusively inside
  `.solar/tmp/`; direct Assembly projects still preserve their source file.
- Configuration edits validate a complete candidate and atomically preserve
  the original manifest on invalid values or I/O failure.
- A successful simulation with no configured waveform now reports the exact
  expected path and explains that `$dumpfile` changes require another
  `solar scan`, instead of only suggesting that dumping be enabled.
- Mutating project operations are serialized with a non-blocking root lock, so
  simultaneous builds, scans, configuration edits, and cleanup cannot race on
  shared state or publication candidates.
- Invalid Icarus and Verilator elaboration requests no longer leak their
  temporary argument arrays, and non-regular waveform diagnostics no longer
  depend on stale `errno` state.
- Oversized manifests are rejected before parsing at a documented 16 MiB
  limit, preventing unbounded memory consumption from accidental input.
- CMM identifiers that exceeded the bundled YANC compiler's fixed qualified
  name capacity now fail with a diagnostic instead of overflowing the stack;
  APPComp and ASMComp reject overlong symbols before fixed-buffer copies.
- Manifest editing/scanning, artifact state, reports, simulation output, and
  discovered source indexing now use bounded, no-follow reads from validated
  descriptors instead of unbounded or check-then-open reads.
- Process capture drains large stdout and stderr streams concurrently and
  bounds in-memory copies while preserving complete configured log files.

### Compatibility

- Existing format-1 and format-2 projects continue to load without rewrites.
- Verilog output paths are unchanged. CMM/C++ projects additionally expose the
  generated Assembly in `software/`; `solar clean` owns only its registered
  file and preserves user-authored files.
- No tag or hosted release is implied by this changelog entry.

## [0.4.0]

### Added

- Benchmark-oriented report sections with Linux host/CPU/memory information,
  Solar build compiler, exact tool versions, readable timings, raw monotonic
  nanoseconds, and explicit measurement-methodology guidance.
- Per-process wall timings for RTL elaboration, each YANC stage, Icarus or
  Verilator simulation compilation, VVP/Verilator/cocotb runtime, and Yosys.
- Separate cocotb `runner.build()` and `runner.test()` timings recorded by the
  private adapter inside `.solar/tmp/`.
- A real, cocotb-independent Verilator integration covering native VCD/FST
  simulation, published waveforms, and visible `$display` output.
- RTL-only Verilog projects with zero declared tests, including transactional
  `solar scan` synchronization after the last managed testbench is removed.
- Successful simulations without a VCD/FST as a supported result, with an
  actionable warning and no stale or invented waveform artifact.
- Reusable semantic progress observer shared by Build, RTL, Compiler, Test,
  Icarus, Verilator, cocotb, YANC, and the process runner.
- Pacman-style `solar build sim` TTY progress with the exact Verilog schedule
  `5/10/20/40/85/95/100`, current stage, backend, spinner, stage clock, and
  total clock.
- A redistributed SAPHO schedule that exposes YANC generation before
  simulation compilation without pretending to know progress inside a tool.
- Stable linear `[sim] ...` output for CI, pipes, and redirected stdout.
- `solar build sim --no-progress` and `solar build sim --verbose`.
- A measured simulation summary for project validation, source resolution,
  YANC generation when applicable, simulator compilation, execution, artifact
  publication, and total wall time.
- Focused tests for percentages, time formatting, TTY/non-TTY output,
  no-progress/verbose modes, every failed stage, signal interruption, log tee,
  and long-process activity.

### Changed

- `solar scan` removes a managed orphan `default_test` when conventional
  discovery yields no testbenches, while preserving explicit unmanaged tests.
- Simulation success is determined by the backend process/test result rather
  than by waveform creation; VCD/FST publication is optional.
- Verilator native simulation uses non-fatal warnings while preserving them in
  logs, and rejects unsupported whitespace-containing workspaces with an
  actionable diagnostic before invoking GNU Make.
- The runner can optionally drain tool stdout/stderr through its existing
  `poll()` loop, tee chunks to preserved logs and an observer, and emit
  periodic activity without threads.
- Verbose simulation uses linear progress while streaming original tool output
  so partial output chunks cannot corrupt a terminal bar.
- `SolarBuildContext` carries a borrowed progress observer and measured load
  and validation durations. Existing callers may omit the observer.
- The product version is `0.4.0`; project manifest format remains `2` and no
  manifest migration is required from Solar 0.3.

### Fixed

- A stale waveform from an earlier run can no longer make a later no-waveform
  simulation appear to have produced a new artifact.
- Failed project loading now terminates the progress display once before the
  actionable diagnostic.
- Parent `SIGINT` forwarding, process-group termination, child reaping, and
  signal-mask restoration remain active with progress observation enabled.

### Compatibility

- The v0.3 command surface, project layouts, artifact locations, supported
  backend set, and `solar.toml` format are unchanged. Verilator behavior is
  hardened without introducing a new backend or manifest key.
- Progress is initially limited to `solar build sim`; other build targets keep
  their v0.3 presentation.
- No viewer is opened by simulation. `solar view` remains explicit.

### Limitations

- Progress is stage-based, not an estimate of work performed inside an EDA
  tool. During long simulation stages only the spinner and clocks advance.
- Test execution remains sequential, with no timeout or incremental cache.

## [0.3.0]

### Added

- Focused CLI with `scan`, `build rtl`, `build sim`, `build synth`,
  `build full`, `view`, and a complete `report`.
- Typed RTL Service with Icarus compile-only and Verilator lint-only
  elaboration.
- Transactional, idempotent `.v`/`.sv` manifest synchronization through
  `solar scan`, including guarded format-1 migration.
- Complete last-build report with UTC timestamps, configuration snapshot,
  tool versions, step and backend results, operation artifacts, log paths, and
  a bounded sanitized stderr tail.
- Focused scan regressions for explicit-source preservation, stale managed
  entries, SystemVerilog, ambiguity rollback, and v1 migration.

### Changed

- The only public operation entry points are `solar build rtl|sim|synth|full`.
  The old `generate`, `compile`, `test`, `sim`, `simulate`, `synth`,
  `synthesize`, and `up` commands were removed without aliases.
- `build sim` owns named, `--all`, and `--list` selection. `build full` stops
  at the first failed simulation and reuses one YANC generation throughout.
- Every executable build target validates first and supports `--profile` and
  `--dry-run`; dry-run does not probe or execute tools.
- Automatic conventional discovery remains active during project loading;
  `scan` is an explicit persistence operation, never a prerequisite.
- Yosys selects SystemVerilog mode for `.sv` sources.
- Project version is `0.3.0`.

### Fixed

- Manifest synchronization preserves explicit unconventional sources and
  refuses ambiguous rewrites without modifying the original file.

### Removed

- Deprecated direct-operation aliases and their CLI implementations.
- Automatic viewer launch from simulation paths. GTKWave is opened only by
  `solar view`.

### Limitations

- Build history and machine-readable reports are not implemented; only the
  latest human-readable build report is stored.
- Test execution remains sequential with no timeout or incremental cache.

## [0.2.0]

### Added

- Solar project format 2 with an explicit `[solar] format = 2` marker.
- Automatic recursive `.v` discovery below `rtl/` and `tb/`, plus the
  non-mutating `solar up` inventory command.
- Named tests and multiple Verilog testbenches.
- `solar test`, including named selection, `--all`, and `--list`.
- Profiles selected through `--profile` or `[project].default_profile`.
- Project, profile, and test include directories and compile-time defines.
- Deterministic effective configuration with stable duplicate removal.
- A generic Compiler Service with structured generated-artifact results.
- A YANC compiler backend and checkout/release layout resolver.
- Vendored YANC 5.2 sources, mandatory staged build, private install bundle,
  license inventory, and automatic runtime resolution.
- Verilator simulation backend with VCD/FST selection, includes, defines, and
  isolated build/log directories.
- cocotb 2.x named-test driver for Verilator projects.
- Explicit detached GTKWave viewing through `solar sim --view` and `solar view`.
- `examples/verilator-cocotb/` and fake/real-skip integration coverage.
- CMM-to-SAPHO compilation through `cmmcomp`, `appcomp`, and `asmcomp`.
- C++ YANC frontend integration through `cpppp` and `cppcomp`.
- Assembly integration through `appcomp` and `asmcomp`, with a bounded YANC
  v5.2 compatibility bridge for required frontend sidecars.
- Canonical `solar generate`, `solar sim`, and `solar synth` commands with
  deprecated `compile`, `simulate`, and `synthesize` aliases.
- `solar build` pipelines (`test`, `sim`, `synth`, and `full`) with `--dry-run`.
- Shared `SolarBuildContext` orchestration and `.solar/state/last-report.txt`, exposed
  by `solar report`.
- Command-specific help and project-scoped `solar doctor`; `doctor --all`
  retains the complete tool matrix.
- The implicit `generated` test for YANC projects.
- YANC diagnostics in `solar doctor`.
- `verilog`, `yanc-cmm`, `yanc-cpp`, and `yanc-asm` project templates.
- Small format-2 counter and YANC examples with per-example READMEs.
- Atomic YANC publication, required-artifact validation, normalized build
  metadata, and per-stage logs.
- Unit coverage for format 2, v1 normalization, effective configuration, Test
  Service behavior, paths with spaces, YANC resolver layouts, stage argv/order,
  failure short-circuiting, missing artifacts, atomic publication, and
  previous-build preservation.
- Opt-in real YANC CMM/C++/Assembly CTest integrations with explicit skip
  reasons when `SOLAR_TEST_YANC_ROOT` is absent.
- CMake installation for the CLI, static Core library, public headers, and
  documentation.
- Parent-signal forwarding/reaping and child-only environment additions in the
  process runner.
- Explicit test failure classification for simulation compile, runtime, and
  logical testbench failures.
- VVP stdout in `SolarTestResult` and labeled `$display` output for individual
  and `--all` CLI test runs.
- End-to-end format-2 Verilog coverage and focused negative validation coverage.
- Public Verilog `sim/` and `synth/` output directories, plus SAPHO/YANC
  `hardware/` and `simulation/` output directories.
- A structured `.solar/state/artifacts.tsv` registry used by cleanup, viewing,
  and operation reports.
- `solar clean --cache` and `solar clean --all` modes.

### Changed

- `solar sim` calls the Test Service used by `solar test`; neither command opens
  a viewer unless `sim --view` is explicit.
- `solar build full` reuses one YANC generation across test and synthesis.
- `solar up` remains temporarily as a deprecated discovery inventory because it
  was not a full pipeline.
- Simulation executables, Yosys scripts, YANC workspaces, intermediates, and
  detailed logs are internal to `.solar/tmp/` and `.solar/logs/`.
- Verilog waveforms are published in `sim/`; netlists and reports in `synth/`.
  SAPHO RTL/MIF/synthesis outputs are published in `hardware/`, and generated
  testbenches/waveforms in `simulation/`.
- `solar clean` is manifest-driven and preserves public directories and files
  not registered as Solar-generated.
- Existing `solar-build/` directories are preserved as a warned legacy layout;
  new operations never recreate or modify them.
- Synthesis accepts a selected profile and excludes test-specific settings.
- Projects without `[solar].format` are normalized internally as format 1 with
  one implicit test named `default`.
- YANC projects compile before simulation or synthesis, and Icarus/Yosys consume
  a structured generated-artifact set.
- The project version is `0.2.0` in CMake and `solar --version`.
- The default Verilog template now uses discovery instead of embedding
  counter-specific source and testbench paths in `solar.toml`.
- A normal Solar build and installation now always builds and carries YANC;
  external roots are development overrides rather than a runtime prerequisite.

### Validation status

- CMM, C++, and Assembly were exercised end to end through Solar with the
  official YANC v5.2 release from a project root containing spaces: compile, artifact
  validation, publication, Icarus/VVP simulation, VCD generation, and
  profile-aware Yosys synthesis all passed.
- The C++ run used the upstream `test21` fixture plus a local header in an
  include directory containing spaces.
- Fake toolchains prove resolver and orchestration units; they are not evidence
  that upstream accepts every source-language flow.
- YANC v5.2 Assembly required Solar to validate `n_ins` from `app_log.txt`,
  produce staging-only `num_ins` compatibility metadata, and create a neutral
  PC map before `asmcomp`.
- GCC Debug passed 24 tests with one explicit real Verilator/cocotb skip; the
  three real bundled-YANC tests passed. Release built and installed with its
  private YANC bundle, and the installed binary completed a real CMM compile.
  ASan/UBSan passed the same matrix with LeakSanitizer disabled because this
  executor runs under `ptrace`; Clang was unavailable.
- The bundled YANC main revision `ea6135af18735e66a2bd23445749d6a244990fcd`
  passed real CMM, C++, and Assembly compile/simulate/synthesize flows.
- Verilator/cocotb process and argv contracts pass fake-tool tests. Their real
  integration is explicitly skipped here because neither tool is installed.
- GTKWave detached launch passes a fake executable test. A real window was not
  opened because this executor could not access its graphical session.

### Limitations

- Tests execute sequentially; there is no timeout or parallelism.
- One YANC source and one processor are supported per project.
- Only the single-processor YANC flow is represented.
- No automatic installation or download of Verilator, cocotb, GTKWave, Icarus,
  or Yosys; the optional `gen_gtkw` and `comp2gtkw` helpers are neither built
  nor installed by Solar.
- No incremental build cache.
- Assembly's neutral PC map does not provide high-level source-line correlation.

## [0.1.0]

### Added

- Initial C17 Core and CLI.
- `init`, `check`, `doctor`, `simulate`, `synthesize`, and `clean`.
- Icarus Verilog and Yosys backends.
- Format-1 `solar.toml`, safe POSIX runner, `.solar/` artifact layout, unit and
  external-tool integration tests.
