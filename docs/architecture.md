# Solar architecture

## Purpose

Solar is a lightweight open hardware design platform and EDA workflow
orchestrator. The native Linux implementation is split into a reusable Core, a
thin CLI, typed services, and isolated adapters for external tools.

```text
Solar CLI
    |
    v
Solar Core
    |-- Project/configuration service
    |-- RTL and Test/cocotb Services
    |-- Compiler Service
    |-- Synthesis Service
    `-- Waveform Service
             |
             v
Static backend registries
    |-- YANC compiler backend
    |-- Icarus simulation backend
    |-- Verilator simulation backend
    `-- Yosys synthesis backend
             |
             v
External executables
```

Core and the current adapters are linked into the same `solar_core` static
library, but their ownership boundaries remain explicit.

## CLI layer

Files under `src/cli/` parse command arguments, load a project through Core,
call one service, print concise output, and map `SolarStatus` to an exit code.
The CLI does not parse manifests, construct Icarus/Yosys/YANC argv, discover
generated files, implement recursive deletion, or invoke `solar` recursively.

| `SolarStatus` | Exit code |
| --- | --- |
| `SOLAR_STATUS_OK` | `0` |
| `SOLAR_STATUS_INVALID_ARGUMENT` | `2` |
| `SOLAR_STATUS_CONFIG_ERROR`, `SOLAR_STATUS_NOT_FOUND` | `3` |
| `SOLAR_STATUS_TOOL_MISSING` | `4` |
| `SOLAR_STATUS_PROCESS_FAILED` | `5` |
| `SOLAR_STATUS_IO_ERROR`, `SOLAR_STATUS_INTERNAL_ERROR` | `1` |

All executable operations enter through `SolarBuildContext`. `build sim`
delegates to the Test Service, while `build full` retains generated artifacts
so a YANC compiler pipeline runs only once.

## Core services

| Module | Responsibility |
| --- | --- |
| `config.c`, `config_model.c` | Parse format 1/2, normalize v1, validate semantics, select tests/profiles, and build owned effective lists. |
| `config_edit.c` | Rewrite selected format-2 keys through a validated atomic candidate without serializing unrelated TOML. |
| `project.c` | Discover/load project roots, resolve manifest-relative paths, validate files/directories, and initialize templates. |
| `discovery.c` | Enumerate conventional Verilog sources, build a conservative module-instantiation graph, and infer synthesis/testbench roots. |
| `scan.c` | Recalculate and synchronize managed sources/tops through a validated, atomic manifest candidate. |
| `rtl.c` | Build effective RTL configuration and dispatch typed elaboration or compiler generation. |
| `build.c` | Own one pipeline context, stage results, timing, reuse, and the last-build report. |
| `test.c` | Select tests/profiles, isolate artifacts, invoke the compiler for generated projects, stage runtime files, and dispatch simulation. |
| `cocotb.c` | Invoke the private cocotb 2.x runner adapter with typed test data. |
| `compiler.c` | Select a compiler backend and return owned, role-based generated artifacts and per-stage results. |
| `synthesis.c` | Build profile-aware RTL-only requests, invoking the Compiler Service first when configured. |
| `system.c` | Collect a bounded non-secret Linux host snapshot without subprocesses. |
| `filesystem.c` | Create/remove/publish project-local generated trees without following unsafe symlinks. |
| `runner.c` | Run one executable and argv with optional working directory and separate logs, without a shell. |
| `progress.c` | Dispatch optional borrowed semantic progress observers without UI state. |
| `waveform.c` | Apply graphical policy and detach GTKWave or Surfer safely. |
| `diagnostics.c` | Structured status, severity, message, and corrective hint. |

Library functions return results; they do not call `exit()` or print normal UI
text. Public result structures own their documented strings/lists until their
matching `*_free()` function is called. Borrowed configuration pointers are
never retained beyond a service call.

## Configuration phases

Project loading deliberately separates four phases:

```text
read recognized Solar TOML subset
-> construct owned SolarConfig
-> normalize absent format marker as format 1
-> semantically validate values and resolved filesystem paths
```

Format 1 becomes one in-memory test named `default`; the manifest is never
rewritten. Format-2 YANC projects receive one implicit generated test. Effective
configuration is created in a separate owned object, so merging global,
profile, and test values cannot mutate the parsed configuration.

## Backend boundaries

There are two internal backend categories:

- simulation/synthesis descriptors in the general backend registry;
- source-to-hardware compiler descriptors in the compiler registry.

YANC is a compiler backend. It is not registered as a simulator. Icarus,
Verilator, and Yosys receive typed requests and structured generated artifacts; they do not
resolve a YANC root or scan a YANC workspace.

| Backend | Category | Required tools |
| --- | --- | --- |
| `yanc` | compiler | language-dependent YANC executables and read-only assets |
| `iverilog` | simulation | `iverilog`, `vvp` |
| `verilator` | simulation | `verilator` |
| `yosys` | synthesis | `yosys` |

Registries are static. Runtime plugins are not implemented.

## Verilog test flow

```text
load and validate project
-> select test and profile
-> merge global -> profile -> test includes/defines
-> create .solar/tmp/tests/<name> and matching .solar logs
-> Icarus compiles RTL + selected test sources
-> VVP runs in the test artifact directory
-> inspect the configured waveform path
-> publish it in sim/ through the artifact registry when present
```

Each include directory is a distinct argv value and each macro is a distinct
`-D` argument. Test names select separate internal executable and log paths;
the useful waveform is public. A zero exit without the configured VCD/FST is a
successful test with no waveform artifact; the CLI names the exact expected
path and reminds the user to rerun `solar scan` after changing `$dumpfile`.
The result keeps a null waveform path. Format-1 manifests are normalized
without being rewritten and publish new waveforms through the same public
layout.

`solar build sim --all` executes declared tests sequentially. It records every
result, continues after a failed test, then returns failure when at least one
test failed.

Each `SolarTestResult` owns a copy of VVP stdout loaded from its preserved run
log. The Core does not print it; frontends choose how to present it. The CLI
labels output by simulation so `$display` messages remain useful.

## Verilog synthesis flow

```text
load and validate project
-> select profile
-> merge global -> profile includes/defines
-> construct request from authored RTL only
-> generate deterministic Yosys script
-> run Yosys
-> verify netlist and statistics report
-> parse Generic Synthesis Statistics
-> publish the untouched text report and typed result
```

Test sources, test include directories, and test defines cannot enter the
synthesis request.

## Generated-hardware flow

For a YANC project:

```text
CMM / C++ / Assembly source
-> Compiler Service
-> YANC backend staging under .solar/tmp/yanc/<processor>/
-> language-specific stages through the shared runner
-> validate assembly, processor RTL, MIFs, testbench, and runtime sidecars
-> normalize into a private candidate tree
-> publish generated Assembly in software/ for CMM/C++
-> publish RTL/MIF files in hardware/ and the testbench in simulation/
-> return SolarGeneratedArtifacts
```

Simulation then stages required MIF/runtime files into the generated test's
working directory and passes generated RTL, support HDL, and generated
testbench to Icarus. Synthesis passes generated processor/support RTL to Yosys
and excludes the testbench.

The official YANC v5.2 CMM, C++, and Assembly paths have passed compile,
simulation, waveform, and synthesis end to end through Solar. Assembly handles
the upstream frontend-sidecar assumption by deriving a validated instruction
count from `app_log.txt`, emitting minimal staging metadata, and creating a
neutral PC map. That map enables the flow but has no high-level source-line
correlation.

## Process runner

Every process request contains:

- executable name or path;
- null-terminated argv;
- optional child working directory;
- optional stdout and stderr log paths.
- optional validated child-only environment additions.
- optional borrowed progress observer for heartbeat and live output.

The runner uses `fork`, `open`, `dup2`, `chdir` in the child, `execvp`, and
`waitpid`. A close-on-exec error pipe distinguishes an absent executable or
pre-exec failure from an ordinary non-zero tool exit. Linux `signalfd` forwards
parent `SIGINT`/`SIGTERM`/`SIGHUP` to a dedicated child process group and the
runner reaps it before returning. The parent process never changes its global
working directory or constructs a shell command.

When live tool output is requested, the parent drains stdout/stderr pipes from
the existing `poll()` loop and tees each chunk to both the configured log and
the observer. The same finite poll interval supplies activity callbacks while
the percentage remains owned by the semantic build stage. No reader thread is
created. The CLI's reusable renderer is the only module that emits carriage
returns or ANSI line clearing, and only when stdout is a TTY; verbose and
redirected output use stable linear records.

Every runner result includes monotonic wall time in nanoseconds, including
process preparation, launch, wait, reap, and captured-output drainage. Typed
service results propagate that measurement for RTL elaboration, each YANC
stage, simulation compilation/runtime, and synthesis. The cocotb bridge is the
one nested orchestration case: its private Python adapter measures
`runner.build()` and `runner.test()` separately, while the POSIX runner records
the enclosing process duration.

`build.c` writes an atomic, self-contained `.solar/state/last-report.txt` after
the measured operation finishes. Tool-version probes and the host snapshot are
collected while writing and are excluded from the operation duration.
`solar report` performs no probes and simply reads this persisted evidence.
Host data is deliberately limited to hostname, OS/kernel/architecture, CPU
model/count, memory, page size, and the compiler/build type used for Solar.

The Report History service snapshots that human report into an immutable
`.solar/reports/build-NNNNNN/` record. Structured GSS, nanosecond simulation
timings, and escaped technical metadata live in separate versioned sidecars;
comparison never parses the formatted report. Publication stages the complete
record, renames it into place, and only then atomically updates the textual
`latest` marker. Failed builds retain any partial timings that reached the
BuildContext.

The synthesis-statistics parser is a separate Core module. It prefers the
Yosys `design hierarchy` summary when present, otherwise accumulates module
sections once, and records a presence bit for each numeric field. The
normalized schema is embedded in the atomic last-build report, so old reports
without the section remain readable and return unavailable statistics through
the public API. Parsing is optional enrichment: an unrecognized optional
statistics format does not invalidate a successfully generated netlist.

## Generated-data safety

External tools write only below `.solar/tmp/`; logs and state remain below
`.solar/logs/` and `.solar/state/`. Validated useful files are copied into
`sim/`, `synth/`, `hardware/`, or `simulation/` and recorded in the artifact
registry. Generated CMM/C++ Assembly is the sole artifact allowed below
`software/`, and only with YANC generation metadata. Descriptor-relative
traversal with no-follow checks rejects unsafe components and symlinks.

YANC runs only in private staging. Publication happens after mandatory output
validation, so an external-tool failure does not replace a previous public
file. Replacement uses `RENAME_EXCHANGE` when available and a same-directory
backup/rename/rollback fallback on other filesystems. `solar clean` trusts only
the registry and never recursively owns a public output directory.

Every mutating project operation acquires a non-blocking advisory lock on the
project-root directory. Builds, `scan`, `config set`, and `clean` therefore do
not share candidates, registries, reports, or temporary trees. Lock contention
is reported as an actionable error; Solar does not wait indefinitely. The lock
uses Linux `flock(2)`, matching the declared Linux platform, and is released
when its owning descriptor closes or the process exits.

## Current architecture limits

- Linux/POSIX only; all services are synchronous.
- No process timeout, programmatic cancellation API, parallel tests, or
  incremental cache. Terminal interruption is forwarded safely.
- Static backends only; no dynamic plugins or arbitrary flags.
- One YANC source/processor and one implicit generated test.
- No GUI dependency, download, or runtime installation. GTKWave or Surfer is
  launched as an optional detached consumer only through `solar view`.
