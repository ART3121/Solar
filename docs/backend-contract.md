# Solar backend contract

## Boundary

Backends adapt validated, typed Solar requests to external tools. They do not
parse CLI arguments or manifests, mutate global configuration, install tools,
launch graphical applications, or write unvalidated output directly into
public artifact directories.

The current registries are static and compiled into `solar_core`. Dynamic
plugin loading is not implemented.

## Backend categories

Simulation and synthesis use an internal `SolarBackend` descriptor containing:

- stable manifest name and capability mask;
- required/optional tool definitions and version-probe argv;
- optional legacy project entry points for format-1 compatibility;
- typed RTL-elaboration, simulation, and synthesis request entry points.

Source-to-hardware compilers use the separate compiler-backend descriptor and
Compiler Service documented in [Compiler backends](compiler-backends.md). YANC
is not a simulation backend. A compiler backend may also expose an optional
waveform-presentation callback and a read-only lookup callback. The first
prepares viewer metadata after simulation; the second lets a frontend find
current metadata without guessing backend paths, turning the compiler into a
simulator, or launching a GUI.

Current registrations are:

| Name | Category | Tools |
| --- | --- | --- |
| `iverilog` | RTL/simulation | required `iverilog`, required `vvp` for simulation |
| `verilator` | RTL/simulation | required `verilator` |
| `yosys` | synthesis | required `yosys` |
| `yanc` | compiler | language-dependent YANC executables/assets |

GTKWave and Surfer are optional waveform consumers, not simulation backends.
The Core viewer service launches the selected client only through `solar view`
and never changes a build result. GTKWave remains the default; Surfer is
selected with `--viewer surfer`. Viewer options may carry a validated working
directory and optional layout. Only GTKWave consumes the current `.gtkw`
layout; other viewers receive the waveform alone.

## Typed requests

An RTL request supplies authored sources, synthesis top, effective global and
profile includes/defines, working directory, and separate logs. Icarus uses
compile-only elaboration; Verilator uses lint-only elaboration.

A simulation request supplies borrowed or owned values prepared by the Test
Service:

- backend, project root, and child working directory;
- test name and top;
- RTL sources and selected test sources;
- effective include directories and defines;
- executable, waveform, and per-stage log paths.

A synthesis request supplies:

- backend, working directory, and top;
- authored or generated RTL and required support HDL;
- effective global/profile include directories and defines;
- deterministic script, netlist, report, and log paths.

Backends consume these values for one call and do not retain pointers. Paths
with spaces remain single argv items. No request field is a free-form command
fragment.

## Lifecycle

A service invokes an EDA backend only after project validation:

1. Look up the configured backend and required capability.
2. Confirm each required executable is available and executable.
3. Build a typed request with safe project-local output paths.
4. Remove stale regular outputs while refusing symlinks/unexpected types.
5. Construct argv or a deterministic tool input script.
6. Invoke the shared runner with an explicit child working directory.
7. Verify every expected regular artifact.
8. Return `SolarResult` while retaining the original stdout/stderr logs.

Compiler backends additionally stage inputs, execute ordered frontend/assembler
stages, validate role-based outputs, and publish a complete candidate. Their
lifecycle is documented separately because it is not a simulation capability.

## Process execution

All process calls use `SolarProcessSpec` with executable, null-terminated argv,
optional working directory, separate stdout/stderr log paths, and optional
validated child-only environment additions. The shared runner owns `fork`,
child-only `chdir`, descriptor redirection, `execvp`, and `waitpid`. On Linux it
uses `signalfd` and a child process group to forward `SIGINT`, `SIGTERM`, and
`SIGHUP`, then reaps the external process before returning.

Backends must not use `system()`, `popen()`, shell concatenation, wildcards,
process-wide `chdir`, or process-wide `PATH` mutation. An argument beginning
with `-` is created from a typed validated setting; user input is never accepted
as arbitrary flags.

| Condition | Solar status |
| --- | --- |
| Executable absent at dispatch/`execvp` | `SOLAR_STATUS_TOOL_MISSING` |
| Non-zero exit or signal termination | `SOLAR_STATUS_PROCESS_FAILED` |
| Missing expected artifact | `SOLAR_STATUS_PROCESS_FAILED` |
| Invalid project/request | `SOLAR_STATUS_CONFIG_ERROR` or `INVALID_ARGUMENT` |
| Log, script, directory, or publication I/O failure | `SOLAR_STATUS_IO_ERROR` |

`SolarTestResult.failure_kind` further distinguishes simulation compilation,
runtime/artifact failure, and a normal non-zero VVP exit interpreted as a
logical testbench failure.

On failure, a backend retains the runner's classification, adds tool/stage/log
context, and preserves the complete external stderr rather than replacing it
with a generic Solar message.

## Icarus backend

For a named format-2 test, Icarus receives this logical argv:

```text
iverilog -g2012 -o <test-dir>/simulation.vvp -s <test.top> \
  -I <include-1> -I <include-2> \
  -DGLOBAL -DPROFILE -DTEST \
  <RTL sources> <selected test sources>
```

Every `-I` directory is a separate argv value, and every define is one
`-D<macro>` argument. Icarus compilation and VVP both use the isolated test
artifact directory as their working directory so relative files remain local:

```text
vvp <test-dir>/simulation.vvp
```

Executables and tool-produced waveforms first live in
`.solar/tmp/tests/<test>/`; logs remain in `.solar/logs/tests/<test>/`. After
validation, only the VCD/FST is published in `sim/` (or `simulation/` for
YANC). Format-1 manifests use the same publication path without being changed.

For the generated YANC test, the Test Service first runs the Compiler Service,
then passes processor RTL, required YANC support HDL, and generated testbench to
Icarus. It copies MIF and simulation sidecars to the test working directory.
Icarus does not scan the toolchain or generated build.

The backend verifies the waveform after the simulator. Viewer launch remains a
separate Core/CLI action so Icarus, Verilator, and cocotb share one policy.

The registered simulation backends are Icarus (`iverilog` + `vvp`) and
Verilator (`--binary --timing`). cocotb is a test driver layered over the
selected Verilator simulator, not another backend. Its private Python adapter
uses the cocotb 2.x runner API and receives only typed sources, includes,
defines, top, build directory, test module, and waveform path.

Verilator compilation uses `-Wno-fatal`: warnings remain in the detailed log,
but a warning alone does not reject an otherwise valid simulation. Verilator's
generated GNU Make files reject build directories whose absolute path contains
whitespace. Because Solar keeps the build workspace below project-local
`.solar/tmp/`, such a project path is rejected before tool execution with an
actionable diagnostic; source and include arguments may still contain spaces.

## Yosys backend

Yosys receives a deterministic script and is invoked with:

```text
yosys -s <project>/.solar/tmp/synth/synthesis.ys
```

The script first registers effective global/profile values with
`verilog_defaults` and then emits one safely quoted `read_verilog` command per
RTL source. Include directories are exposed through generated relative
`includes/N` aliases because Yosys requires `-Idir` as one token; this keeps
original paths containing spaces safe. The script then performs hierarchy
checking, process lowering, optimization, checks, statistics, and netlist
output.

For authored Verilog, only `[sources].rtl` is included. For YANC, only generated
processor/support RTL is included. Authored test sources, generated testbench,
test includes, and test defines never enter synthesis.

Internal and public outputs are separated:

```text
.solar/tmp/synth/synthesis.ys
.solar/tmp/synth/netlist.v
.solar/tmp/synth/statistics.txt
.solar/logs/yosys/yosys.stdout.log
.solar/logs/yosys/yosys.stderr.log
synth/<top>_netlist.v
synth/statistics.txt
```

The backend verifies netlist and report before success. The CMM/YANC generated
flow has passed a real profile-aware Yosys run through Solar.

## Compiler/YANC handoff

`SolarGeneratedArtifacts` identifies assembly, processor RTL, support HDL,
testbench, instruction/data images, working directory, tops, metadata, and
auxiliary files by role. Icarus and Yosys consume this structure. They must not
reconstruct YANC names or search its private workspace.

CMM/C++ Assembly is published as a registered `software/<processor>.asm` and
returned by that role. Direct Assembly source remains user-owned; its staged
copy may satisfy the internal role but is not publicly registered.

YANC resolver precedence, layouts, exact upstream argv, publication constraints,
and the tested Assembly compatibility bridge are documented in
[YANC integration](yanc.md).

## Doctor reports

General tool reports own the executable name, resolved path, and first useful
version line; callers release them with `solar_backend_tool_reports_free()`.
Probes use tool-owned argv (`iverilog -V`, `vvp -V`, `yosys -V`).

YANC has a separate structured resolver report containing root, version,
executables, HDL, macros, headers, and recognized layout. A missing YANC root is
informational for projects that do not use it. Solar never invokes YANC setup
scripts or installs a missing dependency.

## Extending a registry

A new EDA backend needs a private descriptor, typed capability entry point,
tool definitions, tests for exact argv/artifacts, and one registry entry. A new
compiler backend uses the Compiler Service contract. Generic configuration,
runner, and CLI modules must not gain scattered tool-name branches.
