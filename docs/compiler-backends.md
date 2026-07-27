# Compiler backends

## Status

Compiler backends transform a source program into structured hardware
artifacts. They are a separate backend category from simulation and synthesis:
YANC is a compiler backend, Icarus Verilog is a simulation backend, and Yosys is
a synthesis backend.

As of 2026-07-12, the format-2 model, Compiler Service, static compiler registry,
atomic publication helpers, structured result API, and YANC adapter are
implemented. The complete CMM, C++, and Assembly paths have been exercised
through Solar with the official YANC v5.2 release, Icarus/VVP, and Yosys. This
document describes that current contract, which remains pre-release.

## Boundary

The implemented flow is:

```text
CLI command
    -> Solar Core Compiler Service
        -> selected compiler backend
            -> shared process runner
                -> external compiler executables
```

The current Compiler Service owns the generic dispatch boundary:

- selecting a configured backend;
- validating the project before dispatch;
- rejecting projects without a configured compiler stage;
- returning structured diagnostics and artifacts to callers.

The selected compiler backend owns the bounded toolchain lifecycle while using
shared Core runner and filesystem helpers:

- backend identity and supported source languages;
- executable and asset discovery;
- tool version collection;
- project-local staging and log layout;
- exact stage order and argv construction;
- interpretation of stage failures;
- discovery and normalization of documented tool outputs;
- required-artifact validation and publication.

This keeps YANC-specific workspace assumptions out of generic Core without
pretending that the current release has multiple compiler implementations requiring a larger
multi-operation public vtable.

The CLI must not construct compiler arguments, inspect generated filenames, or
run `solar` recursively. Simulation and synthesis backends must not locate YANC
files by scanning `.solar/`; they receive an artifact set from Core.

## Request model

The backend request is constructed only from values needed by the current
backend, including:

- project and manifest roots;
- language and main source;
- validated processor name;
- compiler include directories;
- frequency and simulation clock count;
- backend-specific documented settings such as diagnostic language;
- staging, output, temporary, and log paths.

All strings and lists have explicit ownership. A backend may borrow request
values for the duration of one call, but it must not retain pointers after
returning. No request field is an arbitrary command-line fragment.

Profile defines are deliberately absent from YANC compiler argv. They
are applied later to generated Verilog simulation and synthesis requests.

## Result model

A successful compiler result must identify artifacts by role rather than by a
backend-specific directory convention. The required roles for the YANC
single-processor flow are:

- generated assembly;
- generated processor RTL;
- generated testbench RTL;
- instruction-memory image;
- data-memory image;
- optional Assembly opcode translation table for waveform presentation;
- processor top;
- testbench top;
- runtime working directory;
- stage logs;
- optional auxiliary files and source/PC translation tables;
- build metadata.

Paths in a successful result refer to the published build, not a disposable
staging tree. The result owns any returned strings and arrays and must have one
matching cleanup operation.

For CMM and C++, `assembly_path` refers to the validated public
`software/<processor>.asm`. For direct Assembly input it refers to Solar's
private staged copy; the user-authored `[compiler].source` is never claimed as
a generated artifact.

A process exit code alone is never a compiler result. Success requires both a
successful process pipeline and readable required artifacts with the expected
safe names.

## Backend lifecycle

A compiler backend follows this bounded lifecycle:

1. **Resolve**: find the external root, executables, and read-only assets.
2. **Probe**: collect a version using a safe argv invocation.
3. **Validate**: reject unsupported language, invalid configuration, missing
   executable, invalid layout, or unsafe path before running a stage.
4. **Prepare**: create the minimum tool-specific project structure under
   `.solar/tmp/`; copy source inputs without modifying the originals.
5. **Compile**: invoke stages in deterministic order through the shared runner.
6. **Inspect**: verify all mandatory outputs and the backend-specific top name.
7. **Normalize**: convert native outputs into Solar's artifact roles and remove
   verified staging-path coupling from published files.
8. **Publish**: replace the previous build only after every preceding step
   succeeds.
9. **Return**: provide structured artifacts and stage diagnostics to Core.

After a generated simulation publishes its waveform, Core may call the
backend's optional presentation operation. This is a separate, non-critical
step: YANC copies the validated opcode table, invokes `gen_gtkw` through the
shared runner, and atomically replaces
`.solar/state/waveforms/<test>/`. Failure leaves the raw waveform valid and is
returned as a structured warning to the frontend.

A separate read-only backend operation validates and returns the current
layout for `solar view`. The CLI therefore neither reconstructs a YANC path nor
executes presentation tooling while opening an existing waveform.

The backend stops at the first failed compiler stage. Logs from completed and
failed stages remain available; later executables are not invoked.

## Staging and atomic publication

Disposable compiler state and logs belong below `.solar/`; the validated result
is intentionally visible from the project root:

```text
software/
`-- <processor>.asm       # CMM/C++ output only
hardware/
|-- <processor>.v
|-- <processor>_data.mif
`-- <processor>_inst.mif
simulation/
`-- <processor>_tb.v
```

The generated Assembly joins the hardware and simulation files in one
transactional publication set. An existing unregistered Assembly at the same
path is treated as user-authored and blocks the build instead of being
overwritten. Direct Assembly projects publish no Assembly output.

```text
.solar/tmp/<backend>/<processor>/       # native, disposable workspace
.solar/logs/<backend>/<processor>/      # per-stage stdout/stderr
software/                               # generated CMM/C++ Assembly
hardware/                               # validated RTL/MIF outputs
simulation/                             # validated generated testbench
```

Publication must preserve a previous valid build when compilation or artifact
validation fails. The backend prepares a complete candidate, then uses the
artifact publisher to perform Linux `RENAME_EXCHANGE` when available. A
same-directory backup/rename sequence provides rollback on filesystems without
exchange support. Processor names are validated before becoming path
components, and unregistered public files are never overwritten.

Some tools embed their staging paths in generated files. A backend must account
for that before the generic swap. In particular, the inspected YANC v5.2
`asmcomp` writes its `-p` path into generated MIF and simulation I/O references.
Publishing those files after merely renaming their parent would create a broken
build. See [YANC integration](yanc.md#publication-constraint).

The service does not use build metadata as a cache. A successful build
may record reproducibility information, but compilation still runs when the
command requests it.

## Process execution

Every stage is represented by a process specification with:

- executable path or name;
- a null-terminated argv array;
- optional working directory;
- explicit stdout and stderr log paths;
- explicit environment additions only when a backend proves they are needed.

The shared runner uses `fork`, descriptor redirection, `execvp`, and `waitpid`.
Compiler backends must not use `system()`, shell concatenation, `popen()`,
wildcards, or process-wide `chdir`, `PATH`, or environment mutation.

Paths containing spaces remain individual argv entries. User configuration is
never interpreted as a flag. Backend implementations expose documented options
through typed fields, such as a positive integer frequency, rather than through
free-form strings.

## Diagnostics

Compiler failures remain stage-specific. Core and CLI must be able to
distinguish at least:

- invalid source project or compiler configuration;
- unknown compiler backend;
- toolchain root not found;
- invalid toolchain layout;
- required executable missing;
- frontend failure;
- pre-assembler failure;
- assembler failure;
- expected artifact missing or unreadable;
- filesystem safety or publication failure;
- internal Solar failure.

A diagnostic includes the backend, stage, executable when useful, exit code or
terminating signal, and log path. Solar adds context but does not replace the
external tool's stderr with a generic message.

Version probing is also backend-specific. A backend should preserve the first
useful version line because related executables can format the same version
differently.

## Consumer contract

Simulation and synthesis requests are constructed from artifact roles:

- simulation receives generated RTL, generated testbench, required support
  RTL, memory images, selected top, working directory, effective Verilog
  includes/defines, and isolated output/log paths;
- synthesis receives generated processor RTL, required support RTL, memory
  images when needed, selected top, global/profile Verilog configuration, and
  output/log paths;
- synthesis never receives generated testbench RTL;
- test-specific defines and include directories never reach synthesis.

This contract keeps backend-specific probing out of Icarus and Yosys and makes
the same generated build reusable by another future frontend without invoking
the Solar CLI.

## YANC backend mapping

The first compiler backend is YANC. Its verified mapping is:

```text
CMM:       cmmcomp -> appcomp -> asmcomp
C++:       cpppp -> cppcomp -> appcomp -> asmcomp
Assembly:  appcomp -> compatibility sidecars -> asmcomp
```

YANC root precedence, checkout/release layouts, exact arguments, native output
locations, and observed upstream differences are documented in
[docs/yanc.md](yanc.md). Those observations are based on upstream `main`
commit `ea6135af18735e66a2bd23445749d6a244990fcd` and release v5.2.

The backend must use the inspected interface rather than invoking YANC's setup
or runner scripts. It reads `bin/`, HDL, macros, and headers from Solar's
bundled root (or an explicit development override), while all staged project
data is written under the active project's `.solar/`.

## Testing contract

Unit tests must not require a real compiler toolchain. Small native fake
executables should record argv and create controlled artifacts so tests can
cover:

- backend selection and layout resolution;
- exact stage order and supported locale flags;
- paths and include directories containing spaces;
- stage failure and suppression of later stages;
- missing artifacts despite exit status zero;
- publication success and preservation of a previous build;
- ownership and cleanup of structured results.

Real YANC integration tests run against the mandatory CMake-built bundle.
`SOLAR_TEST_YANC_ROOT` optionally replaces that root for compatibility testing.
Fake stages prove Solar's orchestration contract; they do not prove that a CMM,
C++, or Assembly program is accepted by YANC.

At the status date, CMM and C++ pipelines were exercised externally against the
official toolchain before integration. All three languages were then exercised
end to end through the Solar Compiler Service, including Icarus/VVP, Yosys, and
a project root containing spaces. The C++ run included a local header in a spaced
include directory. Assembly passed using a staging-only bridge derived from
`app_log.txt`; its neutral PC map has no high-level source-line correlation.

## Current limitations

- One source and one processor per compiler project.
- One implicit generated test for YANC projects.
- Sequential execution only.
- No timeout, programmatic cancellation API, incremental cache, or parallel
  compiler stages. Terminal interruption is forwarded to the tool group.
- No arbitrary compiler flags or dynamic compiler plugins.
- No multiprocessor YANC flow.
- No runtime installation, download, setup script, or source-line/variable
  waveform formatting beyond the Assembly-only layout.
